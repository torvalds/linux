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

#define PLANE_SIZE 0x10000
#define UNICODE_MAX 0x10ffff
#define SURROGATE_MASK 0xfffff800
#define SURROGATE_PAIR 0x0000d800
#define SURROGATE_LOW  0x00000400
#define SURROGATE_CHAR_BITS 10
#define SURROGATE_CHAR_MASK ((1 << SURROGATE_CHAR_BITS) - 1)

#define ILLEGAL_CHAR_MARK	'_'
#define EXT_MARK		'.'
#define CRC_MARK		'#'
#define EXT_SIZE		5
/* Number of chars we need to store generated CRC to make filename unique */
#define CRC_LEN			5

static unicode_t get_utf16_char(const uint8_t *str_i, int str_i_max_len,
				int str_i_idx, int u_ch, unicode_t *ret)
{
	unicode_t c;
	int start_idx = str_i_idx;

	/* Expand OSTA compressed Unicode to Unicode */
	c = str_i[str_i_idx++];
	if (u_ch > 1)
		c = (c << 8) | str_i[str_i_idx++];
	if ((c & SURROGATE_MASK) == SURROGATE_PAIR) {
		unicode_t next;

		/* Trailing surrogate char */
		if (str_i_idx >= str_i_max_len) {
			c = UNICODE_MAX + 1;
			goto out;
		}

		/* Low surrogate must follow the high one... */
		if (c & SURROGATE_LOW) {
			c = UNICODE_MAX + 1;
			goto out;
		}

		WARN_ON_ONCE(u_ch != 2);
		next = str_i[str_i_idx++] << 8;
		next |= str_i[str_i_idx++];
		if ((next & SURROGATE_MASK) != SURROGATE_PAIR ||
		    !(next & SURROGATE_LOW)) {
			c = UNICODE_MAX + 1;
			goto out;
		}

		c = PLANE_SIZE +
		    ((c & SURROGATE_CHAR_MASK) << SURROGATE_CHAR_BITS) +
		    (next & SURROGATE_CHAR_MASK);
	}
out:
	*ret = c;
	return str_i_idx - start_idx;
}


static int udf_name_conv_char(uint8_t *str_o, int str_o_max_len,
			      int *str_o_idx,
			      const uint8_t *str_i, int str_i_max_len,
			      int *str_i_idx,
			      int u_ch, int *needsCRC,
			      int (*conv_f)(wchar_t, unsigned char *, int),
			      int translate)
{
	unicode_t c;
	int illChar = 0;
	int len, gotch = 0;

	while (!gotch && *str_i_idx < str_i_max_len) {
		if (*str_o_idx >= str_o_max_len) {
			*needsCRC = 1;
			return gotch;
		}

		len = get_utf16_char(str_i, str_i_max_len, *str_i_idx, u_ch,
				     &c);
		/* These chars cannot be converted. Replace them. */
		if (c == 0 || c > UNICODE_MAX || (conv_f && c > MAX_WCHAR_T) ||
		    (translate && c == '/')) {
			illChar = 1;
			if (!translate)
				gotch = 1;
		} else if (illChar)
			break;
		else
			gotch = 1;
		*str_i_idx += len;
	}
	if (illChar) {
		*needsCRC = 1;
		c = ILLEGAL_CHAR_MARK;
		gotch = 1;
	}
	if (gotch) {
		if (conv_f) {
			len = conv_f(c, &str_o[*str_o_idx],
				     str_o_max_len - *str_o_idx);
		} else {
			len = utf32_to_utf8(c, &str_o[*str_o_idx],
					    str_o_max_len - *str_o_idx);
			if (len < 0)
				len = -ENAMETOOLONG;
		}
		/* Valid character? */
		if (len >= 0)
			*str_o_idx += len;
		else if (len == -ENAMETOOLONG) {
			*needsCRC = 1;
			gotch = 0;
		} else {
			str_o[(*str_o_idx)++] = ILLEGAL_CHAR_MARK;
			*needsCRC = 1;
		}
	}
	return gotch;
}

