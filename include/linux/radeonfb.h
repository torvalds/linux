#ifndef __LINUX_RADEONFB_H__
#define __LINUX_RADEONFB_H__

#include <asm/ioctl.h>
#include <asm/types.h>

#define ATY_RADEON_LCD_ON	0x00000001
#define ATY_RADEON_CRT_ON	0x00000002


#define FBIO_RADEON_GET_MIRROR	_IOR('@', 3, size_t)
#define FBIO_RADEON_SET_MIRROR	_IOW('@', 4, size_t)

#endif

