#ifndef ASM_X86__VGTOD_H
#define ASM_X86__VGTOD_H

#include <asm/vsyscall.h>
#include <linux/clocksource.h>

struct vsyscall_gtod_data {
	seqlock_t	lock;

	/* open coded 'struct timespec' */
	time_t		wall_time_sec;
	u32		wall_time_nsec;

	int		sysctl_enabled;
	struct timezone sys_tz;
	struct { /* extract of a clocksource struct */
		cycle_t (*vread)(void);
		cycle_t	cycle_last;
		cycle_t	mask;
		u32	mult;
		u32	shift;
	} clock;
	struct timespec wall_to_monotonic;
};
extern struct vsyscall_gtod_data __vsyscall_gtod_data
__section_vsyscall_gtod_data;
extern struct vsyscall_gtod_data vsyscall_gtod_data;

#endif /* ASM_X86__VGTOD_H */
