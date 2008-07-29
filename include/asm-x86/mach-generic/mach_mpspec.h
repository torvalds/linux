#ifndef __ASM_MACH_MPSPEC_H
#define __ASM_MACH_MPSPEC_H

#define MAX_IRQ_SOURCES 256

/* Summit or generic (i.e. installer) kernels need lots of bus entries. */
/* Maximum 256 PCI busses, plus 1 ISA bus in each of 4 cabinets. */
#define MAX_MP_BUSSES 260

extern void numaq_mps_oem_check(struct mp_config_table *mpc, char *oem,
				char *productid);
#endif /* __ASM_MACH_MPSPEC_H */
