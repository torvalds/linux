void __init_cpu_features_resolver(unsigned long hwcap,
                                  const __ifunc_arg_t *arg) {
  if (__atomic_load_n(&__aarch64_cpu_features.features, __ATOMIC_RELAXED))
    return;

  // ifunc resolvers don't have hwcaps in arguments on Android API lower
  // than 30. If so, set feature detection done and keep all CPU features
  // unsupported (zeros). To detect this case in runtime we check existence
  // of memfd_create function from Standard C library which was introduced in
  // Android API 30.
  int memfd_create(const char *, unsigned int) __attribute__((weak));
  if (!memfd_create)
    return;

  __init_cpu_features_constructor(hwcap, arg);
}

void CONSTRUCTOR_ATTRIBUTE __init_cpu_features(void) {
  // CPU features already initialized.
  if (__atomic_load_n(&__aarch64_cpu_features.features, __ATOMIC_RELAXED))
    return;

  // Don't set any CPU features,
  // detection could be wrong on Exynos 9810.
  if (__isExynos9810())
    return;

  unsigned long hwcap = getauxval(AT_HWCAP);
  unsigned long hwcap2 = getauxval(AT_HWCAP2);

  __ifunc_arg_t arg;
  arg._size = sizeof(__ifunc_arg_t);
  arg._hwcap = hwcap;
  arg._hwcap2 = hwcap2;
  __init_cpu_features_constructor(hwcap | _IFUNC_ARG_HWCAP, &arg);
}
