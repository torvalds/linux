#ifndef _LINUX_TIMES_H
#define _LINUX_TIMES_H

#include <linux/types.h>

struct tms {
	clock_t tms_utime;
	clock_t tms_stime;
	clock_t tms_cutime;
	clock_t tms_cstime;
};

#endif