static int udf_name_from_CS0(struct super_block *sb,
			     uint8_t *str_o, int str_max_len,
			     const uint8_t *ocu, int ocu_len,
			     int translate)
{
	uint32_t c;
	uint8_t cmp_id;
	int idx, len;
	int u_ch;
	int needsCRC = 0;
	int ext_i_len, ext_max_len;
	int str_o_len = 0;	/* Length of resulting output */
	int ext_o_len = 0;	/* Extension output length */
	int ext_crc_len = 0;	/* Extension output length if used with CRC */
	int i_ext = -1;		/* Extension position in input buffer */
	int o_crc = 0;		/* Rightmost possible output pos for CRC+ext */
	unsigned short valueCRC;
	uint8_t ext[EXT_SIZE * NLS_MAX_CHARSET_SIZE + 1];
	uint8_t crc[CRC_LEN];
	int (*conv_f)(wchar_t, unsigned char *, int);

	if (str_max_len <= 0)
		return 0;

	if (ocu_len == 0) {
		memset(str_o, 0, str_max_len);
		return 0;
	}

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP))
		conv_f = UDF_SB(sb)->s_nls_map->uni2char;
	else
		conv_f = NULL;

	cmp_id = ocu[0];
	if (cmp_id != 8 && cmp_id != 16) {
		memset(str_o, 0, str_max_len);
		pr_err("unknown compression code (%u)\n", cmp_id);
		return -EINVAL;
	}
	u_ch = cmp_id >> 3;

	ocu++;
	ocu_len--;

	if (ocu_len % u_ch) {
		pr_err("incorrect filename length (%d)\n", ocu_len + 1);
		return -EINVAL;
	}

	if (translate) {
		/* Look for extension */
		for (idx = ocu_len - u_ch, ext_i_len = 0;
		     (idx >= 0) && (ext_i_len < EXT_SIZE);
		     idx -= u_ch, ext_i_len++) {
			c = ocu[idx];
			if (u_ch > 1)
				c = (c << 8) | ocu[idx + 1];

			if (c == EXT_MARK) {
				if (ext_i_len)
					i_ext = idx;
				break;
			}
		}
		if (i_ext >= 0) {
			/* Convert extension */
			ext_max_len = min_t(int, sizeof(ext), str_max_len);
			ext[ext_o_len++] = EXT_MARK;
			idx = i_ext + u_ch;
			while (udf_name_conv_char(ext, ext_max_len, &ext_o_len,
						  ocu, ocu_len, &idx,
						  u_ch, &needsCRC,
						  conv_f, translate)) {
				if ((ext_o_len + CRC_LEN) < str_max_len)
					ext_crc_len = ext_o_len;
			}
		}
	}

	idx = 0;
	while (1) {
		if (translate && (idx == i_ext)) {
			if (str_o_len > (str_max_len - ext_o_len))
				needsCRC = 1;
			break;
		}

		if (!udf_name_conv_char(str_o, str_max_len, &str_o_len,
					ocu, ocu_len, &idx,
					u_ch, &needsCRC, conv_f, translate))
			break;

		if (translate &&
		    (str_o_len <= (str_max_len - ext_o_len - CRC_LEN)))
			o_crc = str_o_len;
	}

	if (translate) {
		if (str_o_len <= 2 && str_o[0] == '.' &&
		    (str_o_len == 1 || str_o[1] == '.'))
			needsCRC = 1;
		if (needsCRC) {
			str_o_len = o_crc;
			valueCRC = crc_itu_t(0, ocu, ocu_len);
			crc[0] = CRC_MARK;
			crc[1] = hex_asc_upper_hi(valueCRC >> 8);
			crc[2] = hex_asc_upper_lo(valueCRC >> 8);
			crc[3] = hex_asc_upper_hi(valueCRC);
			crc[4] = hex_asc_upper_lo(valueCRC);
			len = min_t(int, CRC_LEN, str_max_len - str_o_len);
			memcpy(&str_o[str_o_len], crc, len);
			str_o_len += len;
			ext_o_len = ext_crc_len;
		}
		if (ext_o_len > 0) {
			memcpy(&str_o[str_o_len], ext, ext_o_len);
			str_o_len += ext_o_len;
		}
	}

	return str_o_len;
}

