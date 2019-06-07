/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NVRAM_H
#define _LINUX_NVRAM_H

#include <linux/errno.h>
#include <uapi/linux/nvram.h>

#ifdef CONFIG_PPC
#include <asm/machdep.h>
#endif

/**
 * struct nvram_ops - NVRAM functionality made available to drivers
 * @read: validate checksum (if any) then load a range of bytes from NVRAM
 * @write: store a range of bytes to NVRAM then update checksum (if any)
 * @read_byte: load a single byte from NVRAM
 * @write_byte: store a single byte to NVRAM
 * @get_size: return the fixed number of bytes in the NVRAM
 *
 * Architectures which provide an nvram ops struct need not implement all
 * of these methods. If the NVRAM hardware can be accessed only one byte
 * at a time then it may be sufficient to provide .read_byte and .write_byte.
 * If the NVRAM has a checksum (and it is to be checked) the .read and
 * .write methods can be used to implement that efficiently.
 *
 * Portable drivers may use the wrapper functions defined here.
 * The nvram_read() and nvram_write() functions call the .read and .write
 * methods when available and fall back on the .read_byte and .write_byte
 * methods otherwise.
 */

struct nvram_ops {
	ssize_t         (*get_size)(void);
	unsigned char   (*read_byte)(int);
	void            (*write_byte)(unsigned char, int);
	ssize_t         (*read)(char *, size_t, loff_t *);
	ssize_t         (*write)(char *, size_t, loff_t *);
#if defined(CONFIG_X86) || defined(CONFIG_M68K)
	long            (*initialize)(void);
	long            (*set_checksum)(void);
#endif
};

extern const struct nvram_ops arch_nvram_ops;

static inline ssize_t nvram_get_size(void)
{
#ifdef CONFIG_PPC
	if (ppc_md.nvram_size)
		return ppc_md.nvram_size();
#else
	if (arch_nvram_ops.get_size)
		return arch_nvram_ops.get_size();
#endif
	return -ENODEV;
}

static inline unsigned char nvram_read_byte(int addr)
{
#ifdef CONFIG_PPC
	if (ppc_md.nvram_read_val)
		return ppc_md.nvram_read_val(addr);
#else
	if (arch_nvram_ops.read_byte)
		return arch_nvram_ops.read_byte(addr);
#endif
	return 0xFF;
}

static inline void nvram_write_byte(unsigned char val, int addr)
{
#ifdef CONFIG_PPC
	if (ppc_md.nvram_write_val)
		ppc_md.nvram_write_val(addr, val);
#else
	if (arch_nvram_ops.write_byte)
		arch_nvram_ops.write_byte(val, addr);
#endif
}

static inline ssize_t nvram_read_bytes(char *buf, size_t count, loff_t *ppos)
{
	ssize_t nvram_size = nvram_get_size();
	loff_t i;
	char *p = buf;

	if (nvram_size < 0)
		return nvram_size;
	for (i = *ppos; count > 0 && i < nvram_size; ++i, ++p, --count)
		*p = nvram_read_byte(i);
	*ppos = i;
	return p - buf;
}

static inline ssize_t nvram_write_bytes(char *buf, size_t count, loff_t *ppos)
{
	ssize_t nvram_size = nvram_get_size();
	loff_t i;
	char *p = buf;

	if (nvram_size < 0)
		return nvram_size;
	for (i = *ppos; count > 0 && i < nvram_size; ++i, ++p, --count)
		nvram_write_byte(*p, i);
	*ppos = i;
	return p - buf;
}

static inline ssize_t nvram_read(char *buf, size_t count, loff_t *ppos)
{
#ifdef CONFIG_PPC
	if (ppc_md.nvram_read)
		return ppc_md.nvram_read(buf, count, ppos);
#else
	if (arch_nvram_ops.read)
		return arch_nvram_ops.read(buf, count, ppos);
#endif
	return nvram_read_bytes(buf, count, ppos);
}

static inline ssize_t nvram_write(char *buf, size_t count, loff_t *ppos)
{
#ifdef CONFIG_PPC
	if (ppc_md.nvram_write)
		return ppc_md.nvram_write(buf, count, ppos);
#else
	if (arch_nvram_ops.write)
		return arch_nvram_ops.write(buf, count, ppos);
#endif
	return nvram_write_bytes(buf, count, ppos);
}

#endif  /* _LINUX_NVRAM_H */
