// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Some of the source code in this file came from fs/cifs/cifs_unicode.c
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2009
 *   Modified by Steve French (sfrench@us.ibm.com)
 *   Modified by Namjae Jeon (linkinjeon@kernel.org)
 */
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include "glob.h"
#include "unicode.h"
#include "uniupr.h"
#include "smb_common.h"

/*
 * smb_utf16_bytes() - how long will a string be after conversion?
 * @from:	pointer to input string
 * @maxbytes:	don't go past this many bytes of input string
 * @codepage:	destination codepage
 *
 * Walk a utf16le string and return the number of bytes that the string will
 * be after being converted to the given charset, not including any null
 * termination required. Don't walk past maxbytes in the source buffer.
 *
 * Return:	string length after conversion
 */
static int smb_utf16_bytes(const __le16 *from,
			   int maxbytes,
			   const struct nls_table *codepage)
{
	int i;
	int charlen, outlen = 0;
	int maxwords = maxbytes / 2;
	char tmp[NLS_MAX_CHARSET_SIZE];
	__u16 ftmp;

	for (i = 0; i < maxwords; i++) {
		ftmp = get_unaligned_le16(&from[i]);
		if (ftmp == 0)
			break;

		charlen = codepage->uni2char(ftmp, tmp, NLS_MAX_CHARSET_SIZE);
		if (charlen > 0)
			outlen += charlen;
		else
			outlen++;
	}

	return outlen;
}

/*
 * cifs_mapchar() - convert a host-endian char to proper char in codepage
 * @target:	where converted character should be copied
 * @src_char:	2 byte host-endian source character
 * @cp:		codepage to which character should be converted
 * @mapchar:	should character be mapped according to mapchars mount option?
 *
 * This function handles the conversion of a single character. It is the
 * responsibility of the caller to ensure that the target buffer is large
 * enough to hold the result of the conversion (at least NLS_MAX_CHARSET_SIZE).
 *
 * Return:	string length after conversion
 */
