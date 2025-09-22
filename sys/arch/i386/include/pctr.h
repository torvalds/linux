/*	$OpenBSD: pctr.h,v 1.17 2014/03/29 18:09:29 guenther Exp $	*/

/*
 * Pentium performance counter driver for OpenBSD.
 * Copyright 1996 David Mazieres <dm@lcs.mit.edu>.
 *
 * Modification and redistribution in source and binary forms is
 * permitted provided that due credit is given to the author and the
 * OpenBSD project by leaving this copyright notice intact.
 */

#ifndef _MACHINE_PCTR_H_
#define _MACHINE_PCTR_H_

#include <sys/ioccom.h>

typedef u_int64_t	pctrval;

#define PCTR_NUM	4

struct pctrst {
	u_int pctr_fn[PCTR_NUM];	/* Current settings of counters */
	pctrval pctr_tsc;		/* Free-running 64-bit cycle counter */
	pctrval pctr_hwc[PCTR_NUM];	/* Values of the hardware counters */
};

/* Bit values in fn fields and PIOCS ioctl's */
#define P5CTR_K		0x40		/* Monitor kernel-level events */
#define P5CTR_U		0x80		/* Monitor user-level events */
#define P5CTR_C		0x100		/* count cycles rather than events */

#define PCTR_U		0x010000	/* Monitor user-level events */
#define PCTR_K		0x020000	/* Monitor kernel-level events */
#define PCTR_E		0x040000	/* Edge detect */
#define PCTR_EN		0x400000	/* Enable counters (counter 0 only) */
#define PCTR_I		0x800000	/* Invert counter mask */

/* Unit Mask bits */
#define PCTR_UM_M	0x0800		/* Modified cache lines */
#define PCTR_UM_E	0x0400		/* Exclusive cache lines */
#define PCTR_UM_S	0x0200		/* Shared cache lines */
#define PCTR_UM_I	0x0100		/* Invalid cache lines */
#define PCTR_UM_MESI	(PCTR_UM_M|PCTR_UM_E|PCTR_UM_S|PCTR_UM_I)
#define PCTR_UM_A	0x2000		/* Any initiator */

#define PCTR_UM_SHIFT	8		/* Left shift for unit mask */
#define PCTR_CM_SHIFT	24		/* Left shift for counter mask */

/* ioctl to set which counter a device tracks */
#define PCIOCRD _IOR('c', 1, struct pctrst)	/* Read counter value */
#define PCIOCS0 _IOW('c', 8, unsigned int)	/* Set counter 0 function */
#define PCIOCS1 _IOW('c', 9, unsigned int)	/* Set counter 1 function */
#define PCIOCS2 _IOW('c', 10, unsigned int)	/* Set counter 0 function */
#define PCIOCS3 _IOW('c', 11,  unsigned int)	/* Set counter 1 function */

#define _PATH_PCTR "/dev/pctr"

#define rdtsc()							\
({								\
	pctrval v;						\
	__asm volatile ("rdtsc" : "=A" (v));			\
	v;							\
})

/* Read the performance counters (Pentium Pro only) */
#define rdpmc(ctr)						\
({								\
	pctrval v;						\
	__asm volatile ("rdpmc\n"				\
	    "\tandl $0xff, %%edx"				\
	    : "=A" (v) : "c" (ctr));				\
	v;							\
})

#ifdef _KERNEL

#define rdmsr(msr)						\
({								\
	pctrval v;						\
	__asm volatile ("rdmsr" : "=A" (v) : "c" (msr));	\
	v;							\
})

#define wrmsr(msr, v) \
	__asm volatile ("wrmsr" :: "A" ((u_int64_t) (v)), "c" (msr));

void	pctrattach(int);
int	pctropen(dev_t, int, int, struct proc *);
int	pctrclose(dev_t, int, int, struct proc *);
int	pctrioctl(dev_t, u_long, caddr_t, int, struct proc *);

#endif /* _KERNEL */
#endif /* ! _MACHINE_PCTR_H_ */
