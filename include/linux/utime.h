#ifndef _LINUX_UTIME_H
#define _LINUX_UTIME_H

struct utimbuf {
	time_t actime;
	time_t modtime;
};

#endif
