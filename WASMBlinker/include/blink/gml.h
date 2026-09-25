#ifndef BLINK_GML_H_
#define BLINK_GML_H_

#include "blink/dom.h"

#include <string>

namespace blink {

// GuiKit GML is Blinker's authoring front-end for chrome/about documents.
// HTTP still uses ParseHtml. When built with sibling guikit/cpp
// (BLINK_HAS_GUIKIT), this lowers GML (rule()/{{ templates }}) and
// commits a blink::HtmlDocument. Without GuiKit it returns an empty
// document and sets *error.
HtmlDocument ParseGml(const std::string& gml, const std::string& data_json,
                      std::string* error = nullptr);

}  // namespace blink

#endif  // BLINK_GML_H_
