#include "wkv/console.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wkv {
namespace {

// Splits `s` into at most `maxFields` whitespace-delimited fields; the last
// field absorbs the remainder of the string (so values/free text may
// contain spaces).
std::vector<std::string> splitFields(const std::string& s, size_t maxFields) {
  std::vector<std::string> out;
  size_t i = 0, n = s.size();
  while (maxFields > 1 && out.size() + 1 < maxFields) {
    while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= n) break;
    size_t start = i;
    while (i < n && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    out.push_back(s.substr(start, i - start));
  }
  while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  if (i < n) out.push_back(s.substr(i));
  return out;
}

PageID parsePageId(const std::string& s) { return static_cast<PageID>(std::strtoull(s.c_str(), nullptr, 10)); }

}  // namespace

Console::Console(std::string dbPath, std::string walPath, int poolSize)
    : dbPath_(std::move(dbPath)), walPath_(std::move(walPath)), poolSize_(poolSize) {}

bool Console::ensureOpen(std::string* err) {
  if (opened_) return true;

  disk_ = DiskManager::open(dbPath_, err);
  if (!disk_) return false;
  bp_ = std::make_unique<BufferPool>(disk_.get(), poolSize_);
  wal_ = BatchingWAL::open(walPath_, err);
  if (!wal_) return false;
  db_ = std::make_unique<DB>(bp_.get(), wal_.get());
  if (!recoverDB(walPath_, db_.get(), err)) return false;

  opened_ = true;
  return true;
}

bool Console::execute(const std::string& command, std::string* result, std::string* err) {
  result->clear();
  if (!ensureOpen(err)) return false;

  std::vector<std::string> head = splitFields(command, 2);
  if (head.empty()) {
    if (err) *err = "empty command";
    return false;
  }
  std::string verb = head[0];
  std::string rest = head.size() > 1 ? head[1] : std::string();
  for (char& c : verb) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

  if (verb == "SET") {
    std::vector<std::string> args = splitFields(rest, 3);
    if (args.size() < 3) {
      if (err) *err = "usage: SET <page> <key> <value>";
      return false;
    }
    uint64_t txn = db_->beginTxn();
    if (!db_->write(parsePageId(args[0]), txn, args[1], args[2], 0, err)) return false;
    *result = "OK";
    return true;
  }

  if (verb == "GET") {
    std::vector<std::string> args = splitFields(rest, 2);
    if (args.size() < 2) {
      if (err) *err = "usage: GET <page> <key>";
      return false;
    }
    bool found = false;
    std::string value;
    uint64_t txn = db_->beginTxn();
    if (!db_->read(parsePageId(args[0]), txn, args[1], &found, &value, err)) return false;
    *result = (found && !value.empty()) ? value : "(nil)";
    return true;
  }

  if (verb == "DEL") {
    std::vector<std::string> args = splitFields(rest, 2);
    if (args.size() < 2) {
      if (err) *err = "usage: DEL <page> <key>";
      return false;
    }
    uint64_t txn = db_->beginTxn();
    // Empty value is the tombstone GET/SCAN treat as missing. The page
    // engine has no separate delete; this is the console-level mapping
    // UniLoader uses for guikit hostDbDelete.
    if (!db_->write(parsePageId(args[0]), txn, args[1], "", 0, err)) return false;
    *result = "OK";
    return true;
  }

  if (verb == "SCAN") {
    std::vector<std::string> args = splitFields(rest, 2);
    if (args.empty()) {
      if (err) *err = "usage: SCAN <page> [prefix]";
      return false;
    }
    std::string prefix = args.size() > 1 ? args[1] : std::string();
    uint64_t txn = db_->beginTxn();
    std::string out;
    bool ok = db_->scan(
        parsePageId(args[0]), txn, prefix,
        [&](const std::string& k, const std::string& v) {
          if (v.empty()) return true;  // DEL tombstone
          out += k;
          out += '=';
          out += v;
          out += '\n';
          return true;
        },
        err);
    if (!ok) return false;
    *result = out;
    return true;
  }

  if (verb == "HSET") {
    std::vector<std::string> args = splitFields(rest, 4);
    if (args.size() < 4) {
      if (err) *err = "usage: HSET <page> <hashKey> <field> <value>";
      return false;
    }
    uint64_t txn = db_->beginTxn();
    if (!db_->hset(parsePageId(args[0]), txn, args[1], args[2], args[3], 0, err)) return false;
    *result = "OK";
    return true;
  }

  if (verb == "INDEX") {
    std::vector<std::string> args = splitFields(rest, 2);
    if (args.size() < 2) {
      if (err) *err = "usage: INDEX <docId> <text>";
      return false;
    }
    uint64_t docId = std::strtoull(args[0].c_str(), nullptr, 10);
    index_.add(docId, args[1]);
    *result = "OK";
    return true;
  }

  if (verb == "SEARCH") {
    std::string segPath = dbPath_ + ".idx";
    if (!index_.writeSegment(segPath, err)) return false;

    FILE* f = std::fopen(segPath.c_str(), "rb");
    if (!f) {
      if (err) *err = std::strerror(errno);
      return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size > 0 ? static_cast<size_t>(size) : 0);
    if (!data.empty()) std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);

    SegmentSearcher searcher(std::move(data));
    std::vector<uint64_t> ids;
    if (!searcher.search(rest, &ids, err)) return false;

    std::string out;
    for (size_t i = 0; i < ids.size(); ++i) {
      if (i) out += ',';
      out += std::to_string(ids[i]);
    }
    *result = out;
    return true;
  }

  if (verb == "CLOSE") {
    if (!db_->close(err)) return false;
    db_.reset();
    wal_.reset();
    bp_.reset();
    disk_.reset();
    opened_ = false;
    *result = "OK";
    return true;
  }

  if (err) *err = "unknown command: " + verb;
  return false;
}

}  // namespace wkv
