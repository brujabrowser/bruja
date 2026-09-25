#ifndef BLINK_FONT_SET_H_
#define BLINK_FONT_SET_H_

#include "blink/css.h"

#ifdef BLINK_HAS_PAINT_PIPELINE
#include "wasmskia/font.h"
#else
namespace wasmskia {
class Font;
}
#endif

namespace blink {

#ifdef BLINK_HAS_PAINT_PIPELINE
// Loads a system font file for an arbitrary family name when it is not one
// of the wired aliases in ResolveNamed. Implemented in paint.cc.
const wasmskia::Font* DynamicFontFamily(const std::string& family_lower);
#endif

// Vertical metrics for one font size. When a face is available,
// FontMetrics::ForFace reads hhea ascent/descent; ForSize is the Latin
// 0.8/0.2 fallback used when layout has no font (tests without WASMSkia).
struct FontMetrics {
  float ascent = 0;   // baseline to top of the em box, positive upward
  float descent = 0;  // baseline to bottom, positive downward
  float height() const { return ascent + descent; }

  static FontMetrics ForSize(float size_px) {
    FontMetrics m;
    m.ascent = size_px * 0.8f;
    m.descent = size_px * 0.2f;
    return m;
  }

#ifdef BLINK_HAS_PAINT_PIPELINE
  // Prefer the face's own hhea metrics when a Font is in hand. Falls back
  // to ForSize when the pointer is null or the face has no usable ascent.
  static FontMetrics ForFace(const wasmskia::Font* face, float size_px) {
    if (!face || !face->valid() || size_px <= 0.0f) return ForSize(size_px);
    FontMetrics m;
    m.ascent = face->Ascent(size_px);
    m.descent = face->Descent(size_px);
    if (m.ascent <= 0.0f || m.descent < 0.0f) return ForSize(size_px);
    return m;
  }
#endif
};

// The faces CSS `font-family` x `font-weight` x `font-style` can select
// between. Layout and paint must resolve a face the same way or measured
// widths will not match painted glyphs, so both go through Resolve().
struct FontSet {
  // Sans-serif: the initial family, and the fallback for the others.
  const wasmskia::Font* regular = nullptr;
  const wasmskia::Font* bold = nullptr;
  const wasmskia::Font* italic = nullptr;
  const wasmskia::Font* bold_italic = nullptr;
  // Monospace. Its upright face matters much more than its italic: what
  // the family is for is `<pre>` and `<code>` keeping their columns.
  const wasmskia::Font* mono = nullptr;
  const wasmskia::Font* mono_bold = nullptr;
  const wasmskia::Font* mono_italic = nullptr;
  const wasmskia::Font* serif = nullptr;
  const wasmskia::Font* serif_bold = nullptr;
  const wasmskia::Font* serif_italic = nullptr;
  // Named faces distinct from the generic buckets above.
  const wasmskia::Font* georgia = nullptr;
  const wasmskia::Font* consolas = nullptr;

  const wasmskia::Font* ResolveNamed(const std::string& preferred, bool want_bold,
                                     bool want_italic) const {
    if (preferred.empty()) return nullptr;
    if (preferred == "georgia") return georgia ? georgia : serif;
    if (preferred == "consolas" || preferred == "courier new" || preferred == "courier") {
      if (want_bold && mono_bold) return mono_bold;
      if (want_italic && mono_italic) return mono_italic;
      return consolas ? consolas : mono;
    }
    if (preferred == "times" || preferred == "times new roman") {
      if (want_bold && serif_bold) return serif_bold;
      if (want_italic && serif_italic) return serif_italic;
      return serif;
    }
    if (preferred == "segoe ui" || preferred == "arial" || preferred == "helvetica") {
      if (want_bold && bold) return bold;
      if (want_italic && italic) return italic;
      return regular;
    }
    return nullptr;
  }

  // Nearest available face: within the requested family first, then
  // sans-serif. A missing bold face renders un-bolded rather than not at
  // all -- synthetic emboldening would need a stroke pass
  // wasmskia::DrawText does not offer.
  const wasmskia::Font* Resolve(GenericFontFamily family, bool want_bold,
                                bool want_italic) const {
    const wasmskia::Font* upright = nullptr;
    const wasmskia::Font* want = nullptr;
    switch (family) {
      case GenericFontFamily::kMonospace:
        upright = mono;
        want = want_bold ? mono_bold : (want_italic ? mono_italic : mono);
        break;
      case GenericFontFamily::kSerif:
        upright = serif;
        want = want_bold ? serif_bold : (want_italic ? serif_italic : serif);
        break;
      case GenericFontFamily::kSansSerif:
        break;
    }
    if (want) return want;
    if (upright) return upright;  // family present but not this weight/style
    // Family entirely absent: sans-serif rather than nothing at all.
    if (want_bold && want_italic && bold_italic) return bold_italic;
    if (want_bold && want_italic) return bold ? bold : (italic ? italic : regular);
    if (want_bold) return bold ? bold : regular;
    if (want_italic) return italic ? italic : regular;
    return regular;
  }

  const wasmskia::Font* Resolve(bool want_bold, bool want_italic) const {
    return Resolve(GenericFontFamily::kSansSerif, want_bold, want_italic);
  }

  const wasmskia::Font* Resolve(const ComputedStyle& style) const {
    if (const wasmskia::Font* named =
            ResolveNamed(style.font_family_preferred, style.font_bold, style.font_italic)) {
      return named;
    }
#ifdef BLINK_HAS_PAINT_PIPELINE
    if (!style.font_family_preferred.empty()) {
      if (const wasmskia::Font* dyn = DynamicFontFamily(style.font_family_preferred)) {
        return dyn;
      }
    }
#endif
    return Resolve(style.font_family, style.font_bold, style.font_italic);
  }

  bool valid() const { return regular != nullptr; }
};

}  // namespace blink

#endif  // BLINK_FONT_SET_H_
