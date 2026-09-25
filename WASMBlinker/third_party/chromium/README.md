# Chromium reference sources

These files are **spec only**. They document the Blink layout and paint
subsystems that WASMBlinker reimplements, and are **not compiled** into
`blinker` (`CMakeLists.txt`'s `add_library(blinker ...)` never lists this
directory).

Same arrangement as [WASMProcess](../../../WASMProcess/third_party/chromium/README.md):
the upstream source is kept as the description of the behavior to match,
and everything under `../../src` is written against the CSS specifications
and that description rather than derived from the code.

## Layout

Blink's layout is a two-tree design: a mutable `LayoutObject` tree holds
style and dirty bits, and each layout pass produces an immutable tree of
physical fragments. Inline content is not laid out by walking the inline
box tree; it is flattened once into a linear item list and then filled
into line boxes by a separate line breaker.

| Concept | Chromium | Here |
|---|---|---|
| Inline content flattened to items | `InlineNode::CollectInlines`, `InlineItem` | `CollectInlineItems` / `InlineItem` (`include/blink/inline_layout.h`) |
| Line breaking / line box filling | `LineBreaker`, `InlineLayoutAlgorithm` | `LayoutInlineItems` (`src/inline_layout.cc`) |
| Positioned text output | `PhysicalTextFragment` | `InlineFragment` (`include/blink/layout.h`) |
| Box result of layout | `PhysicalBoxFragment` | `LayoutBox` (`include/blink/layout.h`) |
| Block flow | `BlockLayoutAlgorithm` | `LayoutBlockFormattingContext` (`src/layout.cc`) |
| Margin collapsing | `BlockMarginCollapsing` | `CollapseMargins` (`include/blink/block_layout.h`) |
| BFC establishment | `LayoutBlockFlow::CreatesNewFormattingContext` | `EstablishesBlockFormattingContext` (`src/block_layout.cc`) |
| Overflow clip | `OverflowClipPainter`, `PaintLayerClipper` | `PushOverflowClip` / `kPushClipRect` (`src/display_list.cc`) |
| Sticky positioning | `StickyPositionScrollingNode` | `ResolveStickyPositions` (`src/position_layout.cc`) |
| Float avoidance | `ExclusionSpace` | `FloatState` (`src/layout.cc`) |
| Intrinsic sizes | `MinMaxSizes` | `InlineMaxContentWidth`, `FlexItemBaseWidth` |
| Font face/metrics | `Font`, `SimpleFontData`, `FontMetrics` | `FontSet`, `FontMetrics` (`include/blink/font_set.h`) |

The item list is the load-bearing part. An inline collector that joins
descendant text into one string cannot represent `a <b>bold</b> word`,
because the run's own computed style and its DOM node are gone by the time
the line is built -- so `<b>`, `<a href>` and nested `<span>` can be
neither painted nor hit-tested as themselves. Items keep both.

Upstream:

- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/inline/inline_node.cc
- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/inline/inline_item.h
- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/inline/line_breaker.cc
- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/block_layout_algorithm.cc

## Paint

Blink does not draw straight from the layout tree. It walks the fragment
tree once per *paint phase*, grouped by *stacking context*, and records
into a display item list; rasterization is a separate replay of that list.
The phase split is what implements CSS 2.1 Appendix E: every block
background in a stacking context is painted before any float in it, and
every float before any inline text.

