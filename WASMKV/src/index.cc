#include "wkv/index.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "wkv/page.h"
#include "wkv/query.h"
#include "wkv/varint.h"

namespace wkv {

std::vector<std::string> tokenize(const std::string& text) {
  // ASCII-only, matching the Go original's clean() predicate (which only
  // ever recognizes ASCII letters/digits regardless of Unicode input).
  std::vector<std::string> out;
  std::string cur;
  for (unsigned char c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (ok) {
      cur.push_back(static_cast<char>(c));
    } else if (!cur.empty()) {
      out.push_back(std::move(cur));
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

void MemIndex::add(uint64_t docId, const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> tokens = tokenize(text);
  std::unordered_map<std::string, bool> seen;
  for (auto& t : tokens) {
    if (!seen[t]) {
      postings_[t].push_back(docId);
      seen[t] = true;
    }
  }
  ++docsCount_;
}

bool MemIndex::writeSegment(const std::string& path, std::string* err) const {
  std::lock_guard<std::mutex> lock(mu_);

  std::vector<std::string> terms;
  terms.reserve(postings_.size());
  for (auto& [t, _] : postings_) terms.push_back(t);
  std::sort(terms.begin(), terms.end());

  std::vector<uint8_t> out;
  std::vector<uint64_t> termOffsets;
  uint64_t currentOffset = 0;

  for (auto& t : terms) {
    termOffsets.push_back(currentOffset);
    std::vector<uint64_t> postingsList = postings_.at(t);
    std::sort(postingsList.begin(), postingsList.end());

    size_t before = out.size();
    uint8_t countBuf[4];
    storeLE32(countBuf, static_cast<uint32_t>(postingsList.size()));
    out.insert(out.end(), countBuf, countBuf + 4);

    uint64_t lastId = 0;
    for (uint64_t id : postingsList) {
      putUvarint(&out, id - lastId);
      lastId = id;
    }
    currentOffset += out.size() - before;
  }

  uint64_t dictOffset = currentOffset;
  for (size_t i = 0; i < terms.size(); ++i) {
    const std::string& t = terms[i];
    uint8_t lenBuf[4];
    storeLE32(lenBuf, static_cast<uint32_t>(t.size()));
    out.insert(out.end(), lenBuf, lenBuf + 4);
    out.insert(out.end(), t.begin(), t.end());
    uint8_t offBuf[8];
    storeLE64(offBuf, termOffsets[i]);
    out.insert(out.end(), offBuf, offBuf + 8);
  }

  uint8_t dictOffBuf[8];
  storeLE64(dictOffBuf, dictOffset);
  out.insert(out.end(), dictOffBuf, dictOffBuf + 8);

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  size_t written = out.empty() ? 0 : std::fwrite(out.data(), 1, out.size(), f);
  std::fclose(f);
  if (written != out.size()) {
    if (err) *err = "short write";
    return false;
  }
  return true;
}

void SegmentSearcher::initDict() {
  dict_.clear();
  if (data_.size() < 8) return;
  uint64_t dictOffset = loadLE64(data_.data() + data_.size() - 8);
  size_t pos = static_cast<size_t>(dictOffset);
  size_t end = data_.size() - 8;
  while (pos + 4 <= end) {
    uint32_t termLen = loadLE32(data_.data() + pos);
    pos += 4;
    if (pos + termLen > end) break;
    std::string term(reinterpret_cast<const char*>(data_.data() + pos), termLen);
    pos += termLen;
    if (pos + 8 > end) break;
    uint64_t offset = loadLE64(data_.data() + pos);
    pos += 8;
    dict_[term] = offset;
  }
}

std::vector<uint64_t> SegmentSearcher::fetchPostings(const std::string& term) {
  std::call_once(dictOnce_, [this] { initDict(); });

  auto it = dict_.find(term);
  if (it == dict_.end()) return {};

  size_t pos = static_cast<size_t>(it->second);
  if (pos + 4 > data_.size()) return {};
  uint32_t postCount = loadLE32(data_.data() + pos);
  pos += 4;

  std::vector<uint64_t> results;
  results.reserve(postCount);
  uint64_t lastId = 0;
  for (uint32_t i = 0; i < postCount; ++i) {
    uint64_t delta = 0;
    if (!readUvarint(data_.data(), data_.size(), &pos, &delta)) break;
    uint64_t id = lastId + delta;
    results.push_back(id);
    lastId = id;
  }
  return results;
}

bool SegmentSearcher::search(const std::string& queryString, std::vector<uint64_t>* out,
                              std::string* err) {
  std::unique_ptr<Query> ast = parseQuery(queryString, err);
  if (!ast) return false;
  *out = ast->execute(this);
  return true;
}

}  // namespace wkv
