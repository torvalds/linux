#ifndef _PPC64_KDUMP_H
#define _PPC64_KDUMP_H

/* How many bytes to reserve at zero for kdump. The reserve limit should
 * be greater or equal to the trampoline's end address. */
#define KDUMP_RESERVE_LIMIT	0x8000

#define KDUMP_TRAMPOLINE_START	0x0100
#define KDUMP_TRAMPOLINE_END	0x3000

extern void kdump_setup(void);

#endif /* __PPC64_KDUMP_H */