| Concept | Chromium | Here |
|---|---|---|
| Recorded drawing operation | `DisplayItem`, `DrawingDisplayItem` | `DisplayItem` (`include/blink/display_list.h`) |
| The recorded list | `PaintArtifact`, `DisplayItemList` | `DisplayList` |
| Paint phase walk | `PaintPhase`, `BoxFragmentPainter::PaintObject` | `PaintPhase`, `PaintSubtree` (`src/display_list.cc`) |
| Stacking context grouping | `PaintLayer`, `PaintLayerPainter` | `IsStackingContext`, `PaintStackingContext` |
| Text painting | `TextFragmentPainter` | `PaintInlineFragments` |
| Box decorations | `BoxDecorationData`, `BoxPainterBase` | `AppendBoxDecorations` |
| Replay to a surface | `cc::DisplayItemList` raster | `RasterizeDisplayList` (`src/paint.cc`) |
| Overflow clip replay | `ScopedPaintChunkDisplayItem` clip ops | `kPushClipRect` / `kPopClip` in `RasterizeDisplayList` |

Appendix E order, as implemented in `PaintStackingContext`:

1. the stacking context's own background and borders
2. child stacking contexts with negative `z-index`, most negative first
3. in-flow non-positioned descendants' backgrounds and borders
4. non-positioned floats
5. inline content (text fragments, list markers, replaced content)
6. positioned descendants with `z-index: auto` or `0`
7. child stacking contexts with positive `z-index`, least first

The predecessor painter recursed the box tree and sorted only immediate
siblings by `z-index`, which is not this order: a positioned element could
be painted under an unrelated later sibling's background, and inline text
of an ancestor could be covered by a descendant block background.

Upstream:

- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/paint/paint_layer_painter.cc
- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/paint/box_fragment_painter.cc
- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/paint/paint_phase.h
- https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/platform/graphics/paint/display_item.h

## Roadmap (clean-room parity)

Subsystem checklist. "Done" means spec-faithful enough for golden tests;
"Partial" means parsed or stubbed; "Todo" is not started.

| Area | Status | Notes |
|---|---|---|
| Cascade / specificity | Done | `css.cc` |
| Block / inline / float BFC | Done | `layout.cc`, `inline_layout.cc` |
| Inline formatting context | Done | item list + line breaker |
| Margin collapsing (siblings) | Done | `CollapseMargins` in BFC |
| Margin collapsing (parent/child) | Done | first/last child with parent |
| Absolute positioning | Done | deferred `ResolveAbsolutes` |
| Fixed / sticky | Partial | fixed uses viewport scroll; sticky top/bottom vs viewport scroll |
| Flexbox (single line) | Done | grow/shrink/justify/align |
| Flex wrap | Done | `flex-wrap: wrap` line breaking |
| CSS Grid tracks | Partial | `px` + `fr` + `minmax()` minimum clamp |
| Tables | Partial | colspan + rowspan |
| Paint phases / stacking | Done | Appendix E in `display_list.cc` |
| Overflow clip | Done | padding-edge rect clip |
| Box shadow | Done | outer + inset (rect approximation) |
| CSS transforms | Partial | paint via decomposed matrix |
| Scroll / scrollbars | Partial | viewport + per-element scroll via `element_scroll`, extent clamping |
| `visibility` | Done | skip own paint, inherit layout |
| Generic font families | Done | sans/serif/mono via `FontSet` |
| Named font preference | Partial | Georgia, Consolas, Times, Segoe UI |
| System font resolution | Partial | stem-based probe under `C:/Windows/Fonts/` for unknown families |
| Transforms / filters / shadows | Partial | transform + outer box-shadow painted |
| Bidi / RTL | Partial | `dir=rtl` + UAX #9 L1 visual reorder for mixed LTR/RTL runs |
| Generated content | Todo | `::before` / `::after` |
| Hit testing | Partial | box + inline fragments |

CSS specifications the implementation is written against, rather than the
sources above:

- CSS 2.1 section 8.3 (margin collapsing), section 9 (visual formatting), section 10 (box dimensions), section 11 (overflow), Appendix E (paint order)
- CSS Display 3, CSS Inline 3, CSS Text 3 (white space, line breaking)
- CSS Backgrounds and Borders 3, CSS Color 4

Copyright The Chromium Authors for any file copied here. BSD-style license
as in the Chromium tree. New code in this repository is BSD-3-Clause.
