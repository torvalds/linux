/*
 * unicode.c
 *
 * PURPOSE
 *	Routines for converting between UTF-8 and OSTA Compressed Unicode.
 *      Also handles filename mangling
 *
 * DESCRIPTION
 *	OSTA Compressed Unicode is explained in the OSTA UDF specification.
 *		http://www.osta.org/
 *	UTF-8 is explained in the IETF RFC XXXX.
 *		ftp://ftp.internic.net/rfc/rfcxxxx.txt
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 */

#include "udfdecl.h"

#include <linux/kernel.h>
#include <linux/string.h>	/* for memset */
#include <linux/nls.h>
#include <linux/crc-itu-t.h>
#include <linux/slab.h>

#include "udf_sb.h"

static int udf_translate_to_linux(uint8_t *, int, const uint8_t *, int,
				  const uint8_t *, int);

static int udf_uni2char_utf8(wchar_t uni,
			     unsigned char *out,
			     int boundlen)
{
	int u_len = 0;

	if (boundlen <= 0)
		return -ENAMETOOLONG;

	if (uni < 0x80) {
		out[u_len++] = (unsigned char)uni;
	} else if (uni < 0x800) {
		if (boundlen < 2)
			return -ENAMETOOLONG;
		out[u_len++] = (unsigned char)(0xc0 | (uni >> 6));
		out[u_len++] = (unsigned char)(0x80 | (uni & 0x3f));
	} else {
		if (boundlen < 3)
			return -ENAMETOOLONG;
		out[u_len++] = (unsigned char)(0xe0 | (uni >> 12));
		out[u_len++] = (unsigned char)(0x80 | ((uni >> 6) & 0x3f));
		out[u_len++] = (unsigned char)(0x80 | (uni & 0x3f));
	}
	return u_len;
}

static int udf_char2uni_utf8(const unsigned char *in,
			     int boundlen,
			     wchar_t *uni)
{
	unsigned int utf_char;
	unsigned char c;
	int utf_cnt, u_len;

	utf_char = 0;
	utf_cnt = 0;
	for (u_len = 0; u_len < boundlen;) {
		c = in[u_len++];

		/* Complete a multi-byte UTF-8 character */
		if (utf_cnt) {
			utf_char = (utf_char << 6) | (c & 0x3f);
			if (--utf_cnt)
				continue;
		} else {
			/* Check for a multi-byte UTF-8 character */
			if (c & 0x80) {
				/* Start a multi-byte UTF-8 character */
				if ((c & 0xe0) == 0xc0) {
					utf_char = c & 0x1f;
					utf_cnt = 1;
				} else if ((c & 0xf0) == 0xe0) {
					utf_char = c & 0x0f;
					utf_cnt = 2;
				} else if ((c & 0xf8) == 0xf0) {
					utf_char = c & 0x07;
					utf_cnt = 3;
				} else if ((c & 0xfc) == 0xf8) {
					utf_char = c & 0x03;
					utf_cnt = 4;
				} else if ((c & 0xfe) == 0xfc) {
					utf_char = c & 0x01;
					utf_cnt = 5;
				} else {
					utf_cnt = -1;
					break;
				}
				continue;
			} else {
				/* Single byte UTF-8 character (most common) */
				utf_char = c;
			}
		}
		*uni = utf_char;
		break;
	}
	if (utf_cnt) {
		*uni = '?';
		return -EINVAL;
	}
	return u_len;
}

static int udf_name_from_CS0(uint8_t *str_o, int str_max_len,
			     const uint8_t *ocu, int ocu_len,
			     int (*conv_f)(wchar_t, unsigned char *, int))
{
	uint8_t cmp_id;
	int i, len;
	int str_o_len = 0;

	if (str_max_len <= 0)
		return 0;

	if (ocu_len == 0) {
		memset(str_o, 0, str_max_len);
		return 0;
	}

	cmp_id = ocu[0];
	if (cmp_id != 8 && cmp_id != 16) {
		memset(str_o, 0, str_max_len);
		pr_err("unknown compression code (%d) stri=%s\n", cmp_id, ocu);
		return -EINVAL;
	}

	for (i = 1; (i < ocu_len) && (str_o_len < str_max_len);) {
		/* Expand OSTA compressed Unicode to Unicode */
		uint32_t c = ocu[i++];
		if (cmp_id == 16)
			c = (c << 8) | ocu[i++];

		len = conv_f(c, &str_o[str_o_len], str_max_len - str_o_len);
		/* Valid character? */
		if (len >= 0)
			str_o_len += len;
		else if (len == -ENAMETOOLONG)
			break;
		else
			str_o[str_o_len++] = '?';
	}

	return str_o_len;
}

static int udf_name_to_CS0(uint8_t *ocu, int ocu_max_len,
			   const uint8_t *str_i, int str_len,
			   int (*conv_f)(const unsigned char *, int, wchar_t *))
{
	int i, len;
	unsigned int max_val;
	wchar_t uni_char;
	int u_len, u_ch;

	if (ocu_max_len <= 0)
		return 0;

	memset(ocu, 0, ocu_max_len);
	ocu[0] = 8;
	max_val = 0xff;
	u_ch = 1;

try_again:
	u_len = 1;
	for (i = 0; i < str_len; i++) {
		/* Name didn't fit? */
		if (u_len + u_ch > ocu_max_len)
			return 0;
		len = conv_f(&str_i[i], str_len - i, &uni_char);
		if (!len)
			continue;
		/* Invalid character, deal with it */
		if (len < 0) {
			len = 1;
			uni_char = '?';
		}

		if (uni_char > max_val) {
			max_val = 0xffff;
			ocu[0] = 0x10;
			u_ch = 2;
			goto try_again;
		}

		if (max_val == 0xffff)
			ocu[u_len++] = (uint8_t)(uni_char >> 8);
		ocu[u_len++] = (uint8_t)(uni_char & 0xff);
		i += len - 1;
	}

	return u_len;
}

