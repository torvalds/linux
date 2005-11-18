#ifndef ASMARM_ARCH_SMP_H
#define ASMARM_ARCH_SMP_H

#include <linux/config.h>

#include <asm/hardware.h>
#include <asm/io.h>

#define hard_smp_processor_id()				\
	({						\
		unsigned int cpunum;			\
		__asm__("mrc p15, 0, %0, c0, c0, 5"	\
			: "=r" (cpunum));		\
		cpunum &= 0x0F;				\
	})

extern void secondary_scan_irqs(void);

#endif
