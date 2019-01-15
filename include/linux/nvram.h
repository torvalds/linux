/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NVRAM_H
#define _LINUX_NVRAM_H

#include <linux/errno.h>
#include <uapi/linux/nvram.h>

struct nvram_ops {
	ssize_t         (*get_size)(void);
	ssize_t         (*read)(char *, size_t, loff_t *);
	ssize_t         (*write)(char *, size_t, loff_t *);
};

extern const struct nvram_ops arch_nvram_ops;

static inline ssize_t nvram_get_size(void)
{
#ifdef CONFIG_PPC
#else
	if (arch_nvram_ops.get_size)
		return arch_nvram_ops.get_size();
#endif
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
	if (arch_nvram_ops.read)
		return arch_nvram_ops.read(buf, count, ppos);
	return -ENODEV;
}

static inline ssize_t nvram_write(char *buf, size_t count, loff_t *ppos)
{
	if (arch_nvram_ops.write)
		return arch_nvram_ops.write(buf, count, ppos);
	return -ENODEV;
}

#endif  /* _LINUX_NVRAM_H */
