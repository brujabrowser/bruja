#pragma once
// Port of ultimate_db.go sections 6-8 (B+ tree, cursor, sort-merge join).
//
// On-disk cell layout is preserved bit-for-bit from the Go original,
// including its one inconsistency: RightmostChildID is stored as a 4-byte
// LE value while every other PageID field (NextLeafID, ParentID, and each
// internal cell's child pointer) is 8 bytes. Kept as-is for wire
// compatibility with existing ultimate_db.go-written files.

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "wkv/buffer_pool.h"
#include "wkv/page.h"

namespace wkv {

constexpr uint16_t kPageTypeInternal = 1;
constexpr uint16_t kPageTypeLeaf = 2;
constexpr uint32_t kBTreeHeaderSize = 24;

// Thin, non-owning view over a Page's bytes as a B+ tree node. Copyable;
// never outlives the Page it wraps.
class BTreePage {
 public:
  explicit BTreePage(Page* page) : page_(page) {}

  Page* page() const { return page_; }
  PageID id() const { return page_->id; }

  void btreeInit() { std::memset(page_->data.data(), 0, kBTreeHeaderSize); }

  uint16_t pageType() const { return loadLE16(d() + 0); }
  void setPageType(uint16_t t) { storeLE16(d() + 0, t); }
  uint16_t numCells() const { return loadLE16(d() + 2); }
  void setNumCells(uint16_t n) { storeLE16(d() + 2, n); }
  PageID nextLeafId() const { return static_cast<PageID>(loadLE64(d() + 4)); }
  void setNextLeafId(PageID id) { storeLE64(d() + 4, static_cast<uint64_t>(id)); }
  PageID parentId() const { return static_cast<PageID>(loadLE64(d() + 12)); }
  void setParentId(PageID id) { storeLE64(d() + 12, static_cast<uint64_t>(id)); }
  // NOTE: 4 bytes, not 8 -- see file header comment.
  PageID rightmostChildId() const { return static_cast<PageID>(loadLE32(d() + 20)); }
  void setRightmostChildId(PageID id) { storeLE32(d() + 20, static_cast<uint32_t>(id)); }

  bool isSafeForInsert(uint32_t requiredBytes) const;

  uint8_t* d() { return page_->data.data(); }
  const uint8_t* d() const { return page_->data.data(); }

 private:
  Page* page_;
};

class BTree {
 public:
  BTree(BufferPool* bp, PageID rootId) : bp_(bp), rootId_(rootId) {}

  PageID rootId() const { return rootId_; }
  BufferPool* bufferPool() const { return bp_; }

  bool insert(const std::string& key, const std::string& value, std::string* err);

  // Returns the leaf page for `key`, read-locked and pinned. Caller must
  // page->latch.unlock_shared() then bp->unpinPage(page->id, false).
  Page* findLeaf(const std::string& key, std::string* err);

  bool scan(const std::string& prefix, std::vector<std::string>* keys,
            std::vector<std::string>* values, std::string* err);

 private:
  friend class BTreeCursor;

  PageID findChildInInternalNode(BTreePage& node, const std::string& searchKey) const;
  // Returns false with *pageFull=true if the leaf has no room (caller should split).
  bool insertIntoLeaf(BTreePage& node, const std::string& newKey, const std::string& newVal,
                       bool* pageFull, std::string* err);
  bool insertIntoInternal(BTreePage& node, const std::string& pivotKey, PageID leftChildId,
                           PageID rightChildId, bool* pageFull, std::string* err);
  bool splitLeaf(BTreePage& node, std::vector<Page*>& lockedAncestors, std::string* err);
  bool splitInternalNode(BTreePage& node, std::vector<Page*>& lockedAncestors, std::string* err);
  bool promoteToParent(PageID leftChildId, PageID rightChildId, const std::string& pivotKey,
                        std::vector<Page*>& lockedAncestors, std::string* err);
  void releaseAncestors(std::vector<Page*>& ancestors);

  BufferPool* bp_;
  PageID rootId_;
};

// Sequential, lazy iterator over a BTree's leaf nodes.
class BTreeCursor {
 public:
  ~BTreeCursor() { close(); }

  static std::unique_ptr<BTreeCursor> create(BTree* tree, std::string* err);

  // Returns true and fills key/value on success. Returns false with *eof set
  // when iteration is exhausted (not an error); returns false with *eof
  // false and *err set on I/O error.
  bool next(std::string* key, std::string* value, bool* eof, std::string* err);
  void close();

 private:
  BTreeCursor(BTree* tree, Page* node, uint16_t cellIdx, uint32_t offset, bool isEof)
      : tree_(tree), node_(node), cellIdx_(cellIdx), offset_(offset), isEof_(isEof) {}

  BTree* tree_;
  Page* node_;  // read-locked while non-null
  uint16_t cellIdx_;
  uint32_t offset_;
  bool isEof_;
};

struct JoinResult {
  std::string key;
  std::string leftValue;
  std::string rightValue;
};

bool sortMergeJoin(BTree* leftTree, BTree* rightTree, std::vector<JoinResult>* out,
                    std::string* err);

}  // namespace wkv
