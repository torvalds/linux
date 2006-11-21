#ifndef _ASM_X86_64_VSYSCALL_H_
#define _ASM_X86_64_VSYSCALL_H_

enum vsyscall_num {
	__NR_vgettimeofday,
	__NR_vtime,
	__NR_vgetcpu,
};

#define VSYSCALL_START (-10UL << 20)
#define VSYSCALL_SIZE 1024
#define VSYSCALL_END (-2UL << 20)
#define VSYSCALL_ADDR(vsyscall_nr) (VSYSCALL_START+VSYSCALL_SIZE*(vsyscall_nr))

#ifdef __KERNEL__
#include <linux/seqlock.h>

#define __section_vxtime __attribute__ ((unused, __section__ (".vxtime"), aligned(16)))
#define __section_vgetcpu_mode __attribute__ ((unused, __section__ (".vgetcpu_mode"), aligned(16)))
#define __section_jiffies __attribute__ ((unused, __section__ (".jiffies"), aligned(16)))
#define __section_sys_tz __attribute__ ((unused, __section__ (".sys_tz"), aligned(16)))
#define __section_sysctl_vsyscall __attribute__ ((unused, __section__ (".sysctl_vsyscall"), aligned(16)))
#define __section_xtime __attribute__ ((unused, __section__ (".xtime"), aligned(16)))
#define __section_xtime_lock __attribute__ ((unused, __section__ (".xtime_lock"), aligned(16)))

#define VXTIME_TSC	1
#define VXTIME_HPET	2
#define VXTIME_PMTMR	3

#define VGETCPU_RDTSCP	1
#define VGETCPU_LSL	2

struct vxtime_data {
	long hpet_address;	/* HPET base address */
	int last;
	unsigned long last_tsc;
	long quot;
	long tsc_quot;
	int mode;
};

#define hpet_readl(a)           readl((const void __iomem *)fix_to_virt(FIX_HPET_BASE) + a)
#define hpet_writel(d,a)        writel(d, (void __iomem *)fix_to_virt(FIX_HPET_BASE) + a)

/* vsyscall space (readonly) */
extern struct vxtime_data __vxtime;
extern int __vgetcpu_mode;
extern struct timespec __xtime;
extern volatile unsigned long __jiffies;
extern struct timezone __sys_tz;
extern seqlock_t __xtime_lock;

/* kernel space (writeable) */
extern struct vxtime_data vxtime;
extern int vgetcpu_mode;
extern struct timezone sys_tz;
extern int sysctl_vsyscall;
extern seqlock_t xtime_lock;

extern int sysctl_vsyscall;

#define ARCH_HAVE_XTIME_LOCK 1

#endif /* __KERNEL__ */

#endif /* _ASM_X86_64_VSYSCALL_H_ */
