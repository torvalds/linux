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
#include <linux/slab.h>
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "cifs_uniupr.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"

/*
 * cifs_utf16_bytes - how long will a string be after conversion?
 * @utf16 - pointer to input string
 * @maxbytes - don't go past this many bytes of input string
 * @codepage - destination codepage
 *
 * Walk a utf16le string and return the number of bytes that the string will
 * be after being converted to the given charset, not including any null
 * termination required. Don't walk past maxbytes in the source buffer.
 */
int
cifs_utf16_bytes(const __le16 *from, int maxbytes,
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

int cifs_remap(struct cifs_sb_info *cifs_sb)
{
	int map_type;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SFM_CHR)
		map_type = SFM_MAP_UNI_RSVD;
	else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR)
		map_type = SFU_MAP_UNI_RSVD;
	else
		map_type = NO_MAP_UNI_RSVD;

	return map_type;
}

/* Convert character using the SFU - "Services for Unix" remapping range */
static bool
convert_sfu_char(const __u16 src_char, char *target)
{
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
		return false;
	}
	return true;
}

/* Convert character using the SFM - "Services for Mac" remapping range */
static bool
convert_sfm_char(const __u16 src_char, char *target)
{
	switch (src_char) {
	case SFM_COLON:
		*target = ':';
		break;
	case SFM_ASTERISK:
		*target = '*';
		break;
	case SFM_QUESTION:
		*target = '?';
		break;
	case SFM_PIPE:
		*target = '|';
		break;
	case SFM_GRTRTHAN:
		*target = '>';
		break;
	case SFM_LESSTHAN:
		*target = '<';
		break;
	case SFM_SLASH:
		*target = '\\';
		break;
	default:
		return false;
	}
	return true;
}


/*
 * cifs_mapchar - convert a host-endian char to proper char in codepage
 * @target - where converted character should be copied
 * @src_char - 2 byte host-endian source character
 * @cp - codepage to which character should be converted
 * @map_type - How should the 7 NTFS/SMB reserved characters be mapped to UCS2?
 *
 * This function handles the conversion of a single character. It is the
 * responsibility of the caller to ensure that the target buffer is large
 * enough to hold the result of the conversion (at least NLS_MAX_CHARSET_SIZE).
 */
static int
cifs_mapchar(char *target, const __u16 src_char, const struct nls_table *cp,
	     int maptype)
{
	int len = 1;

	if ((maptype == SFM_MAP_UNI_RSVD) && convert_sfm_char(src_char, target))
		return len;
	else if ((maptype == SFU_MAP_UNI_RSVD) &&
		  convert_sfu_char(src_char, target))
		return len;

	/* if character not one of seven in special remap set */
	len = cp->uni2char(src_char, target, NLS_MAX_CHARSET_SIZE);
	if (len <= 0) {
		*target = '?';
		len = 1;
	}
	return len;
}

/*
 * cifs_from_utf16 - convert utf16le string to local charset
 * @to - destination buffer
 * @from - source buffer
 * @tolen - destination buffer size (in bytes)
 * @fromlen - source buffer size (in bytes)
 * @codepage - codepage to which characters should be converted
 * @mapchar - should characters be remapped according to the mapchars option?
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
 */
int
cifs_from_utf16(char *to, const __le16 *from, int tolen, int fromlen,
		const struct nls_table *codepage, int map_type)
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
			charlen = cifs_mapchar(tmp, ftmp, codepage, map_type);
			if ((outlen + charlen) > (tolen - nullsize))
				break;
		}

		/* put converted char into 'to' buffer */
		charlen = cifs_mapchar(&to[outlen], ftmp, codepage, map_type);
		outlen += charlen;
	}

	/* properly null-terminate string */
	for (i = 0; i < nullsize; i++)
		to[outlen++] = 0;

	return outlen;
}

/*
 * NAME:	cifs_strtoUTF16()
 *
 * FUNCTION:	Convert character string to unicode string
 *
 */
