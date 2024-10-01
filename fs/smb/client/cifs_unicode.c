// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2009
 *   Modified by Steve French (sfrench@us.ibm.com)
 */
#include <linux/fs.h>
#include <linux/slab.h>
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"

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
	if (src_char >= 0xF001 && src_char <= 0xF01F) {
		*target = src_char - 0xF000;
		return true;
	}
	switch (src_char) {
	case SFM_COLON:
		*target = ':';
		break;
	case SFM_DOUBLEQUOTE:
		*target = '"';
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
	case SFM_SPACE:
		*target = ' ';
		break;
	case SFM_PERIOD:
		*target = '.';
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
cifs_mapchar(char *target, const __u16 *from, const struct nls_table *cp,
	     int maptype)
{
	int len = 1;
	__u16 src_char;

	src_char = *from;

	if ((maptype == SFM_MAP_UNI_RSVD) && convert_sfm_char(src_char, target))
		return len;
	else if ((maptype == SFU_MAP_UNI_RSVD) &&
		  convert_sfu_char(src_char, target))
		return len;

	/* if character not one of seven in special remap set */
	len = cp->uni2char(src_char, target, NLS_MAX_CHARSET_SIZE);
	if (len <= 0)
		goto surrogate_pair;

	return len;

surrogate_pair:
	/* convert SURROGATE_PAIR and IVS */
	if (strcmp(cp->charset, "utf8"))
		goto unknown;
	len = utf16s_to_utf8s(from, 3, UTF16_LITTLE_ENDIAN, target, 6);
	if (len <= 0)
		goto unknown;
	return len;

unknown:
	*target = '?';
	len = 1;
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
	__u16 ftmp[3];		/* ftmp[3] = 3array x 2bytes = 6bytes UTF-16 */

	/*
	 * because the chars can be of varying widths, we need to take care
	 * not to overflow the destination buffer when we get close to the
	 * end of it. Until we get to this offset, we don't need to check
	 * for overflow however.
	 */
	safelen = tolen - (NLS_MAX_CHARSET_SIZE + nullsize);

	for (i = 0; i < fromwords; i++) {
		ftmp[0] = get_unaligned_le16(&from[i]);
		if (ftmp[0] == 0)
			break;
		if (i + 1 < fromwords)
			ftmp[1] = get_unaligned_le16(&from[i + 1]);
		else
			ftmp[1] = 0;
		if (i + 2 < fromwords)
			ftmp[2] = get_unaligned_le16(&from[i + 2]);
		else
			ftmp[2] = 0;

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

		/* charlen (=bytes of UTF-8 for 1 character)
		 * 4bytes UTF-8(surrogate pair) is charlen=4
		 *   (4bytes UTF-16 code)
		 * 7-8bytes UTF-8(IVS) is charlen=3+4 or 4+4
		 *   (2 UTF-8 pairs divided to 2 UTF-16 pairs) */
		if (charlen == 4)
			i++;
		else if (charlen >= 5)
			/* 5-6bytes UTF-8 */
			i += 2;
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
	__u16 ftmp[3];

	for (i = 0; i < maxwords; i++) {
		ftmp[0] = get_unaligned_le16(&from[i]);
		if (ftmp[0] == 0)
			break;
		if (i + 1 < maxwords)
			ftmp[1] = get_unaligned_le16(&from[i + 1]);
		else
			ftmp[1] = 0;
		if (i + 2 < maxwords)
			ftmp[2] = get_unaligned_le16(&from[i + 2]);
		else
			ftmp[2] = 0;

		charlen = cifs_mapchar(tmp, ftmp, codepage, NO_MAP_UNI_RSVD);
		outlen += charlen;
	}

	return outlen;
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
		dst = kstrndup(src, maxlen, GFP_KERNEL);
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

static __le16 convert_to_sfm_char(char src_char, bool end_of_string)
{
	__le16 dest_char;

	if (src_char >= 0x01 && src_char <= 0x1F) {
		dest_char = cpu_to_le16(src_char + 0xF000);
		return dest_char;
	}
	switch (src_char) {
	case ':':
		dest_char = cpu_to_le16(SFM_COLON);
		break;
	case '"':
		dest_char = cpu_to_le16(SFM_DOUBLEQUOTE);
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
	case '.':
		if (end_of_string)
			dest_char = cpu_to_le16(SFM_PERIOD);
		else
			dest_char = 0;
		break;
	case ' ':
		if (end_of_string)
			dest_char = cpu_to_le16(SFM_SPACE);
		else
			dest_char = 0;
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
	wchar_t *wchar_to;	/* UTF-16 */
	int ret;
	unicode_t u;

	if (map_chars == NO_MAP_UNI_RSVD)
		return cifs_strtoUTF16(target, source, PATH_MAX, cp);

	wchar_to = kzalloc(6, GFP_KERNEL);

	for (i = 0; i < srclen; j++) {
		src_char = source[i];
		charlen = 1;

		/* check if end of string */
		if (src_char == 0)
			goto ctoUTF16_out;

		/* see if we must remap this char */
		if (map_chars == SFU_MAP_UNI_RSVD)
			dst_char = convert_to_sfu_char(src_char);
		else if (map_chars == SFM_MAP_UNI_RSVD) {
			bool end_of_string;

			/**
			 * Remap spaces and periods found at the end of every
			 * component of the path. The special cases of '.' and
			 * '..' do not need to be dealt with explicitly because
			 * they are addressed in namei.c:link_path_walk().
			 **/
			if ((i == srclen - 1) || (source[i+1] == '\\'))
				end_of_string = true;
			else
				end_of_string = false;

			dst_char = convert_to_sfm_char(src_char, end_of_string);
		} else
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
			if (charlen > 0)
				goto ctoUTF16;

			/* convert SURROGATE_PAIR */
			if (strcmp(cp->charset, "utf8") || !wchar_to)
				goto unknown;
			if (*(source + i) & 0x80) {
				charlen = utf8_to_utf32(source + i, 6, &u);
				if (charlen < 0)
					goto unknown;
			} else
				goto unknown;
			ret  = utf8s_to_utf16s(source + i, charlen,
					       UTF16_LITTLE_ENDIAN,
					       wchar_to, 6);
			if (ret < 0)
				goto unknown;

			i += charlen;
			dst_char = cpu_to_le16(*wchar_to);
			if (charlen <= 3)
				/* 1-3bytes UTF-8 to 2bytes UTF-16 */
				put_unaligned(dst_char, &target[j]);
			else if (charlen == 4) {
				/* 4bytes UTF-8(surrogate pair) to 4bytes UTF-16
				 * 7-8bytes UTF-8(IVS) divided to 2 UTF-16
				 *   (charlen=3+4 or 4+4) */
				put_unaligned(dst_char, &target[j]);
				dst_char = cpu_to_le16(*(wchar_to + 1));
				j++;
				put_unaligned(dst_char, &target[j]);
			} else if (charlen >= 5) {
				/* 5-6bytes UTF-8 to 6bytes UTF-16 */
				put_unaligned(dst_char, &target[j]);
				dst_char = cpu_to_le16(*(wchar_to + 1));
				j++;
				put_unaligned(dst_char, &target[j]);
				dst_char = cpu_to_le16(*(wchar_to + 2));
				j++;
				put_unaligned(dst_char, &target[j]);
			}
			continue;

unknown:
			dst_char = cpu_to_le16(0x003f);
			charlen = 1;
		}

ctoUTF16:
		/*
		 * character may take more than one byte in the source string,
		 * but will take exactly two bytes in the target string
		 */
		i += charlen;
		put_unaligned(dst_char, &target[j]);
	}

ctoUTF16_out:
	put_unaligned(0, &target[j]); /* Null terminate target unicode string */
	kfree(wchar_to);
	return j;
}

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
