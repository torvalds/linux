/*	$NetBSD: cd9660_conversion.c,v 1.4 2007/03/14 14:11:17 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 Daniel Watt, Walter Deignan, Ryan Gabrys, Alan
 * Perez-Rathke and Ram Vedam.  All rights reserved.
 *
 * This code was written by Daniel Watt, Walter Deignan, Ryan Gabrys,
 * Alan Perez-Rathke and Ram Vedam.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE,DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
#include "cd9660.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

static char cd9660_compute_gm_offset(time_t);

#if 0
static inline int
cd9660_pad_even(length)
int length;
{
	return length + (length & 0x01);
}
#endif

/*
* These can probably be implemented using a macro
*/

/* Little endian */
void
cd9660_721(uint16_t w, unsigned char *twochar)
{
#if BYTE_ORDER == BIG_ENDIAN
	w = bswap16(w);
#endif
	memcpy(twochar,&w,2);
}

void
cd9660_731(uint32_t w, unsigned char *fourchar)
{
#if BYTE_ORDER == BIG_ENDIAN
	w = bswap32(w);
#endif
	memcpy(fourchar,&w,4);
}

/* Big endian */
void
cd9660_722(uint16_t w, unsigned char *twochar)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	w = bswap16(w);
#endif
	memcpy(twochar,&w,2);
}

void
cd9660_732(uint32_t w, unsigned char *fourchar)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	w = bswap32(w);
#endif
	memcpy(fourchar,&w,4);
}

/**
* Convert a dword into a double endian string of eight characters
* @param int The double word to convert
* @param char* The string to write the both endian double word to - It is assumed this is allocated and at least
*		eight characters long
*/
void
cd9660_bothendian_dword(uint32_t dw, unsigned char *eightchar)
{
	uint32_t le, be;
#if BYTE_ORDER == LITTLE_ENDIAN
	le = dw;
	be = bswap32(dw);
#endif
#if BYTE_ORDER == BIG_ENDIAN
	be = dw;
	le = bswap32(dw);
#endif
	memcpy(eightchar, &le, 4);
	memcpy((eightchar+4), &be, 4);
}

/**
* Convert a word into a double endian string of four characters
* @param int The word to convert
* @param char* The string to write the both endian word to - It is assumed this is allocated and at least
*		four characters long
*/
void
cd9660_bothendian_word(uint16_t dw, unsigned char *fourchar)
{
	uint16_t le, be;
#if BYTE_ORDER == LITTLE_ENDIAN
	le = dw;
	be = bswap16(dw);
#endif
#if BYTE_ORDER == BIG_ENDIAN
	be = dw;
	le = bswap16(dw);
#endif
	memcpy(fourchar, &le, 2);
	memcpy((fourchar+2), &be, 2);
}

void
cd9660_pad_string_spaces(char *str, int len)
{
	int i;

	for (i = 0; i < len; i ++) {
		if (str[i] == '\0')
			str[i] = 0x20;
	}
}

static char
cd9660_compute_gm_offset(time_t tim)
{
	struct tm t, gm;

	(void)localtime_r(&tim, &t);
	(void)gmtime_r(&tim, &gm);
	gm.tm_year -= t.tm_year;
	gm.tm_yday -= t.tm_yday;
	gm.tm_hour -= t.tm_hour;
	gm.tm_min  -= t.tm_min;
	if (gm.tm_year < 0)
		gm.tm_yday = -1;
	else if (gm.tm_year > 0)
		gm.tm_yday = 1;

	return (char)(-(gm.tm_min + 60* (24 * gm.tm_yday + gm.tm_hour)) / 15);
}

/* Long dates: 17 characters */
void
cd9660_time_8426(unsigned char *buf, time_t tim)
{
	struct tm t;
	char temp[18];

	(void)localtime_r(&tim, &t);
	(void)snprintf(temp, sizeof(temp), "%04i%02i%02i%02i%02i%02i%02i",
		1900+(int)t.tm_year,
		(int)t.tm_mon+1,
		(int)t.tm_mday,
		(int)t.tm_hour,
		(int)t.tm_min,
		(int)t.tm_sec,
		0);
	(void)memcpy(buf, temp, 16);
	buf[16] = cd9660_compute_gm_offset(tim);
}

/* Short dates: 7 characters */
void
cd9660_time_915(unsigned char *buf, time_t tim)
{
	struct tm t;

	(void)localtime_r(&tim, &t);
	buf[0] = t.tm_year;
	buf[1] = t.tm_mon+1;
	buf[2] = t.tm_mday;
	buf[3] = t.tm_hour;
	buf[4] = t.tm_min;
	buf[5] = t.tm_sec;
	buf[6] = cd9660_compute_gm_offset(tim);
}