int
cifs_strtoUTF16(__le16 *to, const char *from, int len,
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

	for (i = 0; len && *from; i++, from += charlen, len -= charlen) {
		charlen = codepage->char2uni(from, len, &wchar_to);
		if (charlen < 1) {
			cifs_dbg(VFS, "strtoUTF16: char2uni of 0x%x returned %d\n",
				 *from, charlen);
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
 * cifs_strndup_from_utf16 - copy a string from wire format to the local
 * codepage
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
cifs_strndup_from_utf16(const char *src, const int maxlen,
			const bool is_unicode, const struct nls_table *codepage)
{
	int len;
	char *dst;

	if (is_unicode) {
		len = cifs_utf16_bytes((__le16 *) src, maxlen, codepage);
		len += nls_nullsize(codepage);
		dst = kmalloc(len, GFP_KERNEL);
		if (!dst)
			return NULL;
		cifs_from_utf16(dst, (__le16 *) src, len, maxlen, codepage,
			       NO_MAP_UNI_RSVD);
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

static __le16 convert_to_sfu_char(char src_char)
{
	__le16 dest_char;

	switch (src_char) {
	case ':':
		dest_char = cpu_to_le16(UNI_COLON);
		break;
	case '*':
		dest_char = cpu_to_le16(UNI_ASTERISK);
		break;
	case '?':
		dest_char = cpu_to_le16(UNI_QUESTION);
		break;
	case '<':
		dest_char = cpu_to_le16(UNI_LESSTHAN);
		break;
	case '>':
		dest_char = cpu_to_le16(UNI_GRTRTHAN);
		break;
	case '|':
		dest_char = cpu_to_le16(UNI_PIPE);
		break;
	default:
		dest_char = 0;
	}

	return dest_char;
}

static __le16 convert_to_sfm_char(char src_char)
{
	__le16 dest_char;

	switch (src_char) {
	case ':':
		dest_char = cpu_to_le16(SFM_COLON);
		break;
	case '*':
		dest_char = cpu_to_le16(SFM_ASTERISK);
		break;
	case '?':
		dest_char = cpu_to_le16(SFM_QUESTION);
		break;
	case '<':
		dest_char = cpu_to_le16(SFM_LESSTHAN);
		break;
	case '>':
		dest_char = cpu_to_le16(SFM_GRTRTHAN);
		break;
	case '|':
		dest_char = cpu_to_le16(SFM_PIPE);
		break;
	default:
		dest_char = 0;
	}

	return dest_char;
}

/*
 * Convert 16 bit Unicode pathname to wire format from string in current code
 * page. Conversion may involve remapping up the six characters that are
 * only legal in POSIX-like OS (if they are present in the string). Path
 * names are little endian 16 bit Unicode on the wire
 */
int
cifsConvertToUTF16(__le16 *target, const char *source, int srclen,
		 const struct nls_table *cp, int map_chars)
{
	int i, charlen;
	int j = 0;
	char src_char;
	__le16 dst_char;
	wchar_t tmp;

	if (map_chars == NO_MAP_UNI_RSVD)
		return cifs_strtoUTF16(target, source, PATH_MAX, cp);

	for (i = 0; i < srclen; j++) {
		src_char = source[i];
		charlen = 1;

		/* check if end of string */
		if (src_char == 0)
			goto ctoUTF16_out;

		/* see if we must remap this char */
		if (map_chars == SFU_MAP_UNI_RSVD)
			dst_char = convert_to_sfu_char(src_char);
		else if (map_chars == SFM_MAP_UNI_RSVD)
			dst_char = convert_to_sfm_char(src_char);
		else
			dst_char = 0;
		/*
		 * FIXME: We can not handle remapping backslash (UNI_SLASH)
		 * until all the calls to build_path_from_dentry are modified,
		 * as they use backslash as separator.
		 */
		if (dst_char == 0) {
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

ctoUTF16_out:
	put_unaligned(0, &target[j]); /* Null terminate target unicode string */
	return j;
}

#ifdef CONFIG_CIFS_SMB2
/*
 * cifs_local_to_utf16_bytes - how long will a string be after conversion?
 * @from - pointer to input string
 * @maxbytes - don't go past this many bytes of input string
 * @codepage - source codepage
 *
 * Walk a string and return the number of bytes that the string will
 * be after being converted to the given charset, not including any null
 * termination required. Don't walk past maxbytes in the source buffer.
 */

static int
cifs_local_to_utf16_bytes(const char *from, int len,
			  const struct nls_table *codepage)
{
	int charlen;
	int i;
	wchar_t wchar_to;

	for (i = 0; len && *from; i++, from += charlen, len -= charlen) {
		charlen = codepage->char2uni(from, len, &wchar_to);
		/* Failed conversion defaults to a question mark */
		if (charlen < 1)
			charlen = 1;
	}
	return 2 * i; /* UTF16 characters are two bytes */
}

/*
 * cifs_strndup_to_utf16 - copy a string to wire format from the local codepage
 * @src - source string
 * @maxlen - don't walk past this many bytes in the source string
 * @utf16_len - the length of the allocated string in bytes (including null)
 * @cp - source codepage
 * @remap - map special chars
 *
 * Take a string convert it from the local codepage to UTF16 and
 * put it in a new buffer. Returns a pointer to the new string or NULL on
 * error.
 */
__le16 *
cifs_strndup_to_utf16(const char *src, const int maxlen, int *utf16_len,
		      const struct nls_table *cp, int remap)
{
	int len;
	__le16 *dst;

	len = cifs_local_to_utf16_bytes(src, maxlen, cp);
	len += 2; /* NULL */
	dst = kmalloc(len, GFP_KERNEL);
	if (!dst) {
		*utf16_len = 0;
		return NULL;
	}
	cifsConvertToUTF16(dst, src, strlen(src), cp, remap);
	*utf16_len = len;
	return dst;
}
#endif /* CONFIG_CIFS_SMB2 */
