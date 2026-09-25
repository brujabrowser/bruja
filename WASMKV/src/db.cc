#include "wkv/db.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "wkv/crc32.h"
#include "wkv/time_util.h"

namespace wkv {
namespace {

bool hasPrefix(const uint8_t* data, uint32_t len, const std::string& prefix) {
  if (prefix.size() > len) return false;
  return std::memcmp(data, prefix.data(), prefix.size()) == 0;
}

void compactPage(Page* page) {
  uint32_t currentOffset = kPageHeaderSize;
  uint32_t freeOffset = page->freeSpaceOffset();
  int64_t now = nowNanos();

  struct Record {
    uint64_t txnId;
    uint64_t expiresAt;
    std::string key;
    std::string val;
  };
  std::unordered_map<std::string, Record> latest;

  while (currentOffset < freeOffset) {
    const uint8_t* rec = page->data.data() + currentOffset;
    uint64_t recordTxnId = loadLE64(rec + 0);
    uint64_t expiresAt = loadLE64(rec + 8);
    uint32_t keyLen = loadLE32(rec + 16);
    uint32_t valLen = loadLE32(rec + 20);

    std::string k(reinterpret_cast<const char*>(rec + 24), keyLen);
    std::string v(reinterpret_cast<const char*>(rec + 24 + keyLen), valLen);

    auto it = latest.find(k);
    if (it == latest.end() || recordTxnId > it->second.txnId) {
      if (static_cast<int64_t>(expiresAt) > now) {
        latest[k] = Record{recordTxnId, expiresAt, k, v};
      } else if (it != latest.end()) {
        latest.erase(it);  // expired version overwrites old version = tombstone
      }
    }
    currentOffset += kRecordHeaderSize + keyLen + valLen;
  }

  page->init();
  uint32_t newOffset = kPageHeaderSize;

  for (auto& [k, rec] : latest) {
    uint8_t* t = page->data.data() + newOffset;
    storeLE64(t + 0, rec.txnId);
    storeLE64(t + 8, rec.expiresAt);
    storeLE32(t + 16, static_cast<uint32_t>(rec.key.size()));
    storeLE32(t + 20, static_cast<uint32_t>(rec.val.size()));
    std::memcpy(t + 24, rec.key.data(), rec.key.size());
    std::memcpy(t + 24 + rec.key.size(), rec.val.data(), rec.val.size());
    newOffset += kRecordHeaderSize + static_cast<uint32_t>(rec.key.size()) +
                 static_cast<uint32_t>(rec.val.size());
  }

  page->setFreeSpaceOffset(newOffset);
}

// Shared by DB::write and DB::restoreWrite: appends one record to the page,
// compacting first if it doesn't fit.
bool writeRecordToPage(BufferPool* bp, PageID pageId, uint64_t txnId, int64_t expiresAt,
                        const std::string& key, const std::string& value, std::string* err) {
  Page* page = bp->fetchPage(pageId, err);
  if (!page) return false;

  std::unique_lock<std::shared_mutex> lock(page->latch);

  uint32_t freeOffset = page->freeSpaceOffset();
  if (freeOffset == 0) {
    page->init();
    freeOffset = page->freeSpaceOffset();
  }

  uint32_t recordSize = kRecordHeaderSize + static_cast<uint32_t>(key.size()) +
                        static_cast<uint32_t>(value.size());

  if (freeOffset + recordSize > kPageSize) {
    compactPage(page);
    freeOffset = page->freeSpaceOffset();
  }
  if (freeOffset + recordSize > kPageSize) {
    if (err) *err = "page overflow";
    bp->unpinPage(pageId, true);
    return false;
  }

  uint8_t* t = page->data.data() + freeOffset;
  storeLE64(t + 0, txnId);
  storeLE64(t + 8, static_cast<uint64_t>(expiresAt));
  storeLE32(t + 16, static_cast<uint32_t>(key.size()));
  storeLE32(t + 20, static_cast<uint32_t>(value.size()));
  std::memcpy(t + 24, key.data(), key.size());
  std::memcpy(t + 24 + key.size(), value.data(), value.size());

  page->setFreeSpaceOffset(freeOffset + recordSize);
  bp->unpinPage(pageId, true);
  return true;
}

}  // namespace

bool DB::write(PageID pageId, uint64_t txnId, const std::string& key, const std::string& value,
               int64_t ttlNanos, std::string* err) {
  int64_t expiresAt = ttlNanos > 0 ? nowNanos() + ttlNanos : kExpireNever;

  if (!wal_->append(txnId, expiresAt, pageId, key, value, err)) return false;

  return writeRecordToPage(bp_, pageId, txnId, expiresAt, key, value, err);
}

bool DB::restoreWrite(uint64_t txnId, int64_t expiresAt, PageID pageId, const std::string& key,
                      const std::string& value, std::string* err) {
  return writeRecordToPage(bp_, pageId, txnId, expiresAt, key, value, err);
}

bool DB::read(PageID pageId, uint64_t readTxnId, const std::string& key, bool* found,
              std::string* value, std::string* err) {
  *found = false;
  Page* page = bp_->fetchPage(pageId, err);
  if (!page) return false;

  std::shared_lock<std::shared_mutex> lock(page->latch);

  uint32_t freeOffset = page->freeSpaceOffset();
  if (freeOffset == 0) {
    bp_->unpinPage(pageId, false);
    return true;  // key not found
  }

  uint32_t currentOffset = kPageHeaderSize;
  bool haveValue = false;
  std::string latestValue;
  uint64_t highestTxnId = 0;
  int64_t now = nowNanos();

  while (currentOffset < freeOffset) {
    const uint8_t* rec = page->data.data() + currentOffset;
    uint64_t recordTxnId = loadLE64(rec + 0);
    int64_t expiresAt = static_cast<int64_t>(loadLE64(rec + 8));
    uint32_t keyLen = loadLE32(rec + 16);
    uint32_t valLen = loadLE32(rec + 20);

    const char* recordKey = reinterpret_cast<const char*>(rec + 24);
    const char* recordVal = reinterpret_cast<const char*>(rec + 24 + keyLen);

    if (keyLen == key.size() && std::memcmp(recordKey, key.data(), keyLen) == 0 &&
        recordTxnId <= readTxnId && recordTxnId >= highestTxnId) {
      if (expiresAt > now) {
        highestTxnId = recordTxnId;
        latestValue.assign(recordVal, valLen);
        haveValue = true;
      } else {
        highestTxnId = recordTxnId;
        haveValue = false;  // tombstone
      }
    }
    currentOffset += kRecordHeaderSize + keyLen + valLen;
  }

  bp_->unpinPage(pageId, false);

  if (haveValue) {
    *found = true;
    *value = std::move(latestValue);
  }
  return true;
}

bool DB::scan(PageID pageId, uint64_t readTxnId, const std::string& prefix, const ScanIter& iter,
              std::string* err) {
  Page* page = bp_->fetchPage(pageId, err);
  if (!page) return false;

  std::shared_lock<std::shared_mutex> lock(page->latch);

  uint32_t freeOffset = page->freeSpaceOffset();
  if (freeOffset == 0 || freeOffset <= kPageHeaderSize) {
    bp_->unpinPage(pageId, false);
    return true;  // empty page
  }

  struct ScanRecord {
    uint64_t txnId;
    std::string value;
  };
  std::unordered_map<std::string, ScanRecord> latest;
  int64_t now = nowNanos();
  uint32_t currentOffset = kPageHeaderSize;

  // Pass 1: build the latest valid version per key.
  while (currentOffset < freeOffset) {
    const uint8_t* rec = page->data.data() + currentOffset;
    uint64_t recordTxnId = loadLE64(rec + 0);
    int64_t expiresAt = static_cast<int64_t>(loadLE64(rec + 8));
    uint32_t keyLen = loadLE32(rec + 16);
    uint32_t valLen = loadLE32(rec + 20);

    const uint8_t* recordKey = rec + 24;
    const char* recordVal = reinterpret_cast<const char*>(rec + 24 + keyLen);

    if (hasPrefix(recordKey, keyLen, prefix) && recordTxnId <= readTxnId) {
      std::string k(reinterpret_cast<const char*>(recordKey), keyLen);
      auto it = latest.find(k);
      if (it == latest.end() || recordTxnId >= it->second.txnId) {
        if (expiresAt > now) {
          latest[k] = ScanRecord{recordTxnId, std::string(recordVal, valLen)};
        } else {
          latest.erase(k);  // expired -- acts as a tombstone
        }
      }
    }
    currentOffset += kRecordHeaderSize + keyLen + valLen;
  }

  bp_->unpinPage(pageId, false);

  // Pass 2: yield the deduplicated results.
  for (auto& [k, rec] : latest) {
    if (!iter(k, rec.value)) break;
  }
  return true;
}

bool DB::hset(PageID pageId, uint64_t txnId, const std::string& hashKey, const std::string& field,
              const std::string& value, int64_t ttlNanos, std::string* err) {
  std::string compositeKey;
  compositeKey.reserve(std::strlen(kHashPrefix) + hashKey.size() + 1 + field.size());
  compositeKey += kHashPrefix;
  compositeKey += hashKey;
  compositeKey += ':';
  compositeKey += field;
  return write(pageId, txnId, compositeKey, value, ttlNanos, err);
}

bool DB::close(std::string* err) {
  if (!bp_->flushAll(err)) return false;
  return wal_->close(err);
}

bool recoverDB(const std::string& walPath, DB* db, std::string* err) {
  FILE* f = std::fopen(walPath.c_str(), "rb");
  if (!f) return true;  // no WAL yet -- fresh database, not an error

  uint64_t maxTxnId = 0;
  uint8_t header[36];

  for (;;) {
    size_t n = std::fread(header, 1, 36, f);
    if (n == 0) break;  // clean EOF between records
    if (n != 36) {
      std::fclose(f);
      if (err) *err = "corrupted WAL: unexpected EOF";
      return false;
    }

    uint32_t expectedCrc = loadLE32(header + 0);
    uint64_t txnId = loadLE64(header + 4);
    int64_t expiresAt = static_cast<int64_t>(loadLE64(header + 12));
    PageID pageId = static_cast<PageID>(loadLE64(header + 20));
    uint32_t keyLen = loadLE32(header + 28);
    uint32_t valLen = loadLE32(header + 32);

    if (keyLen > (1u << 20) || valLen > (1u << 28)) {
      std::fclose(f);
      if (err) *err = "corrupted WAL: limits exceeded";
      return false;
    }

    std::vector<uint8_t> payload(static_cast<size_t>(keyLen) + valLen);
    if (!payload.empty() && std::fread(payload.data(), 1, payload.size(), f) != payload.size()) {
      std::fclose(f);
      if (err) *err = "corrupted WAL: missing payload";
      return false;
    }

    uint32_t crc = crc32Begin();
    crc = crc32Append(crc, header + 4, 32);
    crc = crc32Append(crc, payload.data(), payload.size());
    if (crc32End(crc) != expectedCrc) {
      std::fclose(f);
      if (err) *err = "corrupted WAL: CRC mismatch";
      return false;
    }

    std::string key(reinterpret_cast<const char*>(payload.data()), keyLen);
    std::string val(reinterpret_cast<const char*>(payload.data() + keyLen), valLen);

    if (!db->restoreWrite(txnId, expiresAt, pageId, key, val, err)) {
      std::fclose(f);
      return false;
    }

    if (txnId > maxTxnId) maxTxnId = txnId;
  }

  std::fclose(f);
  db->setNextTxnId(maxTxnId);
  return true;
}

}  // namespace wkv
