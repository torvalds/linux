/*
 *  include/asm-s390/ebcdic.h
 *    EBCDIC -> ASCII, ASCII -> EBCDIC conversion routines.
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _EBCDIC_H
#define _EBCDIC_H

#ifndef _S390_TYPES_H
#include <types.h>
#endif

extern __u8 _ascebc_500[];   /* ASCII -> EBCDIC 500 conversion table */
extern __u8 _ebcasc_500[];   /* EBCDIC 500 -> ASCII conversion table */
extern __u8 _ascebc[];   /* ASCII -> EBCDIC conversion table */
extern __u8 _ebcasc[];   /* EBCDIC -> ASCII conversion table */
extern __u8 _ebc_tolower[]; /* EBCDIC -> lowercase */
extern __u8 _ebc_toupper[]; /* EBCDIC -> uppercase */

extern __inline__ void
codepage_convert(const __u8 *codepage, volatile __u8 * addr, unsigned long nr)
{
	if (nr-- <= 0)
		return;
        __asm__ __volatile__(
		"   bras 1,1f\n"
		"   tr   0(1,%0),0(%2)\n"
                "0: tr   0(256,%0),0(%2)\n"
		"   la   %0,256(%0)\n"
		"1: ahi  %1,-256\n"
		"   jnm  0b\n"
		"   ex   %1,0(1)"
                : "+&a" (addr), "+&a" (nr)
                : "a" (codepage) : "cc", "memory", "1" );
}

#define ASCEBC(addr,nr) codepage_convert(_ascebc, addr, nr)
#define EBCASC(addr,nr) codepage_convert(_ebcasc, addr, nr)
#define ASCEBC_500(addr,nr) codepage_convert(_ascebc_500, addr, nr)
#define EBCASC_500(addr,nr) codepage_convert(_ebcasc_500, addr, nr)
#define EBC_TOLOWER(addr,nr) codepage_convert(_ebc_tolower, addr, nr)
#define EBC_TOUPPER(addr,nr) codepage_convert(_ebc_toupper, addr, nr)

#endif

