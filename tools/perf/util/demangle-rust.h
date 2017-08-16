#ifndef __PERF_DEMANGLE_RUST
#define __PERF_DEMANGLE_RUST 1

bool rust_is_mangled(const char *str);
void rust_demangle_sym(char *str);

#endif /* __PERF_DEMANGLE_RUST */
