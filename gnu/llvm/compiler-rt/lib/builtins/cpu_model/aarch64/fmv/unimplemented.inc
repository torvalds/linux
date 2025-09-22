// On platforms that have not implemented this yet, we provide an implementation
// that does not claim support for any features by leaving
// __aarch64_cpu_features.features initialized to 0.

void __init_cpu_features_resolver(unsigned long hwcap,
                                  const __ifunc_arg_t *arg) {}

void __init_cpu_features(void) {}
