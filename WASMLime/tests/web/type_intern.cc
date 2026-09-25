// Generated Lime Frame/NavigationController types intern through Bindings
// CHPT (WASMv8Bindings) with intern records in the WASMSafeSpace cage.
#include "test.h"

#include "frame_gen.h"
#include "navigation_gen.h"

#include <string>

TEST(lime_frame_type_key_differs_from_navigation_controller) {
  EXPECT_EQ(std::string(lime::Frame::intern_key()),
            std::string("interface:lime.Frame"));
  EXPECT_EQ(std::string(lime::NavigationController::intern_key()),
            std::string("interface:lime.NavigationController"));
  EXPECT(lime::Frame::type_key() != lime::NavigationController::type_key());
  EXPECT(lime::Frame::chpt_tag() != lime::NavigationController::chpt_tag());
  const mojo::internal::InternedTypeRec* rec =
      mojo::internal::InternedType(lime::Frame::type_key());
  EXPECT(rec != nullptr);
  EXPECT(rec->type_key == lime::Frame::type_key());
  EXPECT(mojo::internal::TypeCage().Contains(rec));
}
