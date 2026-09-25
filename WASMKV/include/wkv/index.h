#pragma once
// Port of ultimate_db.go section 9 (inverted index + segment search).

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace wkv {

// Lowercases and splits on runs of non-alphanumeric characters.
std::vector<std::string> tokenize(const std::string& text);

class MemIndex {
 public:
  void add(uint64_t docId, const std::string& text);

  // Writes the segment file (sorted postings + trailing dictionary) to
  // `path`. Returns false and sets *err on I/O failure.
  bool writeSegment(const std::string& path, std::string* err) const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::vector<uint64_t>> postings_;
  uint64_t docsCount_ = 0;
};

// Reads a segment written by MemIndex::writeSegment and answers postings /
// boolean queries against it. `data` must outlive the searcher.
class SegmentSearcher {
 public:
  explicit SegmentSearcher(std::vector<uint8_t> data) : data_(std::move(data)) {}

  std::vector<uint64_t> fetchPostings(const std::string& term);

  // Parses and executes a boolean query (see query.h). Returns false and
  // sets *err on a parse error.
  bool search(const std::string& queryString, std::vector<uint64_t>* out, std::string* err);

  const std::vector<uint8_t>& data() const { return data_; }

 private:
  void initDict();

  std::vector<uint8_t> data_;
  std::unordered_map<std::string, uint64_t> dict_;
  std::once_flag dictOnce_;
};

}  // namespace wkv
