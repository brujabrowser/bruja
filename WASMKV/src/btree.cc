#include "wkv/btree.h"

#include <cstring>

namespace wkv {
namespace {

int compareBytes(const uint8_t* a, uint32_t aLen, const uint8_t* b, uint32_t bLen) {
  uint32_t n = aLen < bLen ? aLen : bLen;
  int c = n ? std::memcmp(a, b, n) : 0;
  if (c != 0) return c;
  if (aLen < bLen) return -1;
  if (aLen > bLen) return 1;
  return 0;
}

bool hasPrefix(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

}  // namespace

bool BTreePage::isSafeForInsert(uint32_t requiredBytes) const {
  uint16_t cells = numCells();
  uint32_t offset = kBTreeHeaderSize;
  bool leaf = pageType() == kPageTypeLeaf;
  for (uint16_t i = 0; i < cells; ++i) {
    if (leaf) {
      uint16_t kLen = loadLE16(d() + offset);
      uint16_t vLen = loadLE16(d() + offset + 2);
      offset += 4 + kLen + vLen;
    } else {
      uint16_t kLen = loadLE16(d() + offset);
      offset += 10 + kLen;
    }
  }
  return (offset + requiredBytes) < kPageSize;
}

PageID BTree::findChildInInternalNode(BTreePage& node, const std::string& searchKey) const {
  uint16_t numCells = node.numCells();
  uint32_t offset = kBTreeHeaderSize;
  for (uint16_t i = 0; i < numCells; ++i) {
    uint16_t kLen = loadLE16(node.d() + offset);
    PageID childId = static_cast<PageID>(loadLE64(node.d() + offset + 2));
    const uint8_t* cellKey = node.d() + offset + 10;
    if (compareBytes(reinterpret_cast<const uint8_t*>(searchKey.data()),
                      static_cast<uint32_t>(searchKey.size()), cellKey, kLen) < 0) {
      return childId;
    }
    offset += 10 + kLen;
  }
  return node.rightmostChildId();
}

bool BTree::insertIntoLeaf(BTreePage& node, const std::string& newKey, const std::string& newVal,
                            bool* pageFull, std::string* err) {
  *pageFull = false;
  uint32_t reqBytes = 4 + static_cast<uint32_t>(newKey.size()) + static_cast<uint32_t>(newVal.size());
  if (!node.isSafeForInsert(reqBytes)) {
    *pageFull = true;
    return false;
  }

  uint16_t numCells = node.numCells();
  uint32_t offset = kBTreeHeaderSize;
  uint32_t insertOffset = 0;
  bool foundInsertPoint = false;

  for (uint16_t i = 0; i < numCells; ++i) {
    uint16_t kLen = loadLE16(node.d() + offset);
    uint16_t vLen = loadLE16(node.d() + offset + 2);
    const uint8_t* cellKey = node.d() + offset + 4;

    if (!foundInsertPoint && compareBytes(reinterpret_cast<const uint8_t*>(newKey.data()),
                                           static_cast<uint32_t>(newKey.size()), cellKey,
                                           kLen) < 0) {
      insertOffset = offset;
      foundInsertPoint = true;
    }
    offset += 4 + kLen + vLen;
  }

  if (!foundInsertPoint) insertOffset = offset;

  if (insertOffset < offset) {
    uint32_t bytesToShift = offset - insertOffset;
    std::memmove(node.d() + insertOffset + reqBytes, node.d() + insertOffset, bytesToShift);
  }

  storeLE16(node.d() + insertOffset, static_cast<uint16_t>(newKey.size()));
  storeLE16(node.d() + insertOffset + 2, static_cast<uint16_t>(newVal.size()));
  uint32_t keyStart = insertOffset + 4;
  uint32_t valStart = keyStart + static_cast<uint32_t>(newKey.size());
  std::memcpy(node.d() + keyStart, newKey.data(), newKey.size());
  std::memcpy(node.d() + valStart, newVal.data(), newVal.size());

  node.setNumCells(numCells + 1);
  (void)err;
  return true;
}

bool BTree::insertIntoInternal(BTreePage& node, const std::string& pivotKey, PageID leftChildId,
                                PageID rightChildId, bool* pageFull, std::string* err) {
  *pageFull = false;
  uint32_t reqBytes = 10 + static_cast<uint32_t>(pivotKey.size());
  if (!node.isSafeForInsert(reqBytes)) {
    *pageFull = true;
    return false;
  }

  uint16_t numCells = node.numCells();
  uint32_t offset = kBTreeHeaderSize;
  uint32_t insertOffset = 0;
  bool foundInsertPoint = false;

  for (uint16_t i = 0; i < numCells; ++i) {
    uint16_t kLen = loadLE16(node.d() + offset);
    const uint8_t* cellKey = node.d() + offset + 10;

    if (!foundInsertPoint && compareBytes(reinterpret_cast<const uint8_t*>(pivotKey.data()),
                                           static_cast<uint32_t>(pivotKey.size()), cellKey,
                                           kLen) < 0) {
      insertOffset = offset;
      foundInsertPoint = true;
    }
    offset += 10 + kLen;
  }

  if (!foundInsertPoint) {
    insertOffset = offset;
    node.setRightmostChildId(rightChildId);
  } else {
    uint32_t bytesToShift = offset - insertOffset;
    std::memmove(node.d() + insertOffset + reqBytes, node.d() + insertOffset, bytesToShift);
    uint32_t pushedCellLeftChildOffset = insertOffset + reqBytes + 2;
    storeLE64(node.d() + pushedCellLeftChildOffset, static_cast<uint64_t>(rightChildId));
  }

  storeLE16(node.d() + insertOffset, static_cast<uint16_t>(pivotKey.size()));
  storeLE64(node.d() + insertOffset + 2, static_cast<uint64_t>(leftChildId));
  std::memcpy(node.d() + insertOffset + 10, pivotKey.data(), pivotKey.size());

  node.setNumCells(numCells + 1);
  (void)err;
  return true;
}

bool BTree::splitLeaf(BTreePage& node, std::vector<Page*>& lockedAncestors, std::string* err) {
  Page* newRawPage = bp_->newPage(err);
  if (!newRawPage) return false;
  newRawPage->latch.lock();

  BTreePage newLeaf(newRawPage);
  newLeaf.btreeInit();
  newLeaf.setPageType(kPageTypeLeaf);
  newLeaf.setNextLeafId(node.nextLeafId());
  node.setNextLeafId(newLeaf.id());
  newLeaf.setParentId(node.parentId());

  uint16_t numCells = node.numCells();
  uint16_t midPoint = numCells / 2;

  uint32_t offset = kBTreeHeaderSize;
  std::string midKey;

  for (uint16_t i = 0; i < numCells; ++i) {
    uint16_t kLen = loadLE16(node.d() + offset);
    uint16_t vLen = loadLE16(node.d() + offset + 2);
    if (i == midPoint) {
      midKey.assign(reinterpret_cast<const char*>(node.d() + offset + 4), kLen);
      break;
    }
    offset += 4 + kLen + vLen;
  }

  uint32_t bytesToMove = kPageSize - offset;
  std::memcpy(newLeaf.d() + kBTreeHeaderSize, node.d() + offset, bytesToMove);

  newLeaf.setNumCells(numCells - midPoint);
  node.setNumCells(midPoint);
  std::memset(node.d() + offset, 0, kPageSize - offset);

  newRawPage->latch.unlock();
  bp_->unpinPage(newRawPage->id, true);

  return promoteToParent(node.id(), newLeaf.id(), midKey, lockedAncestors, err);
}

bool BTree::splitInternalNode(BTreePage& node, std::vector<Page*>& lockedAncestors,
                               std::string* err) {
  Page* newRawPage = bp_->newPage(err);
  if (!newRawPage) return false;
  newRawPage->latch.lock();

  BTreePage newInternal(newRawPage);
  newInternal.btreeInit();
  newInternal.setPageType(kPageTypeInternal);
  newInternal.setParentId(node.parentId());

  uint16_t numCells = node.numCells();
  uint16_t midPoint = numCells / 2;

  uint32_t offset = kBTreeHeaderSize;
  std::string pivotKey;
  PageID midCellLeftChild = 0;
  uint32_t midCellEnd = 0;

  for (uint16_t i = 0; i < numCells; ++i) {
    uint16_t kLen = loadLE16(node.d() + offset);
    if (i == midPoint) {
      midCellLeftChild = static_cast<PageID>(loadLE64(node.d() + offset + 2));
      pivotKey.assign(reinterpret_cast<const char*>(node.d() + offset + 10), kLen);
      midCellEnd = offset + 10 + kLen;
      break;
    }
    offset += 10 + kLen;
  }

  newInternal.setRightmostChildId(node.rightmostChildId());
  node.setRightmostChildId(midCellLeftChild);

  uint32_t bytesToMove = kPageSize - midCellEnd;
  std::memcpy(newInternal.d() + kBTreeHeaderSize, node.d() + midCellEnd, bytesToMove);

  newInternal.setNumCells(numCells - midPoint - 1);
  node.setNumCells(midPoint);
  // Zeroes from `offset` (start of the discarded midpoint cell), not
  // midCellEnd -- matches ultimate_db.go exactly.
  std::memset(node.d() + offset, 0, kPageSize - offset);

  newRawPage->latch.unlock();
  bp_->unpinPage(newRawPage->id, true);

  return promoteToParent(node.id(), newInternal.id(), pivotKey, lockedAncestors, err);
}

bool BTree::promoteToParent(PageID leftChildId, PageID rightChildId, const std::string& pivotKey,
                             std::vector<Page*>& lockedAncestors, std::string* err) {
  if (leftChildId == rootId_) {
    Page* newRootRaw = bp_->newPage(err);
    if (!newRootRaw) return false;
    newRootRaw->latch.lock();

    BTreePage newRoot(newRootRaw);
    newRoot.btreeInit();
    newRoot.setPageType(kPageTypeInternal);
    newRoot.setNumCells(1);

    uint32_t offset = kBTreeHeaderSize;
    storeLE16(newRoot.d() + offset, static_cast<uint16_t>(pivotKey.size()));
    storeLE64(newRoot.d() + offset + 2, static_cast<uint64_t>(leftChildId));
    std::memcpy(newRoot.d() + offset + 10, pivotKey.data(), pivotKey.size());

    newRoot.setRightmostChildId(rightChildId);
    rootId_ = newRoot.id();

    newRootRaw->latch.unlock();
    bp_->unpinPage(newRootRaw->id, true);
    return true;
  }

  Page* parentRaw = lockedAncestors[lockedAncestors.size() - 2];
  BTreePage parentNode(parentRaw);
  bool pageFull = false;
  bool ok = insertIntoInternal(parentNode, pivotKey, leftChildId, rightChildId, &pageFull, err);

  if (!ok && pageFull) {
    std::vector<Page*> parentAncestors(lockedAncestors.begin(), lockedAncestors.end() - 1);
    bool splitOk = splitInternalNode(parentNode, parentAncestors, err);
    return splitOk;
  }
  return ok;
}

void BTree::releaseAncestors(std::vector<Page*>& ancestors) {
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
    Page* page = *it;
    page->latch.unlock();
    bp_->unpinPage(page->id, true);
  }
}

bool BTree::insert(const std::string& key, const std::string& value, std::string* err) {
  PageID currId = rootId_;
  std::vector<Page*> lockedAncestors;

  Page* currRaw = bp_->fetchPage(currId, err);
  if (!currRaw) return false;
  currRaw->latch.lock();
  lockedAncestors.push_back(currRaw);
  BTreePage currNode(currRaw);

  uint32_t reqBytes = 4 + static_cast<uint32_t>(key.size()) + static_cast<uint32_t>(value.size());

  while (currNode.pageType() == kPageTypeInternal) {
    PageID childId = findChildInInternalNode(currNode, key);
    Page* childRaw = bp_->fetchPage(childId, err);
    if (!childRaw) {
      releaseAncestors(lockedAncestors);
      return false;
    }
    childRaw->latch.lock();
    BTreePage childNode(childRaw);

    if (childNode.isSafeForInsert(reqBytes)) {
      releaseAncestors(lockedAncestors);
      lockedAncestors.clear();
    }

    lockedAncestors.push_back(childRaw);
    currId = childId;
    currNode = childNode;
  }

  bool pageFull = false;
  bool ok = insertIntoLeaf(currNode, key, value, &pageFull, err);
  if (!ok && pageFull) {
    ok = splitLeaf(currNode, lockedAncestors, err);
  }

  releaseAncestors(lockedAncestors);
  return ok;
}

Page* BTree::findLeaf(const std::string& key, std::string* err) {
  PageID currId = rootId_;
  Page* currRaw = bp_->fetchPage(currId, err);
  if (!currRaw) return nullptr;
  currRaw->latch.lock_shared();
  BTreePage currNode(currRaw);

  while (currNode.pageType() == kPageTypeInternal) {
    PageID childId = findChildInInternalNode(currNode, key);
    Page* childRaw = bp_->fetchPage(childId, err);
    if (!childRaw) {
      currNode.page()->latch.unlock_shared();
      bp_->unpinPage(currId, false);
      return nullptr;
    }
    childRaw->latch.lock_shared();
    currNode.page()->latch.unlock_shared();
    bp_->unpinPage(currId, false);
    currId = childId;
    currNode = BTreePage(childRaw);
  }
  return currNode.page();
}

bool BTree::scan(const std::string& prefix, std::vector<std::string>* keys,
                  std::vector<std::string>* values, std::string* err) {
  Page* raw = findLeaf(prefix, err);
  if (!raw) return false;
  BTreePage currNode(raw);

  for (;;) {
    uint16_t numCells = currNode.numCells();
    uint32_t offset = kBTreeHeaderSize;

    for (uint16_t i = 0; i < numCells; ++i) {
      uint16_t kLen = loadLE16(currNode.d() + offset);
      uint16_t vLen = loadLE16(currNode.d() + offset + 2);
      std::string key(reinterpret_cast<const char*>(currNode.d() + offset + 4), kLen);
      std::string val(reinterpret_cast<const char*>(currNode.d() + offset + 4 + kLen), vLen);

      if (hasPrefix(key, prefix)) {
        keys->push_back(key);
        values->push_back(val);
      }
      offset += 4 + kLen + vLen;
    }

    PageID nextId = currNode.nextLeafId();
    if (nextId == 0) break;

    currNode.page()->latch.unlock_shared();
    bp_->unpinPage(currNode.id(), false);

    Page* nextRaw = bp_->fetchPage(nextId, err);
    if (!nextRaw) return false;
    nextRaw->latch.lock_shared();
    currNode = BTreePage(nextRaw);
  }

  currNode.page()->latch.unlock_shared();
  bp_->unpinPage(currNode.id(), false);
  return true;
}

// ---------------------------------------------------------------------
// BTreeCursor
// ---------------------------------------------------------------------

std::unique_ptr<BTreeCursor> BTreeCursor::create(BTree* tree, std::string* err) {
  PageID currId = tree->rootId();
  for (;;) {
    Page* raw = tree->bp_->fetchPage(currId, err);
    if (!raw) return nullptr;
    raw->latch.lock_shared();
    BTreePage node(raw);

    if (node.pageType() == kPageTypeLeaf) {
      bool isEof = node.numCells() == 0;
      return std::unique_ptr<BTreeCursor>(
          new BTreeCursor(tree, raw, 0, kBTreeHeaderSize, isEof));
    }

    PageID childId = static_cast<PageID>(loadLE64(node.d() + kBTreeHeaderSize + 2));
    raw->latch.unlock_shared();
    tree->bp_->unpinPage(currId, false);
    currId = childId;
  }
}

bool BTreeCursor::next(std::string* key, std::string* value, bool* eof, std::string* err) {
  *eof = false;
  if (isEof_) {
    *eof = true;
    return false;
  }

  BTreePage node(node_);
  uint16_t kLen = loadLE16(node.d() + offset_);
  uint16_t vLen = loadLE16(node.d() + offset_ + 2);
  key->assign(reinterpret_cast<const char*>(node.d() + offset_ + 4), kLen);
  value->assign(reinterpret_cast<const char*>(node.d() + offset_ + 4 + kLen), vLen);

  cellIdx_++;
  offset_ += 4 + kLen + vLen;

  if (cellIdx_ >= node.numCells()) {
    PageID nextLeafId = node.nextLeafId();
    node_->latch.unlock_shared();
    tree_->bp_->unpinPage(node_->id, false);

    if (nextLeafId == 0) {
      isEof_ = true;
      node_ = nullptr;
    } else {
      Page* nextRaw = tree_->bp_->fetchPage(nextLeafId, err);
      if (!nextRaw) {
        // Matches ultimate_db.go: a fetch failure here discards the
        // already-extracted current-cell key/value, returning the error
        // instead (*eof stays false to distinguish this from clean EOF).
        node_ = nullptr;
        isEof_ = true;
        key->clear();
        value->clear();
        return false;
      }
      nextRaw->latch.lock_shared();
      node_ = nextRaw;
      cellIdx_ = 0;
      offset_ = kBTreeHeaderSize;
    }
  }
  return true;
}

void BTreeCursor::close() {
  if (node_ != nullptr) {
    node_->latch.unlock_shared();
    tree_->bp_->unpinPage(node_->id, false);
    node_ = nullptr;
    isEof_ = true;
  }
}

bool sortMergeJoin(BTree* leftTree, BTree* rightTree, std::vector<JoinResult>* out,
                    std::string* err) {
  auto leftCursor = BTreeCursor::create(leftTree, err);
  if (!leftCursor) return false;
  auto rightCursor = BTreeCursor::create(rightTree, err);
  if (!rightCursor) return false;

  std::string lKey, lVal, rKey, rVal;
  bool lEof = false, rEof = false;
  bool lOk = leftCursor->next(&lKey, &lVal, &lEof, err);
  bool rOk = rightCursor->next(&rKey, &rVal, &rEof, err);
  if (!lOk && !lEof) return false;
  if (!rOk && !rEof) return false;

  while (!lEof && !rEof) {
    int cmp = lKey.compare(rKey);
    if (cmp == 0) {
      out->push_back(JoinResult{lKey, lVal, rVal});
      lOk = leftCursor->next(&lKey, &lVal, &lEof, err);
      if (!lOk && !lEof) return false;
      rOk = rightCursor->next(&rKey, &rVal, &rEof, err);
      if (!rOk && !rEof) return false;
    } else if (cmp < 0) {
      lOk = leftCursor->next(&lKey, &lVal, &lEof, err);
      if (!lOk && !lEof) return false;
    } else {
      rOk = rightCursor->next(&rKey, &rVal, &rEof, err);
      if (!rOk && !rEof) return false;
    }
  }
  return true;
}

}  // namespace wkv
