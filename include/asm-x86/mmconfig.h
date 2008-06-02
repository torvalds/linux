#ifndef _ASM_MMCONFIG_H
#define _ASM_MMCONFIG_H

#ifdef CONFIG_PCI_MMCONFIG
extern void __cpuinit fam10h_check_enable_mmcfg(void);
extern void __init check_enable_amd_mmconf_dmi(void);
#else
static inline void fam10h_check_enable_mmcfg(void) { }
static inline void check_enable_amd_mmconf_dmi(void) { }
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_X86_64)
extern void __cpuinit amd_enable_pci_ext_cfg(struct cpuinfo_x86 *c);
#else
static inline void amd_enable_pci_ext_cfg(struct cpuinfo_x86 *c) { }
#endif

#endif
