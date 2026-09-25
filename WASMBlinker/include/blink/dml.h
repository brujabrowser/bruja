#ifndef BLINK_DML_H_
#define BLINK_DML_H_

#include "blink/dom.h"

#include <string>

namespace blink {

// GuiKit GS returns DML (setValue/append/addClass/…). LoadGML compiles the
// GML tree; the viewer applies this DML afterward — same contract as NOTES
// for sandbox lists (home tiles, dashboard guests).
void ApplyHtmlDml(HtmlDocument* doc, const std::string& dml_json);

// Pull reserved "__dml" out of LoadGML data_json so templates never see it.
// Returns template data; writes the raw JSON array into *dml_json when present.
std::string SplitLoadGmlData(const std::string& data_json, std::string* dml_json);

}  // namespace blink

#endif  // BLINK_DML_H_
