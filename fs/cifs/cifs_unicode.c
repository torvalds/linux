/*
 *   fs/cifs/cifs_unicode.c
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2005
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include "cifs_unicode.h"
#include "cifs_uniupr.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"

/*
 * NAME:	cifs_strfromUCS()
 *
 * FUNCTION:	Convert little-endian unicode string to character string
 *
 */
int
cifs_strfromUCS_le(char *to, const __le16 * from,
		   int len, const struct nls_table *codepage)
{
	int i;
	int outlen = 0;

	for (i = 0; (i < len) && from[i]; i++) {
		int charlen;
		/* 2.4.0 kernel or greater */
		charlen =
		    codepage->uni2char(le16_to_cpu(from[i]), &to[outlen],
				       NLS_MAX_CHARSET_SIZE);
		if (charlen > 0) {
			outlen += charlen;
		} else {
			to[outlen++] = '?';
		}
	}
	to[outlen] = 0;
	return outlen;
}

/*
 * NAME:	cifs_strtoUCS()
 *
 * FUNCTION:	Convert character string to unicode string
 *
 */
int
cifs_strtoUCS(__le16 * to, const char *from, int len,
	      const struct nls_table *codepage)
{
	int charlen;
	int i;
	wchar_t * wchar_to = (wchar_t *)to; /* needed to quiet sparse */

	for (i = 0; len && *from; i++, from += charlen, len -= charlen) {

		/* works for 2.4.0 kernel or later */
		charlen = codepage->char2uni(from, len, &wchar_to[i]);
		if (charlen < 1) {
			cERROR(1,
			       ("strtoUCS: char2uni of %d returned %d",
				(int)*from, charlen));
			/* A question mark */
			to[i] = cpu_to_le16(0x003f);
			charlen = 1;
		} else
			to[i] = cpu_to_le16(wchar_to[i]);

	}

	to[i] = 0;
	return i;
}