int udf_CS0toUTF8(uint8_t *utf_o, int o_len, const uint8_t *ocu_i, int i_len)
{
	return udf_name_from_CS0(utf_o, o_len, ocu_i, i_len,
				 udf_uni2char_utf8);
}

int udf_get_filename(struct super_block *sb, const uint8_t *sname, int slen,
		     uint8_t *dname, int dlen)
{
	uint8_t *filename;
	int (*conv_f)(wchar_t, unsigned char *, int);
	int ret;

	if (!slen)
		return -EIO;

	if (dlen <= 0)
		return 0;

	filename = kmalloc(dlen, GFP_NOFS);
	if (!filename)
		return -ENOMEM;

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UTF8)) {
		conv_f = udf_uni2char_utf8;
	} else if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP)) {
		conv_f = UDF_SB(sb)->s_nls_map->uni2char;
	} else
		BUG();

	ret = udf_name_from_CS0(filename, dlen, sname, slen, conv_f);
	if (ret < 0) {
		udf_debug("Failed in udf_get_filename: sname = %s\n", sname);
		goto out2;
	}

	ret = udf_translate_to_linux(dname, dlen, filename, dlen,
				     sname + 1, slen - 1);
	/* Zero length filename isn't valid... */
	if (ret == 0)
		ret = -EINVAL;
out2:
	kfree(filename);
	return ret;
}

int udf_put_filename(struct super_block *sb, const uint8_t *sname, int slen,
		     uint8_t *dname, int dlen)
{
	int (*conv_f)(const unsigned char *, int, wchar_t *);

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UTF8)) {
		conv_f = udf_char2uni_utf8;
	} else if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP)) {
		conv_f = UDF_SB(sb)->s_nls_map->char2uni;
	} else
		BUG();

	return udf_name_to_CS0(dname, dlen, sname, slen, conv_f);
}

#define ILLEGAL_CHAR_MARK	'_'
#define EXT_MARK		'.'
#define CRC_MARK		'#'
#define EXT_SIZE 		5
/* Number of chars we need to store generated CRC to make filename unique */
#define CRC_LEN			5

static int udf_translate_to_linux(uint8_t *newName, int newLen,
				  const uint8_t *udfName, int udfLen,
				  const uint8_t *fidName, int fidNameLen)
{
	int index, newIndex = 0, needsCRC = 0;
	int extIndex = 0, newExtIndex = 0, hasExt = 0;
	unsigned short valueCRC;
	uint8_t curr;

	if (udfName[0] == '.' &&
	    (udfLen == 1 || (udfLen == 2 && udfName[1] == '.'))) {
		needsCRC = 1;
		newIndex = udfLen;
		memcpy(newName, udfName, udfLen);
	} else {
		for (index = 0; index < udfLen; index++) {
			curr = udfName[index];
			if (curr == '/' || curr == 0) {
				needsCRC = 1;
				curr = ILLEGAL_CHAR_MARK;
				while (index + 1 < udfLen &&
						(udfName[index + 1] == '/' ||
						 udfName[index + 1] == 0))
					index++;
			}
			if (curr == EXT_MARK &&
					(udfLen - index - 1) <= EXT_SIZE) {
				if (udfLen == index + 1)
					hasExt = 0;
				else {
					hasExt = 1;
					extIndex = index;
					newExtIndex = newIndex;
				}
			}
			if (newIndex < newLen)
				newName[newIndex++] = curr;
			else
				needsCRC = 1;
		}
	}
	if (needsCRC) {
		uint8_t ext[EXT_SIZE];
		int localExtIndex = 0;

		if (hasExt) {
			int maxFilenameLen;
			for (index = 0;
			     index < EXT_SIZE && extIndex + index + 1 < udfLen;
			     index++) {
				curr = udfName[extIndex + index + 1];

				if (curr == '/' || curr == 0) {
					needsCRC = 1;
					curr = ILLEGAL_CHAR_MARK;
					while (extIndex + index + 2 < udfLen &&
					      (index + 1 < EXT_SIZE &&
						(udfName[extIndex + index + 2] == '/' ||
						 udfName[extIndex + index + 2] == 0)))
						index++;
				}
				ext[localExtIndex++] = curr;
			}
			maxFilenameLen = newLen - CRC_LEN - localExtIndex;
			if (newIndex > maxFilenameLen)
				newIndex = maxFilenameLen;
			else
				newIndex = newExtIndex;
		} else if (newIndex > newLen - CRC_LEN)
			newIndex = newLen - CRC_LEN;
		newName[newIndex++] = CRC_MARK;
		valueCRC = crc_itu_t(0, fidName, fidNameLen);
		newName[newIndex++] = hex_asc_upper_hi(valueCRC >> 8);
		newName[newIndex++] = hex_asc_upper_lo(valueCRC >> 8);
		newName[newIndex++] = hex_asc_upper_hi(valueCRC);
		newName[newIndex++] = hex_asc_upper_lo(valueCRC);

		if (hasExt) {
			newName[newIndex++] = EXT_MARK;
			for (index = 0; index < localExtIndex; index++)
				newName[newIndex++] = ext[index];
		}
	}

	return newIndex;
}
