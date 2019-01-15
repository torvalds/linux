/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NVRAM_H
#define _LINUX_NVRAM_H

#include <linux/errno.h>
#include <uapi/linux/nvram.h>

static inline ssize_t nvram_get_size(void)
{
	return -ENODEV;
}

static inline unsigned char nvram_read_byte(int addr)
{
	return 0xFF;
}

static inline void nvram_write_byte(unsigned char val, int addr)
{
}

static inline ssize_t nvram_read(char *buf, size_t count, loff_t *ppos)
{
	return -ENODEV;
}

static inline ssize_t nvram_write(char *buf, size_t count, loff_t *ppos)
{
	return -ENODEV;
}

#endif  /* _LINUX_NVRAM_H */
