/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_ENDIANNESS_H
#define _OBJTOOL_ENDIANNESS_H

#include <linux/kernel.h>
#include <endian.h>
#include <objtool/elf.h>

/*
 * Does a byte swap if target file endianness doesn't match the host, i.e. cross
 * compilation for little endian on big endian and vice versa.
 * To be used for multi-byte values conversion, which are read from / about
 * to be written to a target native endianness ELF file.
 */
static inline bool need_bswap(struct elf *elf)
{
	return (__BYTE_ORDER == __LITTLE_ENDIAN) ^
	       (elf->ehdr.e_ident[EI_DATA] == ELFDATA2LSB);
}

#define bswap_if_needed(elf, val)					\
({									\
	__typeof__(val) __ret;						\
	bool __need_bswap = need_bswap(elf);				\
	switch (sizeof(val)) {						\
	case 8:								\
		__ret = __need_bswap ? bswap_64(val) : (val); break;	\
	case 4:								\
		__ret = __need_bswap ? bswap_32(val) : (val); break;	\
	case 2:								\
		__ret = __need_bswap ? bswap_16(val) : (val); break;	\
	default:							\
		BUILD_BUG(); break;					\
	}								\
	__ret;								\
})

#endif /* _OBJTOOL_ENDIANNESS_H */
