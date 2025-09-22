#include "sanitizer_common/sanitizer_common.h"
#include "gtest/gtest.h"

TEST(ModuleUUID, kModuleUUIDSize) {
#if SANITIZER_APPLE
    EXPECT_EQ(__sanitizer::kModuleUUIDSize, 16ULL);
#else
    EXPECT_EQ(__sanitizer::kModuleUUIDSize, 32ULL);
#endif
}
