#ifndef ASM_X86__MMCONFIG_H
#define ASM_X86__MMCONFIG_H

#ifdef CONFIG_PCI_MMCONFIG
extern void __cpuinit fam10h_check_enable_mmcfg(void);
extern void __cpuinit check_enable_amd_mmconf_dmi(void);
#else
static inline void fam10h_check_enable_mmcfg(void) { }
static inline void check_enable_amd_mmconf_dmi(void) { }
#endif

#endif /* ASM_X86__MMCONFIG_H */
