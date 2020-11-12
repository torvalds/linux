/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_ENDIANNESS_H
#define _OBJTOOL_ENDIANNESS_H

#include <linux/kernel.h>
#include <endian.h>
#include "arch_endianness.h"

#ifndef __TARGET_BYTE_ORDER
#error undefined arch __TARGET_BYTE_ORDER
#endif

#if __BYTE_ORDER != __TARGET_BYTE_ORDER
#define __NEED_BSWAP 1
#else
#define __NEED_BSWAP 0
#endif

/*
 * Does a byte swap if target endianness doesn't match the host, i.e. cross
 * compilation for little endian on big endian and vice versa.
 * To be used for multi-byte values conversion, which are read from / about
 * to be written to a target native endianness ELF file.
 */
#define bswap_if_needed(val)						\
({									\
	__typeof__(val) __ret;						\
	switch (sizeof(val)) {						\
	case 8: __ret = __NEED_BSWAP ? bswap_64(val) : (val); break;	\
	case 4: __ret = __NEED_BSWAP ? bswap_32(val) : (val); break;	\
	case 2: __ret = __NEED_BSWAP ? bswap_16(val) : (val); break;	\
	default:							\
		BUILD_BUG(); break;					\
	}								\
	__ret;								\
})

#endif /* _OBJTOOL_ENDIANNESS_H */
