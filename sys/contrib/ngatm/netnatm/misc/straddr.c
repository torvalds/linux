/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: libunimsg/netnatm/misc/straddr.c,v 1.4 2004/07/08 08:22:02 brandt Exp $
 */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/ctype.h>
#include <sys/libkern.h>
#else
#include <ctype.h>
#include <string.h>
#endif
#include <netnatm/addr.h>

/*
 * Convert an NSAP address from the ASCII format to the binary.
 * ASCII format means each byte formatted as a 2-byte hex number
 * with dots freely interspersed between the bytes.
 * If the conversion is succesful, the function returns 0, -1
 * on conversion errors.
 */
int
uni_str2nsap(u_char *out, const char *in)
{
	int i;
	int c;

	for(i = 0; i < 20; i++) {
		while((c = *in++) == '.')
			;
		if(!isascii(c) || !isxdigit(c))
			return -1;
		out[i] = isdigit(c) ? (c - '0')
			: islower(c) ? (c - 'a' + 10)
			: (c - 'A' + 10);
		out[i] <<= 4;
		c = *in++;
		if(!isascii(c) || !isxdigit(c))
			return -1;
		out[i] |= isdigit(c) ? (c - '0')
			: islower(c) ? (c - 'a' + 10)
			: (c - 'A' + 10);
	}
	return *in != '\0';
}

/*
 * Parse an emebedded E.164 NSAP address.
 * If check is 0, the contents of the last 11 bytes are ignored
 * If check is 1, the contents of all of these but the selector byte
 * are checked to be zero. If check is 2 all 11 bytes must be 0.
 */
int
uni_nsap2e164(char *e164, const u_char *nsap, int check)
{
	char *p = e164;
	u_int d;
	int i;

	if(nsap[0] != 0x45)
		return -1;
	if((nsap[8] & 0xf) != 0xf)
		return -1;
	for(i = 1; i <= 7; i++) {
		d = (nsap[i] >> 4) & 0xf;
		if(d == 0x00 && p == e164)
			continue;
		if(d >= 0xa)
			return -1;
		*p++ = d + '0';

		d = nsap[i] & 0xf;
		if(d == 0x00 && p == e164)
			continue;
		if(d >= 0xa)
			return -1;
		*p++ = d + '0';
	}
	d = (nsap[i] >> 4) & 0xf;
	if(d != 0x00 || p == e164) {
		if(d >= 0xa)
			return -1;
		*p++ = d + '0';
	}
	if(p == e164)
		return -1;
	*p++ = 0;

	if(check == 0)
		return 0;
	while(i < ((check == 1) ? 19 : 20)) {
		if(nsap[i] != 0x00)
			return -1;
		i++;
	}

	return 0;
}

/*
 * Convert a binary representation to ASCII. The standard formats are
 * recognized and dotted. Non-standard formats get no dots altogether.
 */
void
uni_prefix2str(char *out, const u_char *in, u_int len, int dotit)
{
	static char hex[16] = "0123456789abcdef";
	static int fmt[3][6] = {
		{ 1, 2, 10, 6, 1, 0 },
		{ 1, 2, 10, 6, 1, 0 },
		{ 1, 8,  4, 6, 1, 0 },
	};
	int f, b;
	u_int i;

	if (len > 20)
		len = 20;

	if(dotit) {
		switch(*in) {

		  case 0x39:	/* DCC */
			i = 0;
  fmt:
			for(f = 0; fmt[i][f]; f++) {
				if (len == 0)
					goto done;
				if(f != 0)
					*out++ = '.';
				for(b = 0; b < fmt[i][f]; b++) {
					if (len-- == 0)
						goto done;
					*out++ = hex[(*in >> 4) & 0xf];
					*out++ = hex[*in & 0xf];
					in++;
				}
			}
  done:
			*out = '\0';
			return;

		  case 0x47:	/* ICD */
			i = 1;
			goto fmt;

		  case 0x45:	/* E.164 */
			i = 2;
			goto fmt;
		}
	}

	/* undotted */
	for(i = 0; i < len; i++) {
		*out++ = hex[(*in >> 4) & 0xf];
		*out++ = hex[*in & 0xf];
		in++;
	}
	*out = '\0';
}

void
uni_nsap2str(char *out, const u_char *in, int dotit)
{
	uni_prefix2str(out, in, 20, dotit);
}

/*
 * Make an embedded E.164 NSAP address from a NSAP address.
 * The E.164 address is a string of digits, at least one digit and
 * not more than 15 digits long. The NSAP address will start with
 * byte 0x45 and then a 8 byte field, which contains the right
 * justified E.164 address in BCD coding, filled with a 0xf to the
 * right. The rest of the address is zero.
 * The function returns 0 if everything is ok, -1 in case of a wrong
 * E.164 address.
 */
int
uni_e1642nsap(u_char *nsap, const char *e164)
{
	size_t len;
	int fill;
	u_int i;

	if((len = strlen(e164)) > 15 || len == 0)
		return -1;
	for(i = 0; i < len; i++)
		if(!isdigit(e164[i]))
			return -1;

	*nsap++ = 0x45;
	fill = (15 - len) / 2;
	while(fill--)
		*nsap++ = 0x00;
	if((len & 1) == 0) {
		*nsap++ = *e164++ - '0';
		len--;
	}
	while(len > 1) {
		len -= 2;
		*nsap = (*e164++ - '0') << 4;
		*nsap++ |= *e164 - '0';
	}
	*nsap++ = ((*e164++ - '0') << 4) | 0xf;
	for(fill = 0; fill < 11; fill++)
		*nsap++ = 0;

	return 0;
}
