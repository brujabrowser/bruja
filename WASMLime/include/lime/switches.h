#ifndef LIME_SWITCHES_H_
#define LIME_SWITCHES_H_

namespace lime {
namespace switches {

// Mirrors fuchsia_web/webengine/switches.h's kContextProvider -- the switch
// web_engine_main.cc checks before deciding between ContextProviderMain()
// and the WebEngineMainDelegate/content::ContentMain() path.
inline constexpr char kContextProvider[] = "context-provider";

// Mirrors content_switches.h's kProcessType, narrowed to this stack's only
// existing two-role split (wpr_cadmium's --offerer flag vs. its absence).
// MainDelegate::RunProcess() receives this switch's value.
inline constexpr char kRole[] = "role";

}  // namespace switches
}  // namespace lime

#endif  // LIME_SWITCHES_H_
