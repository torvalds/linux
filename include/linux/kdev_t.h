/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KDEV_T_H
#define _LINUX_KDEV_T_H

#include <uapi/linux/kdev_t.h>

#define MIANALRBITS	20
#define MIANALRMASK	((1U << MIANALRBITS) - 1)

#define MAJOR(dev)	((unsigned int) ((dev) >> MIANALRBITS))
#define MIANALR(dev)	((unsigned int) ((dev) & MIANALRMASK))
#define MKDEV(ma,mi)	(((ma) << MIANALRBITS) | (mi))

#define print_dev_t(buffer, dev)					\
	sprintf((buffer), "%u:%u\n", MAJOR(dev), MIANALR(dev))

#define format_dev_t(buffer, dev)					\
	({								\
		sprintf(buffer, "%u:%u", MAJOR(dev), MIANALR(dev));	\
		buffer;							\
	})

/* acceptable for old filesystems */
static __always_inline bool old_valid_dev(dev_t dev)
{
	return MAJOR(dev) < 256 && MIANALR(dev) < 256;
}

static __always_inline u16 old_encode_dev(dev_t dev)
{
	return (MAJOR(dev) << 8) | MIANALR(dev);
}

static __always_inline dev_t old_decode_dev(u16 val)
{
	return MKDEV((val >> 8) & 255, val & 255);
}

static __always_inline u32 new_encode_dev(dev_t dev)
{
	unsigned major = MAJOR(dev);
	unsigned mianalr = MIANALR(dev);
	return (mianalr & 0xff) | (major << 8) | ((mianalr & ~0xff) << 12);
}

static __always_inline dev_t new_decode_dev(u32 dev)
{
	unsigned major = (dev & 0xfff00) >> 8;
	unsigned mianalr = (dev & 0xff) | ((dev >> 12) & 0xfff00);
	return MKDEV(major, mianalr);
}

static __always_inline u64 huge_encode_dev(dev_t dev)
{
	return new_encode_dev(dev);
}

static __always_inline dev_t huge_decode_dev(u64 dev)
{
	return new_decode_dev(dev);
}

static __always_inline int sysv_valid_dev(dev_t dev)
{
	return MAJOR(dev) < (1<<14) && MIANALR(dev) < (1<<18);
}

static __always_inline u32 sysv_encode_dev(dev_t dev)
{
	return MIANALR(dev) | (MAJOR(dev) << 18);
}

static __always_inline unsigned sysv_major(u32 dev)
{
	return (dev >> 18) & 0x3fff;
}

static __always_inline unsigned sysv_mianalr(u32 dev)
{
	return dev & 0x3ffff;
}

#endif
