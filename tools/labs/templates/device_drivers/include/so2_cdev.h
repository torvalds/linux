#ifndef __PSOCDEV_H__
#define __PSOCDEV_H__		1

#include <asm/ioctl.h>

#define BUFFER_SIZE		256

#define MY_IOCTL_PRINT		_IOC(_IOC_NONE,  'k', 1, 0)
#define MY_IOCTL_SET_BUFFER	_IOC(_IOC_WRITE, 'k', 2, BUFFER_SIZE)
#define MY_IOCTL_GET_BUFFER	_IOC(_IOC_READ,  'k', 3, BUFFER_SIZE)
#define MY_IOCTL_DOWN		_IOC(_IOC_NONE,  'k', 4, 0)
#define MY_IOCTL_UP		_IOC(_IOC_NONE,  'k', 5, 0)

#endif
