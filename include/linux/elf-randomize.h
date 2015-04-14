#ifndef _ELF_RANDOMIZE_H
#define _ELF_RANDOMIZE_H

#ifndef CONFIG_ARCH_HAS_ELF_RANDOMIZE
static inline unsigned long arch_mmap_rnd(void) { return 0; }
#else
extern unsigned long arch_mmap_rnd(void);
#endif

#endif
