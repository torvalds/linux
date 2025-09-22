void __init_cpu_features_resolver(unsigned long hwcap,
                                  const __ifunc_arg_t *arg) {
  if (__atomic_load_n(&__aarch64_cpu_features.features, __ATOMIC_RELAXED))
    return;

  __init_cpu_features_constructor(hwcap, arg);
}

void CONSTRUCTOR_ATTRIBUTE __init_cpu_features(void) {
  unsigned long hwcap = 0;
  unsigned long hwcap2 = 0;
  // CPU features already initialized.
  if (__atomic_load_n(&__aarch64_cpu_features.features, __ATOMIC_RELAXED))
    return;

  int res = 0;
  res = elf_aux_info(AT_HWCAP, &hwcap, sizeof hwcap);
  res |= elf_aux_info(AT_HWCAP2, &hwcap2, sizeof hwcap2);
  if (res)
    return;

  __ifunc_arg_t arg;
  arg._size = sizeof(__ifunc_arg_t);
  arg._hwcap = hwcap;
  arg._hwcap2 = hwcap2;
  __init_cpu_features_constructor(hwcap | _IFUNC_ARG_HWCAP, &arg);
}