static int udf_name_to_CS0(struct super_block *sb,
			   uint8_t *ocu, int ocu_max_len,
			   const uint8_t *str_i, int str_len)
{
	int i, len;
	unsigned int max_val;
	int u_len, u_ch;
	unicode_t uni_char;
	int (*conv_f)(const unsigned char *, int, wchar_t *);

	if (ocu_max_len <= 0)
		return 0;

	if (UDF_QUERY_FLAG(sb, UDF_FLAG_NLS_MAP))
		conv_f = UDF_SB(sb)->s_nls_map->char2uni;
	else
		conv_f = NULL;

	memset(ocu, 0, ocu_max_len);
	ocu[0] = 8;
	max_val = 0xff;
	u_ch = 1;

try_again:
	u_len = 1;
	for (i = 0; i < str_len; i += len) {
		/* Name didn't fit? */
		if (u_len + u_ch > ocu_max_len)
			return 0;
		if (conv_f) {
			wchar_t wchar;

			len = conv_f(&str_i[i], str_len - i, &wchar);
			if (len > 0)
				uni_char = wchar;
		} else {
			len = utf8_to_utf32(&str_i[i], str_len - i,
					    &uni_char);
		}
		/* Invalid character, deal with it */
		if (len <= 0 || uni_char > UNICODE_MAX) {
			len = 1;
			uni_char = '?';
		}

		if (uni_char > max_val) {
			unicode_t c;

			if (max_val == 0xff) {
				max_val = 0xffff;
				ocu[0] = 0x10;
				u_ch = 2;
				goto try_again;
			}
			/*
			 * Use UTF-16 encoding for chars outside we
			 * cannot encode directly.
			 */
			if (u_len + 2 * u_ch > ocu_max_len)
				return 0;

			uni_char -= PLANE_SIZE;
			c = SURROGATE_PAIR |
			    ((uni_char >> SURROGATE_CHAR_BITS) &
			     SURROGATE_CHAR_MASK);
			ocu[u_len++] = (uint8_t)(c >> 8);
			ocu[u_len++] = (uint8_t)(c & 0xff);
			uni_char = SURROGATE_PAIR | SURROGATE_LOW |
					(uni_char & SURROGATE_CHAR_MASK);
		}

		if (max_val == 0xffff)
			ocu[u_len++] = (uint8_t)(uni_char >> 8);
		ocu[u_len++] = (uint8_t)(uni_char & 0xff);
	}

	return u_len;
}

/*
 * Convert CS0 dstring to output charset. Warning: This function may truncate
 * input string if it is too long as it is used for informational strings only
 * and it is better to truncate the string than to refuse mounting a media.
 */
int udf_dstrCS0toChar(struct super_block *sb, uint8_t *utf_o, int o_len,
		      const uint8_t *ocu_i, int i_len)
{
	int s_len = 0;

	if (i_len > 0) {
		s_len = ocu_i[i_len - 1];
		if (s_len >= i_len) {
			pr_warn("incorrect dstring lengths (%d/%d),"
				" truncating\n", s_len, i_len);
			s_len = i_len - 1;
			/* 2-byte encoding? Need to round properly... */
			if (ocu_i[0] == 16)
				s_len -= (s_len - 1) & 2;
		}
	}

	return udf_name_from_CS0(sb, utf_o, o_len, ocu_i, s_len, 0);
}

int udf_get_filename(struct super_block *sb, const uint8_t *sname, int slen,
		     uint8_t *dname, int dlen)
{
	int ret;

	if (!slen)
		return -EIO;

	if (dlen <= 0)
		return 0;

	ret = udf_name_from_CS0(sb, dname, dlen, sname, slen, 1);
	/* Zero length filename isn't valid... */
	if (ret == 0)
		ret = -EINVAL;
	return ret;
}

int udf_put_filename(struct super_block *sb, const uint8_t *sname, int slen,
		     uint8_t *dname, int dlen)
{
	return udf_name_to_CS0(sb, dname, dlen, sname, slen);
}

