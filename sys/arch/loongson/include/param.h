/*	$OpenBSD: param.h,v 1.3 2013/03/23 16:12:23 deraadt Exp $ */

/* Public Domain */

#ifndef	_MACHINE_PARAM_H_
#define	_MACHINE_PARAM_H_

#define	MACHINE		"loongson"
#define	_MACHINE	loongson
#define	MACHINE_ARCH	"mips64el"	/* not the canonical endianness */
#define	_MACHINE_ARCH	mips64el
#define	MACHINE_CPU	"mips64"
#define	_MACHINE_CPU	mips64
#define	MID_MACHINE	MID_MIPS64

#ifdef _KERNEL

/*
 * The Loongson level 1 cache expects software to prevent virtual
 * aliases. Unfortunately, since this cache is physically tagged,
 * this would require all virtual address to have the same bits 14
 * and 13 as their physical addresses, which is not something the
 * kernel can guarantee unless the page size is at least 16KB.
 */
#define	PAGE_SHIFT	14

#endif /* _KERNEL */

#include <mips64/param.h>

#endif /* _MACHINE_PARAM_H_ */
