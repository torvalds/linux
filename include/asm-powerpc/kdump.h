#ifndef _PPC64_KDUMP_H
#define _PPC64_KDUMP_H

/* Kdump kernel runs at 32 MB, change at your peril. */
#define KDUMP_KERNELBASE	0x2000000

/* How many bytes to reserve at zero for kdump. The reserve limit should
 * be greater or equal to the trampoline's end address.
 * Reserve to the end of the FWNMI area, see head_64.S */
#define KDUMP_RESERVE_LIMIT	0x10000 /* 64K */

#ifdef CONFIG_CRASH_DUMP

#define PHYSICAL_START	KDUMP_KERNELBASE
#define KDUMP_TRAMPOLINE_START	0x0100
#define KDUMP_TRAMPOLINE_END	0x3000

#define KDUMP_MIN_TCE_ENTRIES	2048

#else /* !CONFIG_CRASH_DUMP */

#define PHYSICAL_START	0x0

#endif /* CONFIG_CRASH_DUMP */

#ifndef __ASSEMBLY__
#ifdef CONFIG_CRASH_DUMP

extern void reserve_kdump_trampoline(void);
extern void setup_kdump_trampoline(void);

#else /* !CONFIG_CRASH_DUMP */

static inline void reserve_kdump_trampoline(void) { ; }
static inline void setup_kdump_trampoline(void) { ; }

#endif /* CONFIG_CRASH_DUMP */
#endif /* __ASSEMBLY__ */

#endif /* __PPC64_KDUMP_H */
