static void CONSTRUCTOR_ATTRIBUTE init_have_lse_atomics(void) {
  unsigned long hwcap;
  int result = elf_aux_info(AT_HWCAP, &hwcap, sizeof hwcap);
  __aarch64_have_lse_atomics = result == 0 && (hwcap & HWCAP_ATOMICS) != 0;
}