static int
cifs_mapchar(char *target, const __u16 src_char, const struct nls_table *cp,
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
	switch (src_char) {
	case UNI_COLON:
		*target = ':';
		break;
	case UNI_ASTERISK:
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
	len = cp->uni2char(src_char, target, NLS_MAX_CHARSET_SIZE);
	if (len <= 0) {
		*target = '?';
		len = 1;
	}

	goto out;
}

/*
 * is_char_allowed() - check for valid character
 * @ch:		input character to be checked
 *
 * Return:	1 if char is allowed, otherwise 0
 */
static inline int is_char_allowed(char *ch)
{
	/* check for control chars, wildcards etc. */
	if (!(*ch & 0x80) &&
		(*ch <= 0x1f ||
		 *ch == '?' || *ch == '"' || *ch == '<' ||
		 *ch == '>' || *ch == '|'))
		return 0;

	return 1;
}

/*
 * smb_from_utf16() - convert utf16le string to local charset
 * @to:		destination buffer
 * @from:	source buffer
 * @tolen:	destination buffer size (in bytes)
 * @fromlen:	source buffer size (in bytes)
 * @codepage:	codepage to which characters should be converted
 * @mapchar:	should characters be remapped according to the mapchars option?
 *
 * Convert a little-endian utf16le string (as sent by the server) to a string
 * in the provided codepage. The tolen and fromlen parameters are to ensure
 * that the code doesn't walk off of the end of the buffer (which is always
 * a danger if the alignment of the source buffer is off). The destination
 * string is always properly null terminated and fits in the destination
 * buffer. Returns the length of the destination string in bytes (including
 * null terminator).
 *
 * Note that some windows versions actually send multiword UTF-16 characters
 * instead of straight UTF16-2. The linux nls routines however aren't able to
 * deal with those characters properly. In the event that we get some of
 * those characters, they won't be translated properly.
 *
 * Return:	string length after conversion
 */
static int smb_from_utf16(char *to,
			  const __le16 *from,
			  int tolen,
			  int fromlen,
			  const struct nls_table *codepage,
			  bool mapchar)
{
	int i, charlen, safelen;
	int outlen = 0;
	int nullsize = nls_nullsize(codepage);
	int fromwords = fromlen / 2;
	char tmp[NLS_MAX_CHARSET_SIZE];
	__u16 ftmp;

	/*
	 * because the chars can be of varying widths, we need to take care
	 * not to overflow the destination buffer when we get close to the
	 * end of it. Until we get to this offset, we don't need to check
	 * for overflow however.
	 */
	safelen = tolen - (NLS_MAX_CHARSET_SIZE + nullsize);

	for (i = 0; i < fromwords; i++) {
		ftmp = get_unaligned_le16(&from[i]);
		if (ftmp == 0)
			break;

		/*
		 * check to see if converting this character might make the
		 * conversion bleed into the null terminator
		 */
		if (outlen >= safelen) {
			charlen = cifs_mapchar(tmp, ftmp, codepage, mapchar);
			if ((outlen + charlen) > (tolen - nullsize))
				break;
		}

		/* put converted char into 'to' buffer */
		charlen = cifs_mapchar(&to[outlen], ftmp, codepage, mapchar);
		outlen += charlen;
	}

	/* properly null-terminate string */
	for (i = 0; i < nullsize; i++)
		to[outlen++] = 0;

	return outlen;
}

/*
 * smb_strtoUTF16() - Convert character string to unicode string
 * @to:		destination buffer
 * @from:	source buffer
 * @len:	destination buffer size (in bytes)
 * @codepage:	codepage to which characters should be converted
 *
 * Return:	string length after conversion
 */
int
smb_strtoUTF16(__le16 *to, const char *from, int len,
	      const struct nls_table *codepage)
{
	int charlen;
	int i;
	wchar_t wchar_to; /* needed to quiet sparse */

	/* special case for utf8 to handle no plane0 chars */
	if (!strcmp(codepage->charset, "utf8")) {
		/*
		 * convert utf8 -> utf16, we assume we have enough space
		 * as caller should have assumed conversion does not overflow
		 * in destination len is length in wchar_t units (16bits)
		 */
		i  = utf8s_to_utf16s(from, len, UTF16_LITTLE_ENDIAN,
				       (wchar_t *) to, len);

		/* if success terminate and exit */
		if (i >= 0)
			goto success;
		/*
		 * if fails fall back to UCS encoding as this
		 * function should not return negative values
		 * currently can fail only if source contains
		 * invalid encoded characters
		 */
	}

	for (i = 0; len > 0 && *from; i++, from += charlen, len -= charlen) {
		charlen = codepage->char2uni(from, len, &wchar_to);
		if (charlen < 1) {
			/* A question mark */
			wchar_to = 0x003f;
			charlen = 1;
		}
		put_unaligned_le16(wchar_to, &to[i]);
	}

success:
	put_unaligned_le16(0, &to[i]);
	return i;
}

/*
 * smb_strndup_from_utf16() - copy a string from wire format to the local
 *		codepage
 * @src:	source string
 * @maxlen:	don't walk past this many bytes in the source string
 * @is_unicode:	is this a unicode string?
 * @codepage:	destination codepage
 *
 * Take a string given by the server, convert it to the local codepage and
 * put it in a new buffer. Returns a pointer to the new string or NULL on
 * error.
 *
 * Return:	destination string buffer or error ptr
 */
char *
smb_strndup_from_utf16(const char *src, const int maxlen,
			const bool is_unicode, const struct nls_table *codepage)
{
	int len, ret;
	char *dst;

	if (is_unicode) {
		len = smb_utf16_bytes((__le16 *) src, maxlen, codepage);
		len += nls_nullsize(codepage);
		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return ERR_PTR(-ENOMEM);
		ret = smb_from_utf16(dst, (__le16 *) src, len, maxlen, codepage,
			       false);
		if (ret < 0) {
			kfree(dst);
			return ERR_PTR(-EINVAL);
		}
	} else {
		len = strnlen(src, maxlen);
		len++;
		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return ERR_PTR(-ENOMEM);
		strscpy(dst, src, len);
	}

	return dst;
}

/*
 * Convert 16 bit Unicode pathname to wire format from string in current code
 * page. Conversion may involve remapping up the six characters that are
 * only legal in POSIX-like OS (if they are present in the string). Path
 * names are little endian 16 bit Unicode on the wire
 */
/*
 * smbConvertToUTF16() - convert string from local charset to utf16
 * @target:	destination buffer
 * @source:	source buffer
 * @srclen:	source buffer size (in bytes)
 * @cp:		codepage to which characters should be converted
 * @mapchar:	should characters be remapped according to the mapchars option?
 *
 * Convert 16 bit Unicode pathname to wire format from string in current code
 * page. Conversion may involve remapping up the six characters that are
 * only legal in POSIX-like OS (if they are present in the string). Path
 * names are little endian 16 bit Unicode on the wire
 *
 * Return:	char length after conversion
 */
int
smbConvertToUTF16(__le16 *target, const char *source, int srclen,
		 const struct nls_table *cp, int mapchars)
{
	int i, j, charlen;
	char src_char;
	__le16 dst_char;
	wchar_t tmp;

	if (!mapchars)
		return smb_strtoUTF16(target, source, srclen, cp);

	for (i = 0, j = 0; i < srclen; j++) {
		src_char = source[i];
		charlen = 1;
		switch (src_char) {
		case 0:
			put_unaligned(0, &target[j]);
			return j;
		case ':':
			dst_char = cpu_to_le16(UNI_COLON);
			break;
		case '*':
			dst_char = cpu_to_le16(UNI_ASTERISK);
			break;
		case '?':
			dst_char = cpu_to_le16(UNI_QUESTION);
			break;
		case '<':
			dst_char = cpu_to_le16(UNI_LESSTHAN);
			break;
		case '>':
			dst_char = cpu_to_le16(UNI_GRTRTHAN);
			break;
		case '|':
			dst_char = cpu_to_le16(UNI_PIPE);
			break;
		/*
		 * FIXME: We can not handle remapping backslash (UNI_SLASH)
		 * until all the calls to build_path_from_dentry are modified,
		 * as they use backslash as separator.
		 */
		default:
			charlen = cp->char2uni(source + i, srclen - i, &tmp);
			dst_char = cpu_to_le16(tmp);

			/*
			 * if no match, use question mark, which at least in
			 * some cases serves as wild card
			 */
			if (charlen < 1) {
				dst_char = cpu_to_le16(0x003f);
				charlen = 1;
			}
		}
		/*
		 * character may take more than one byte in the source string,
		 * but will take exactly two bytes in the target string
		 */
		i += charlen;
		put_unaligned(dst_char, &target[j]);
	}

	return j;
}
