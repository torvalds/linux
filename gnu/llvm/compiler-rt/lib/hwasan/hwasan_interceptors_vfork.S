#include "sanitizer_common/sanitizer_asm.h"
#include "builtins/assembly.h"

#if defined(__linux__) && HWASAN_WITH_INTERCEPTORS
#define COMMON_INTERCEPTOR_SPILL_AREA __hwasan_extra_spill_area
#define COMMON_INTERCEPTOR_HANDLE_VFORK __hwasan_handle_vfork
#include "sanitizer_common/sanitizer_common_interceptors_vfork_aarch64.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_riscv64.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_x86_64.inc.S"
#endif

NO_EXEC_STACK_DIRECTIVE

GNU_PROPERTY_BTI_PAC
