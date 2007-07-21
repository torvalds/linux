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
#include <linux/udf_fs.h>

#include "udf_sb.h"

static int udf_translate_to_linux(uint8_t *, uint8_t *, int, uint8_t *, int);

static int udf_char_to_ustr(struct ustr *dest, const uint8_t *src, int strlen)
{
	if ((!dest) || (!src) || (!strlen) || (strlen > UDF_NAME_LEN - 2))
		return 0;

	memset(dest, 0, sizeof(struct ustr));
	memcpy(dest->u_name, src, strlen);
	dest->u_cmpID = 0x08;
	dest->u_len = strlen;

	return strlen;
}

/*
 * udf_build_ustr
 */
int udf_build_ustr(struct ustr *dest, dstring *ptr, int size)
{
	int usesize;

	if ((!dest) || (!ptr) || (!size))
		return -1;

	memset(dest, 0, sizeof(struct ustr));
	usesize = (size > UDF_NAME_LEN) ? UDF_NAME_LEN : size;
	dest->u_cmpID = ptr[0];
	dest->u_len = ptr[size - 1];
	memcpy(dest->u_name, ptr + 1, usesize - 1);

	return 0;
}

/*
 * udf_build_ustr_exact
 */
static int udf_build_ustr_exact(struct ustr *dest, dstring *ptr, int exactsize)
{
	if ((!dest) || (!ptr) || (!exactsize))
		return -1;

	memset(dest, 0, sizeof(struct ustr));
	dest->u_cmpID = ptr[0];
	dest->u_len = exactsize - 1;
	memcpy(dest->u_name, ptr + 1, exactsize - 1);

	return 0;
}

/*
 * udf_ocu_to_utf8
 *
 * PURPOSE
 *	Convert OSTA Compressed Unicode to the UTF-8 equivalent.
 *
 * DESCRIPTION
 *	This routine is only called by udf_filldir().
 *
 * PRE-CONDITIONS
 *	utf			Pointer to UTF-8 output buffer.
 *	ocu			Pointer to OSTA Compressed Unicode input buffer
 *				of size UDF_NAME_LEN bytes.
 * 				both of type "struct ustr *"
 *
 * POST-CONDITIONS
 *	<return>		Zero on success.
 *
 * HISTORY
 *	November 12, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
int udf_CS0toUTF8(struct ustr *utf_o, struct ustr *ocu_i)
{
	uint8_t *ocu;
	uint32_t c;
	uint8_t cmp_id, ocu_len;
	int i;

	ocu = ocu_i->u_name;

	ocu_len = ocu_i->u_len;
	cmp_id = ocu_i->u_cmpID;
	utf_o->u_len = 0;

	if (ocu_len == 0) {
		memset(utf_o, 0, sizeof(struct ustr));
		utf_o->u_cmpID = 0;
		utf_o->u_len = 0;
		return 0;
	}

	if ((cmp_id != 8) && (cmp_id != 16)) {
		printk(KERN_ERR "udf: unknown compression code (%d) stri=%s\n",
		       cmp_id, ocu_i->u_name);
		return 0;
	}

	for (i = 0; (i < ocu_len) && (utf_o->u_len <= (UDF_NAME_LEN - 3));) {

		/* Expand OSTA compressed Unicode to Unicode */
		c = ocu[i++];
		if (cmp_id == 16)
			c = (c << 8) | ocu[i++];

		/* Compress Unicode to UTF-8 */
		if (c < 0x80U) {
			utf_o->u_name[utf_o->u_len++] = (uint8_t)c;
		} else if (c < 0x800U) {
			utf_o->u_name[utf_o->u_len++] = (uint8_t)(0xc0 | (c >> 6));
			utf_o->u_name[utf_o->u_len++] = (uint8_t)(0x80 | (c & 0x3f));
		} else {
			utf_o->u_name[utf_o->u_len++] = (uint8_t)(0xe0 | (c >> 12));
			utf_o->u_name[utf_o->u_len++] = (uint8_t)(0x80 | ((c >> 6) & 0x3f));
			utf_o->u_name[utf_o->u_len++] = (uint8_t)(0x80 | (c & 0x3f));
		}
	}
	utf_o->u_cmpID = 8;

	return utf_o->u_len;
}

