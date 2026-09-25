#ifndef LIME_MAIN_H_
#define LIME_MAIN_H_

#include "lime/main_params.h"

namespace lime {

// Analog of content::ContentMain() -- the whole reason this repo exists.
// Drives CommandLine -> MainDelegate lifecycle -> whp::Executor run loop,
// so a wpr_*-style consumer's main() can shrink to what
// fuchsia_web/webengine/web_engine_main.cc's own main() already is: a
// couple of switch checks and one call down into here.
int Main(MainParams params);

}  // namespace lime

#endif  // LIME_MAIN_H_
