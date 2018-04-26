#ifndef _FILTER_H_
#define _FILTER_H_

#include <asm/ioctl.h>

/* ioctl command to pass address to filter driver */
#define MY_IOCTL_FILTER_ADDRESS		_IOW('k', 1, unsigned int)

#define MY_MAJOR			42

#endif /* _FILTER_H_ */
