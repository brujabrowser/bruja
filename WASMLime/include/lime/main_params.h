#ifndef LIME_MAIN_PARAMS_H_
#define LIME_MAIN_PARAMS_H_

namespace lime {

class MainDelegate;

// Analog of content::ContentMainParams.
struct MainParams {
  explicit MainParams(MainDelegate* delegate) : delegate(delegate) {}

  MainDelegate* delegate;
};

}  // namespace lime

#endif  // LIME_MAIN_PARAMS_H_
