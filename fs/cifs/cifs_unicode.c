/*
 *   fs/cifs/cifs_unicode.c
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2009
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
 * cifs_ucs2_bytes - how long will a string be after conversion?
 * @ucs - pointer to input string
 * @maxbytes - don't go past this many bytes of input string
 * @codepage - destination codepage
 *
 * Walk a ucs2le string and return the number of bytes that the string will
 * be after being converted to the given charset, not including any null
 * termination required. Don't walk past maxbytes in the source buffer.
 */
int
cifs_ucs2_bytes(const __le16 *from, int maxbytes,
		const struct nls_table *codepage)
{
	int i;
	int charlen, outlen = 0;
	int maxwords = maxbytes / 2;
	char tmp[NLS_MAX_CHARSET_SIZE];

	for (i = 0; i < maxwords && from[i]; i++) {
		charlen = codepage->uni2char(le16_to_cpu(from[i]), tmp,
					     NLS_MAX_CHARSET_SIZE);
		if (charlen > 0)
			outlen += charlen;
		else
			outlen++;
	}

	return outlen;
}

/*
 * cifs_mapchar - convert a little-endian char to proper char in codepage
 * @target - where converted character should be copied
 * @src_char - 2 byte little-endian source character
 * @cp - codepage to which character should be converted
 * @mapchar - should character be mapped according to mapchars mount option?
 *
 * This function handles the conversion of a single character. It is the
 * responsibility of the caller to ensure that the target buffer is large
 * enough to hold the result of the conversion (at least NLS_MAX_CHARSET_SIZE).
 */
static int
cifs_mapchar(char *target, const __le16 src_char, const struct nls_table *cp,
	     bool mapchar)
{
	int len = 1;

	if (!mapchar)
		goto cp_convert;

	/*
	 * BB: Cannot handle remapping UNI_SLASH until all the calls to
	 *     build_path_from_dentry are modified, as they use slash as
	 *     separator.
	 */
	switch (le16_to_cpu(src_char)) {
	case UNI_COLON:
		*target = ':';
		break;
	case UNI_ASTERIK:
		*target = '*';
		break;
	case UNI_QUESTION:
		*target = '?';
		break;
	case UNI_PIPE:
		*target = '|';
		break;
	case UNI_GRTRTHAN:
		*target = '>';
		break;
	case UNI_LESSTHAN:
		*target = '<';
		break;
	default:
		goto cp_convert;
	}

out:
	return len;

cp_convert:
	len = cp->uni2char(le16_to_cpu(src_char), target,
			   NLS_MAX_CHARSET_SIZE);
	if (len <= 0) {
		*target = '?';
		len = 1;
	}
	goto out;
}

/*
 * cifs_from_ucs2 - convert utf16le string to local charset
 * @to - destination buffer
 * @from - source buffer
 * @tolen - destination buffer size (in bytes)
 * @fromlen - source buffer size (in bytes)
 * @codepage - codepage to which characters should be converted
 * @mapchar - should characters be remapped according to the mapchars option?
 *
 * Convert a little-endian ucs2le string (as sent by the server) to a string
 * in the provided codepage. The tolen and fromlen parameters are to ensure
 * that the code doesn't walk off of the end of the buffer (which is always
 * a danger if the alignment of the source buffer is off). The destination
 * string is always properly null terminated and fits in the destination
 * buffer. Returns the length of the destination string in bytes (including
 * null terminator).
 *
 * Note that some windows versions actually send multiword UTF-16 characters
 * instead of straight UCS-2. The linux nls routines however aren't able to
 * deal with those characters properly. In the event that we get some of
 * those characters, they won't be translated properly.
 */
int
cifs_from_ucs2(char *to, const __le16 *from, int tolen, int fromlen,
		 const struct nls_table *codepage, bool mapchar)
{
	int i, charlen, safelen;
	int outlen = 0;
	int nullsize = nls_nullsize(codepage);
	int fromwords = fromlen / 2;
	char tmp[NLS_MAX_CHARSET_SIZE];

	/*
	 * because the chars can be of varying widths, we need to take care
	 * not to overflow the destination buffer when we get close to the
	 * end of it. Until we get to this offset, we don't need to check
	 * for overflow however.
	 */
	safelen = tolen - (NLS_MAX_CHARSET_SIZE + nullsize);

	for (i = 0; i < fromwords && from[i]; i++) {
		/*
		 * check to see if converting this character might make the
		 * conversion bleed into the null terminator
		 */
		if (outlen >= safelen) {
			charlen = cifs_mapchar(tmp, from[i], codepage, mapchar);
			if ((outlen + charlen) > (tolen - nullsize))
				break;
		}

		/* put converted char into 'to' buffer */
		charlen = cifs_mapchar(&to[outlen], from[i], codepage, mapchar);
		outlen += charlen;
	}

	/* properly null-terminate string */
	for (i = 0; i < nullsize; i++)
		to[outlen++] = 0;

	return outlen;
}

/*
 * NAME:	cifs_strtoUCS()
 *
 * FUNCTION:	Convert character string to unicode string
 *
 */
int
cifs_strtoUCS(__le16 *to, const char *from, int len,
	      const struct nls_table *codepage)
{
	int charlen;
	int i;
	wchar_t *wchar_to = (wchar_t *)to; /* needed to quiet sparse */

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

/*
 * cifs_strndup_from_ucs - copy a string from wire format to the local codepage
 * @src - source string
 * @maxlen - don't walk past this many bytes in the source string
 * @is_unicode - is this a unicode string?
 * @codepage - destination codepage
 *
 * Take a string given by the server, convert it to the local codepage and
 * put it in a new buffer. Returns a pointer to the new string or NULL on
 * error.
 */
char *
cifs_strndup_from_ucs(const char *src, const int maxlen, const bool is_unicode,
	     const struct nls_table *codepage)
{
	int len;
	char *dst;

	if (is_unicode) {
		len = cifs_ucs2_bytes((__le16 *) src, maxlen, codepage);
		len += nls_nullsize(codepage);
		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return NULL;
		cifs_from_ucs2(dst, (__le16 *) src, len, maxlen, codepage,
			       false);
	} else {
		len = strnlen(src, maxlen);
		len++;
		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return NULL;
		strlcpy(dst, src, len);
	}

	return dst;
}

