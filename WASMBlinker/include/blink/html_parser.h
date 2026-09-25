#ifndef BLINK_HTML_PARSER_H_
#define BLINK_HTML_PARSER_H_

#include "blink/dom.h"

#include <string>

namespace blink {

// A minimal, hand-rolled HTML tokenizer/tree-builder -- not HTML5-spec-
// complete (see loki-closure's htmlparse.go for the fuller feature set
// this is a scoped-down native replacement for; no quirks-mode detection,
// no foster-parenting, no auto-close-tag heuristics beyond mismatched end
// tags falling back to a stack search). Enough to build a real DOM tree
// out of ordinary, reasonably well-formed HTML: tags, quoted/unquoted/
// boolean attributes, void elements, <script>/<style> raw text, comments,
// <!DOCTYPE>, and a small named-entity set (amp lt gt quot apos nbsp).
HtmlDocument ParseHtml(const std::string& source);

}  // namespace blink

#endif  // BLINK_HTML_PARSER_H_