/*
 *
 * udf_utf8_to_ocu
 *
 * PURPOSE
 *	Convert UTF-8 to the OSTA Compressed Unicode equivalent.
 *
 * DESCRIPTION
 *	This routine is only called by udf_lookup().
 *
 * PRE-CONDITIONS
 *	ocu			Pointer to OSTA Compressed Unicode output
 *				buffer of size UDF_NAME_LEN bytes.
 *	utf			Pointer to UTF-8 input buffer.
 *	utf_len			Length of UTF-8 input buffer in bytes.
 *
 * POST-CONDITIONS
 *	<return>		Zero on success.
 *
 * HISTORY
 *	November 12, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
static int udf_UTF8toCS0(dstring *ocu, struct ustr *utf, int length)
{
	unsigned c, i, max_val, utf_char;
	int utf_cnt, u_len;

	memset(ocu, 0, sizeof(dstring) * length);
	ocu[0] = 8;
	max_val = 0xffU;

try_again:
	u_len = 0U;
	utf_char = 0U;
	utf_cnt = 0U;
	for (i = 0U; i < utf->u_len; i++) {
		c = (uint8_t)utf->u_name[i];

		/* Complete a multi-byte UTF-8 character */
		if (utf_cnt) {
			utf_char = (utf_char << 6) | (c & 0x3fU);
			if (--utf_cnt)
				continue;
		} else {
			/* Check for a multi-byte UTF-8 character */
			if (c & 0x80U) {
				/* Start a multi-byte UTF-8 character */
				if ((c & 0xe0U) == 0xc0U) {
					utf_char = c & 0x1fU;
					utf_cnt = 1;
				} else if ((c & 0xf0U) == 0xe0U) {
					utf_char = c & 0x0fU;
					utf_cnt = 2;
				} else if ((c & 0xf8U) == 0xf0U) {
					utf_char = c & 0x07U;
					utf_cnt = 3;
				} else if ((c & 0xfcU) == 0xf8U) {
					utf_char = c & 0x03U;
					utf_cnt = 4;
				} else if ((c & 0xfeU) == 0xfcU) {
					utf_char = c & 0x01U;
					utf_cnt = 5;
				} else {
					goto error_out;
				}
				continue;
			} else {
				/* Single byte UTF-8 character (most common) */
				utf_char = c;
			}
		}

		/* Choose no compression if necessary */
		if (utf_char > max_val) {
			if (max_val == 0xffU) {
				max_val = 0xffffU;
				ocu[0] = (uint8_t)0x10U;
				goto try_again;
			}
			goto error_out;
		}

		if (max_val == 0xffffU) {
			ocu[++u_len] = (uint8_t)(utf_char >> 8);
		}
		ocu[++u_len] = (uint8_t)(utf_char & 0xffU);
	}

	if (utf_cnt) {
error_out:
		ocu[++u_len] = '?';
		printk(KERN_DEBUG "udf: bad UTF-8 character\n");
	}

	ocu[length - 1] = (uint8_t)u_len + 1;

	return u_len + 1;
}

static int udf_CS0toNLS(struct nls_table *nls, struct ustr *utf_o,
			struct ustr *ocu_i)
{
	uint8_t *ocu;
	uint32_t c;
	uint8_t cmp_id, ocu_len;
	int i;

	ocu = ocu_i->u_name;

	ocu_len = ocu_i->u_len;
	cmp_id = ocu_i->u_cmpID;
	utf_o->u_len = 0;

	if (ocu_len == 0) {
		memset(utf_o, 0, sizeof(struct ustr));
		utf_o->u_cmpID = 0;
		utf_o->u_len = 0;
		return 0;
	}

	if ((cmp_id != 8) && (cmp_id != 16)) {
		printk(KERN_ERR "udf: unknown compression code (%d) stri=%s\n",
		       cmp_id, ocu_i->u_name);
		return 0;
	}

	for (i = 0; (i < ocu_len) && (utf_o->u_len <= (UDF_NAME_LEN - 3));) {
		/* Expand OSTA compressed Unicode to Unicode */
		c = ocu[i++];
		if (cmp_id == 16)
			c = (c << 8) | ocu[i++];

		utf_o->u_len += nls->uni2char(c, &utf_o->u_name[utf_o->u_len],
					      UDF_NAME_LEN - utf_o->u_len);
	}
	utf_o->u_cmpID = 8;

	return utf_o->u_len;
}

static int udf_NLStoCS0(struct nls_table *nls, dstring *ocu, struct ustr *uni,
			int length)
{
	unsigned len, i, max_val;
	uint16_t uni_char;
	int u_len;

	memset(ocu, 0, sizeof(dstring) * length);
	ocu[0] = 8;
	max_val = 0xffU;

try_again:
	u_len = 0U;
	for (i = 0U; i < uni->u_len; i++) {
		len = nls->char2uni(&uni->u_name[i], uni->u_len - i, &uni_char);
		if (len <= 0)
			continue;

		if (uni_char > max_val) {
			max_val = 0xffffU;
			ocu[0] = (uint8_t)0x10U;
			goto try_again;
		}

		if (max_val == 0xffffU)
			ocu[++u_len] = (uint8_t)(uni_char >> 8);
		ocu[++u_len] = (uint8_t)(uni_char & 0xffU);
		i += len - 1;
	}

	ocu[length - 1] = (uint8_t)u_len + 1;
	return u_len + 1;
}

