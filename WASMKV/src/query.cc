#include "wkv/query.h"

#include <sstream>

#include "wkv/index.h"

namespace wkv {

std::vector<uint64_t> intersect(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
  std::vector<uint64_t> res;
  res.reserve(a.size() < b.size() ? a.size() : b.size());
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) {
      ++i;
    } else if (a[i] > b[j]) {
      ++j;
    } else {
      res.push_back(a[i]);
      ++i;
      ++j;
    }
  }
  return res;
}

std::vector<uint64_t> setUnion(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
  std::vector<uint64_t> res;
  res.reserve(a.size() + b.size());
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) {
      res.push_back(a[i++]);
    } else if (a[i] > b[j]) {
      res.push_back(b[j++]);
    } else {
      res.push_back(a[i]);
      ++i;
      ++j;
    }
  }
  res.insert(res.end(), a.begin() + static_cast<long>(i), a.end());
  res.insert(res.end(), b.begin() + static_cast<long>(j), b.end());
  return res;
}

std::vector<uint64_t> difference(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
  std::vector<uint64_t> res;
  res.reserve(a.size());
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] < b[j]) {
      res.push_back(a[i++]);
    } else if (a[i] > b[j]) {
      ++j;
    } else {
      ++i;
      ++j;
    }
  }
  res.insert(res.end(), a.begin() + static_cast<long>(i), a.end());
  return res;
}

std::vector<uint64_t> TermQuery::execute(SegmentSearcher* s) const { return s->fetchPostings(term_); }

namespace {

class Parser {
 public:
  explicit Parser(std::vector<std::string> tokens) : tokens_(std::move(tokens)) {}

  std::unique_ptr<Query> parseExpression(std::string* err) {
    std::unique_ptr<Query> left = parseTerm(err);
    if (!left) return nullptr;
    while (current() == "OR") {
      consume();
      std::unique_ptr<Query> right = parseTerm(err);
      if (!right) return nullptr;
      left = std::make_unique<OrQuery>(std::move(left), std::move(right));
    }
    return left;
  }

 private:
  std::string current() const { return pos_ < tokens_.size() ? tokens_[pos_] : std::string(); }
  std::string consume() {
    std::string t = current();
    ++pos_;
    return t;
  }

  std::unique_ptr<Query> parseTerm(std::string* err) {
    std::unique_ptr<Query> left = parseFactor(err);
    if (!left) return nullptr;
    while (current() == "AND" || current() == "NOT") {
      std::string op = consume();
      std::unique_ptr<Query> right = parseFactor(err);
      if (!right) return nullptr;
      if (op == "AND") {
        left = std::make_unique<AndQuery>(std::move(left), std::move(right));
      } else {
        left = std::make_unique<NotQuery>(std::move(left), std::move(right));
      }
    }
    return left;
  }

  std::unique_ptr<Query> parseFactor(std::string* err) {
    std::string token = current();

    if (token == "(") {
      consume();
      std::unique_ptr<Query> expr = parseExpression(err);
      if (!expr) return nullptr;
      if (current() != ")") {
        if (err) *err = "missing closing parenthesis";
        return nullptr;
      }
      consume();
      return expr;
    }

    if (token.empty() || token == ")" || token == "AND" || token == "OR" || token == "NOT") {
      if (err) *err = "unexpected token: " + token;
      return nullptr;
    }

    consume();
    std::vector<std::string> cleaned = tokenize(token);
    if (cleaned.empty()) return std::make_unique<TermQuery>("");
    return std::make_unique<TermQuery>(cleaned[0]);
  }

  std::vector<std::string> tokens_;
  size_t pos_ = 0;
};

}  // namespace

std::unique_ptr<Query> parseQuery(const std::string& input, std::string* err) {
  std::string spaced;
  spaced.reserve(input.size());
  for (char c : input) {
    if (c == '(') {
      spaced += " ( ";
    } else if (c == ')') {
      spaced += " ) ";
    } else {
      spaced += c;
    }
  }

  std::istringstream iss(spaced);
  std::vector<std::string> tokens;
  std::string tok;
  while (iss >> tok) tokens.push_back(tok);

  Parser parser(std::move(tokens));
  return parser.parseExpression(err);
}

}  // namespace wkv
