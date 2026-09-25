#pragma once
// Port of ultimate_db.go sections 10-11 (boolean query AST + parser).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wkv {

class SegmentSearcher;

// Assumes both inputs are sorted ascending (true of postings lists written
// by MemIndex::writeSegment).
std::vector<uint64_t> intersect(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b);
std::vector<uint64_t> setUnion(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b);
std::vector<uint64_t> difference(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b);

class Query {
 public:
  virtual ~Query() = default;
  virtual std::vector<uint64_t> execute(SegmentSearcher* s) const = 0;
};

class TermQuery : public Query {
 public:
  explicit TermQuery(std::string term) : term_(std::move(term)) {}
  std::vector<uint64_t> execute(SegmentSearcher* s) const override;

 private:
  std::string term_;
};

class AndQuery : public Query {
 public:
  AndQuery(std::unique_ptr<Query> left, std::unique_ptr<Query> right)
      : left_(std::move(left)), right_(std::move(right)) {}
  std::vector<uint64_t> execute(SegmentSearcher* s) const override {
    return intersect(left_->execute(s), right_->execute(s));
  }

 private:
  std::unique_ptr<Query> left_, right_;
};

class OrQuery : public Query {
 public:
  OrQuery(std::unique_ptr<Query> left, std::unique_ptr<Query> right)
      : left_(std::move(left)), right_(std::move(right)) {}
  std::vector<uint64_t> execute(SegmentSearcher* s) const override {
    return setUnion(left_->execute(s), right_->execute(s));
  }

 private:
  std::unique_ptr<Query> left_, right_;
};

// A NOT B, i.e. set difference (matches ultimate_db.go: NOT is a binary
// "AND NOT" operator at the same precedence tier as AND, not a unary prefix).
class NotQuery : public Query {
 public:
  NotQuery(std::unique_ptr<Query> left, std::unique_ptr<Query> right)
      : left_(std::move(left)), right_(std::move(right)) {}
  std::vector<uint64_t> execute(SegmentSearcher* s) const override {
    return difference(left_->execute(s), right_->execute(s));
  }

 private:
  std::unique_ptr<Query> left_, right_;
};

// Parses a query string like "(a OR b) AND c NOT d". Returns nullptr and
// sets *err on a syntax error.
std::unique_ptr<Query> parseQuery(const std::string& input, std::string* err);

}  // namespace wkv
