// CUDA headers define __noinline__ which interferes with libstdc++'s use of
// `__attribute((__noinline__))`. In order to avoid compilation error,
// temporarily unset __noinline__ when we include affected libstdc++ header.

#pragma push_macro("__noinline__")
#undef __noinline__
#include_next "bits/shared_ptr_base.h"

#pragma pop_macro("__noinline__")