int udf_get_filename(struct super_block *sb, uint8_t *sname, uint8_t *dname,
		     int flen)
{
	struct ustr filename, unifilename;
	int len;

	if (udf_build_ustr_exact(&unifilename, sname, flen)) {
		return 0;
	}

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UTF8)) {
		if (!udf_CS0toUTF8(&filename, &unifilename)) {
			udf_debug("Failed in udf_get_filename: sname = %s\n", sname);
			return 0;
		}
	} else if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP)) {
		if (!udf_CS0toNLS(UDF_SB(sb)->s_nls_map, &filename, &unifilename)) {
			udf_debug("Failed in udf_get_filename: sname = %s\n", sname);
			return 0;
		}
	} else {
		return 0;
	}

	len = udf_translate_to_linux(dname, filename.u_name, filename.u_len,
				     unifilename.u_name, unifilename.u_len);
	if (len) {
		return len;
	}

	return 0;
}

int udf_put_filename(struct super_block *sb, const uint8_t *sname,
		     uint8_t *dname, int flen)
{
	struct ustr unifilename;
	int namelen;

	if (!(udf_char_to_ustr(&unifilename, sname, flen))) {
		return 0;
	}

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_UTF8)) {
		namelen = udf_UTF8toCS0(dname, &unifilename, UDF_NAME_LEN);
		if (!namelen) {
			return 0;
		}
	} else if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP)) {
		namelen = udf_NLStoCS0(UDF_SB(sb)->s_nls_map, dname, &unifilename, UDF_NAME_LEN);
		if (!namelen) {
			return 0;
		}
	} else {
		return 0;
	}

	return namelen;
}

#define ILLEGAL_CHAR_MARK	'_'
#define EXT_MARK		'.'
#define CRC_MARK		'#'
#define EXT_SIZE 		5

static int udf_translate_to_linux(uint8_t *newName, uint8_t *udfName, int udfLen,
				  uint8_t *fidName, int fidNameLen)
{
	int index, newIndex = 0, needsCRC = 0;
	int extIndex = 0, newExtIndex = 0, hasExt = 0;
	unsigned short valueCRC;
	uint8_t curr;
	const uint8_t hexChar[] = "0123456789ABCDEF";

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
				while (index + 1 < udfLen && (udfName[index + 1] == '/' ||
							      udfName[index + 1] == 0))
					index++;
			} if (curr == EXT_MARK && (udfLen - index - 1) <= EXT_SIZE) {
				if (udfLen == index + 1) {
					hasExt = 0;
				} else {
					hasExt = 1;
					extIndex = index;
					newExtIndex = newIndex;
				}
			}
			if (newIndex < 256)
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
			for(index = 0; index < EXT_SIZE && extIndex + index + 1 < udfLen; index++) {
				curr = udfName[extIndex + index + 1];

				if (curr == '/' || curr == 0) {
					needsCRC = 1;
					curr = ILLEGAL_CHAR_MARK;
					while(extIndex + index + 2 < udfLen &&
					      (index + 1 < EXT_SIZE
					       && (udfName[extIndex + index + 2] == '/' ||
						   udfName[extIndex + index + 2] == 0)))
						index++;
				}
				ext[localExtIndex++] = curr;
			}
			maxFilenameLen = 250 - localExtIndex;
			if (newIndex > maxFilenameLen)
				newIndex = maxFilenameLen;
			else
				newIndex = newExtIndex;
		} else if (newIndex > 250) {
			newIndex = 250;
		}
		newName[newIndex++] = CRC_MARK;
		valueCRC = udf_crc(fidName, fidNameLen, 0);
		newName[newIndex++] = hexChar[(valueCRC & 0xf000) >> 12];
		newName[newIndex++] = hexChar[(valueCRC & 0x0f00) >> 8];
		newName[newIndex++] = hexChar[(valueCRC & 0x00f0) >> 4];
		newName[newIndex++] = hexChar[(valueCRC & 0x000f)];

		if (hasExt) {
			newName[newIndex++] = EXT_MARK;
			for (index = 0; index < localExtIndex; index++)
				newName[newIndex++] = ext[index];
		}
	}

	return newIndex;
}
