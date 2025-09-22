// NOLINT: llvm-header-guard
// __register_frame() is used with dynamically generated code to register the
// FDE for a generated (JIT) code. This header provides protypes, since the gcc
// version of unwind.h may not, so CMake can check if the corresponding symbols
// exist in the runtime.
extern void __register_frame(const void *fde);   // NOLINT
extern void __deregister_frame(const void *fde); // NOLINT
extern void __unw_add_dynamic_fde();             // NOLINT
