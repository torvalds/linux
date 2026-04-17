// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS Unicode string handling.
 *
 * Copyright (c) 2001-2006 Anton Altaparmakov
 */

#include "ntfs.h"

/*
 * IMPORTANT
 * =========
 *
 * All these routines assume that the Unicode characters are in little endian
 * encoding inside the strings!!!
 */

/*
 * This is used by the name collation functions to quickly determine what
 * characters are (in)valid.
 */
static const u8 legal_ansi_char_array[0x40] = {
	0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,

	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,

	0x17, 0x07, 0x18, 0x17, 0x17, 0x17, 0x17, 0x17,
	0x17, 0x17, 0x18, 0x16, 0x16, 0x17, 0x07, 0x00,

	0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
	0x17, 0x17, 0x04, 0x16, 0x18, 0x16, 0x18, 0x18,
};

/*
 * ntfs_are_names_equal - compare two Unicode names for equality
 * @s1:			name to compare to @s2
 * @s1_len:		length in Unicode characters of @s1
 * @s2:			name to compare to @s1
 * @s2_len:		length in Unicode characters of @s2
 * @ic:			ignore case bool
 * @upcase:		upcase table (only if @ic == IGNORE_CASE)
 * @upcase_size:	length in Unicode characters of @upcase (if present)
 *
 * Compare the names @s1 and @s2 and return 'true' (1) if the names are
 * identical, or 'false' (0) if they are not identical. If @ic is IGNORE_CASE,
 * the @upcase table is used to performa a case insensitive comparison.
 */
bool ntfs_are_names_equal(const __le16 *s1, size_t s1_len,
		const __le16 *s2, size_t s2_len, const u32 ic,
		const __le16 *upcase, const u32 upcase_size)
{
	if (s1_len != s2_len)
		return false;
	if (ic == CASE_SENSITIVE)
		return !ntfs_ucsncmp(s1, s2, s1_len);
	return !ntfs_ucsncasecmp(s1, s2, s1_len, upcase, upcase_size);
}

/*
 * ntfs_collate_names - collate two Unicode names
 * @name1:	first Unicode name to compare
 * @name1_len:	first Unicode name length
 * @name2:	second Unicode name to compare
 * @name2_len:	second Unicode name length
 * @err_val:	if @name1 contains an invalid character return this value
 * @ic:		either CASE_SENSITIVE or IGNORE_CASE
 * @upcase:	upcase table (ignored if @ic is CASE_SENSITIVE)
 * @upcase_len:	upcase table size (ignored if @ic is CASE_SENSITIVE)
 *
 * ntfs_collate_names collates two Unicode names and returns:
 *
 *  -1 if the first name collates before the second one,
 *   0 if the names match,
 *   1 if the second name collates before the first one, or
 * @err_val if an invalid character is found in @name1 during the comparison.
 *
 * The following characters are considered invalid: '"', '*', '<', '>' and '?'.
 */
int ntfs_collate_names(const __le16 *name1, const u32 name1_len,
		const __le16 *name2, const u32 name2_len,
		const int err_val, const u32 ic,
		const __le16 *upcase, const u32 upcase_len)
{
	u32 cnt, min_len;
	u16 c1, c2;

	min_len = name1_len;
	if (name1_len > name2_len)
		min_len = name2_len;
	for (cnt = 0; cnt < min_len; ++cnt) {
		c1 = le16_to_cpu(*name1++);
		c2 = le16_to_cpu(*name2++);
		if (ic) {
			if (c1 < upcase_len)
				c1 = le16_to_cpu(upcase[c1]);
			if (c2 < upcase_len)
				c2 = le16_to_cpu(upcase[c2]);
		}
		if (c1 < 64 && legal_ansi_char_array[c1] & 8)
			return err_val;
		if (c1 < c2)
			return -1;
		if (c1 > c2)
			return 1;
	}
	if (name1_len < name2_len)
		return -1;
	if (name1_len == name2_len)
		return 0;
	/* name1_len > name2_len */
	c1 = le16_to_cpu(*name1);
	if (c1 < 64 && legal_ansi_char_array[c1] & 8)
		return err_val;
	return 1;
}

/*
 * ntfs_ucsncmp - compare two little endian Unicode strings
 * @s1:		first string
 * @s2:		second string
 * @n:		maximum unicode characters to compare
 *
 * Compare the first @n characters of the Unicode strings @s1 and @s2,
 * The strings in little endian format and appropriate le16_to_cpu()
 * conversion is performed on non-little endian machines.
 *
 * The function returns an integer less than, equal to, or greater than zero
 * if @s1 (or the first @n Unicode characters thereof) is found, respectively,
 * to be less than, to match, or be greater than @s2.
 */
int ntfs_ucsncmp(const __le16 *s1, const __le16 *s2, size_t n)
{
	u16 c1, c2;
	size_t i;

	for (i = 0; i < n; ++i) {
		c1 = le16_to_cpu(s1[i]);
		c2 = le16_to_cpu(s2[i]);
		if (c1 < c2)
			return -1;
		if (c1 > c2)
			return 1;
		if (!c1)
			break;
	}
	return 0;
}

/*
 * ntfs_ucsncasecmp - compare two little endian Unicode strings, ignoring case
 * @s1:			first string
 * @s2:			second string
 * @n:			maximum unicode characters to compare
 * @upcase:		upcase table
 * @upcase_size:	upcase table size in Unicode characters
 *
 * Compare the first @n characters of the Unicode strings @s1 and @s2,
 * ignoring case. The strings in little endian format and appropriate
 * le16_to_cpu() conversion is performed on non-little endian machines.
 *
 * Each character is uppercased using the @upcase table before the comparison.
 *
 * The function returns an integer less than, equal to, or greater than zero
 * if @s1 (or the first @n Unicode characters thereof) is found, respectively,
 * to be less than, to match, or be greater than @s2.
 */
int ntfs_ucsncasecmp(const __le16 *s1, const __le16 *s2, size_t n,
		const __le16 *upcase, const u32 upcase_size)
{
	size_t i;
	u16 c1, c2;

	for (i = 0; i < n; ++i) {
		c1 = le16_to_cpu(s1[i]);
		if (c1 < upcase_size)
			c1 = le16_to_cpu(upcase[c1]);
		c2 = le16_to_cpu(s2[i]);
		if (c2 < upcase_size)
			c2 = le16_to_cpu(upcase[c2]);
		if (c1 < c2)
			return -1;
		if (c1 > c2)
			return 1;
		if (!c1)
			break;
	}
	return 0;
}

int ntfs_file_compare_values(const struct file_name_attr *file_name_attr1,
		const struct file_name_attr *file_name_attr2,
		const int err_val, const u32 ic,
		const __le16 *upcase, const u32 upcase_len)
{
	return ntfs_collate_names((__le16 *)&file_name_attr1->file_name,
			file_name_attr1->file_name_length,
			(__le16 *)&file_name_attr2->file_name,
			file_name_attr2->file_name_length,
			err_val, ic, upcase, upcase_len);
}

/*
 * ntfs_nlstoucs - convert NLS string to little endian Unicode string
 * @vol:	ntfs volume which we are working with
 * @ins:	input NLS string buffer
 * @ins_len:	length of input string in bytes
 * @outs:	on return contains the allocated output Unicode string buffer
 * @max_name_len: maximum number of Unicode characters allowed for the output name
 *
 * Convert the input string @ins, which is in whatever format the loaded NLS
 * map dictates, into a little endian, 2-byte Unicode string.
 *
 * This function allocates the string and the caller is responsible for
 * calling kmem_cache_free(ntfs_name_cache, *@outs); when finished with it.
 *
 * On success the function returns the number of Unicode characters written to
 * the output string *@outs (>= 0), not counting the terminating Unicode NULL
 * character. *@outs is set to the allocated output string buffer.
 *
 * On error, a negative number corresponding to the error code is returned. In
 * that case the output string is not allocated. Both *@outs and *@outs_len
 * are then undefined.
 *
 * This might look a bit odd due to fast path optimization...
 */
int ntfs_nlstoucs(const struct ntfs_volume *vol, const char *ins,
		const int ins_len, __le16 **outs, int max_name_len)
{
	struct nls_table *nls = vol->nls_map;
	__le16 *ucs;
	wchar_t wc;
	int i, o, wc_len;

	/* We do not trust outside sources. */
	if (likely(ins)) {
		if (max_name_len > NTFS_MAX_NAME_LEN)
			ucs = kvmalloc((max_name_len + 2) * sizeof(__le16),
				       GFP_NOFS | __GFP_ZERO);
		else
			ucs = kmem_cache_alloc(ntfs_name_cache, GFP_NOFS);
		if (likely(ucs)) {
			if (vol->nls_utf8) {
				o = utf8s_to_utf16s(ins, ins_len,
						    UTF16_LITTLE_ENDIAN,
						    (wchar_t *)ucs,
						    max_name_len + 2);
				if (o < 0 || o > max_name_len) {
					wc_len = o;
					goto name_err;
				}
			} else {
				for (i = o = 0; i < ins_len; i += wc_len) {
					wc_len = nls->char2uni(ins + i, ins_len - i,
							&wc);
					if (likely(wc_len >= 0 &&
					    o < max_name_len)) {
						if (likely(wc)) {
							ucs[o++] = cpu_to_le16(wc);
							continue;
						} /* else if (!wc) */
						break;
					}

					goto name_err;
				}
			}
			ucs[o] = 0;
			*outs = ucs;
			return o;
		} /* else if (!ucs) */
		ntfs_debug("Failed to allocate buffer for converted name from ntfs_name_cache.");
		return -ENOMEM;
	} /* else if (!ins) */
	ntfs_error(vol->sb, "Received NULL pointer.");
	return -EINVAL;
name_err:
	if (max_name_len > NTFS_MAX_NAME_LEN)
		kvfree(ucs);
	else
		kmem_cache_free(ntfs_name_cache, ucs);
	if (wc_len < 0) {
		ntfs_debug("Name using character set %s contains characters that cannot be converted to Unicode.",
				nls->charset);
		i = -EILSEQ;
	} else {
		ntfs_debug("Name is too long (maximum length for a name on NTFS is %d Unicode characters.",
				max_name_len);
		i = -ENAMETOOLONG;
	}
	return i;
}

/*
 * ntfs_ucstonls - convert little endian Unicode string to NLS string
 * @vol:	ntfs volume which we are working with
 * @ins:	input Unicode string buffer
 * @ins_len:	length of input string in Unicode characters
 * @outs:	on return contains the (allocated) output NLS string buffer
 * @outs_len:	length of output string buffer in bytes
 *
 * Convert the input little endian, 2-byte Unicode string @ins, of length
 * @ins_len into the string format dictated by the loaded NLS.
 *
 * If *@outs is NULL, this function allocates the string and the caller is
 * responsible for calling kfree(*@outs); when finished with it. In this case
 * @outs_len is ignored and can be 0.
 *
 * On success the function returns the number of bytes written to the output
 * string *@outs (>= 0), not counting the terminating NULL byte. If the output
 * string buffer was allocated, *@outs is set to it.
 *
 * On error, a negative number corresponding to the error code is returned. In
 * that case the output string is not allocated. The contents of *@outs are
 * then undefined.
 *
 * This might look a bit odd due to fast path optimization...
 */
int ntfs_ucstonls(const struct ntfs_volume *vol, const __le16 *ins,
		const int ins_len, unsigned char **outs, int outs_len)
{
	struct nls_table *nls = vol->nls_map;
	unsigned char *ns;
	int i, o, ns_len, wc;

	/* We don't trust outside sources. */
	if (ins) {
		ns = *outs;
		ns_len = outs_len;
		if (ns && !ns_len) {
			wc = -ENAMETOOLONG;
			goto conversion_err;
		}
		if (!ns) {
			ns_len = ins_len * NLS_MAX_CHARSET_SIZE;
			ns = kmalloc(ns_len + 1, GFP_NOFS);
			if (!ns)
				goto mem_err_out;
		}

		if (vol->nls_utf8) {
			o = utf16s_to_utf8s((const wchar_t *)ins, ins_len,
					UTF16_LITTLE_ENDIAN, ns, ns_len);
			if (o >= ns_len) {
				wc = -ENAMETOOLONG;
				goto conversion_err;
			}
			goto done;
		}

		for (i = o = 0; i < ins_len; i++) {
retry:
			wc = nls->uni2char(le16_to_cpu(ins[i]), ns + o,
					ns_len - o);
			if (wc > 0) {
				o += wc;
				continue;
			} else if (!wc)
				break;
			else if (wc == -ENAMETOOLONG && ns != *outs) {
				unsigned char *tc;
				/* Grow in multiples of 64 bytes. */
				tc = kmalloc((ns_len + 64) &
						~63, GFP_NOFS);
				if (tc) {
					memcpy(tc, ns, ns_len);
					ns_len = ((ns_len + 64) & ~63) - 1;
					kfree(ns);
					ns = tc;
					goto retry;
				} /* No memory so goto conversion_error; */
			} /* wc < 0, real error. */
			goto conversion_err;
		}
done:
		ns[o] = 0;
		*outs = ns;
		return o;
	} /* else (!ins) */
	ntfs_error(vol->sb, "Received NULL pointer.");
	return -EINVAL;
conversion_err:
	ntfs_error(vol->sb,
		"Unicode name contains characters that cannot be converted to character set %s.  You might want to try to use the mount option nls=utf8.",
		nls->charset);
	if (ns != *outs)
		kfree(ns);
	if (wc != -ENAMETOOLONG)
		wc = -EILSEQ;
	return wc;
mem_err_out:
	ntfs_error(vol->sb, "Failed to allocate name!");
	return -ENOMEM;
}

/*
 * ntfs_ucsnlen - determine the length of a little endian Unicode string
 * @s:		pointer to Unicode string
 * @maxlen:	maximum length of string @s
 *
 * Return the number of Unicode characters in the little endian Unicode
 * string @s up to a maximum of maxlen Unicode characters, not including
 * the terminating (__le16)'\0'. If there is no (__le16)'\0' between @s
 * and @s + @maxlen, @maxlen is returned.
 *
 * This function never looks beyond @s + @maxlen.
 */
static u32 ntfs_ucsnlen(const __le16 *s, u32 maxlen)
{
	u32 i;

	for (i = 0; i < maxlen; i++) {
		if (!le16_to_cpu(s[i]))
			break;
	}
	return i;
}

/*
 * ntfs_ucsndup - duplicate little endian Unicode string
 * @s:		pointer to Unicode string
 * @maxlen:	maximum length of string @s
 *
 * Return a pointer to a new little endian Unicode string which is a duplicate
 * of the string s.  Memory for the new string is obtained with kmalloc,
 * and can be freed with kfree.
 *
 * A maximum of @maxlen Unicode characters are copied and a terminating
 * (__le16)'\0' little endian Unicode character is added.
 *
 * This function never looks beyond @s + @maxlen.
 *
 * Return a pointer to the new little endian Unicode string on success and NULL
 * on failure with errno set to the error code.
 */
__le16 *ntfs_ucsndup(const __le16 *s, u32 maxlen)
{
	__le16 *dst;
	u32 len;

	len = ntfs_ucsnlen(s, maxlen);
	dst = kmalloc((len + 1) * sizeof(__le16), GFP_NOFS);
	if (dst) {
		memcpy(dst, s, len * sizeof(__le16));
		dst[len] = cpu_to_le16(L'\0');
	}
	return dst;
}

/*
 * ntfs_names_are_equal - compare two Unicode names for equality
 * @s1:                 name to compare to @s2
 * @s1_len:             length in Unicode characters of @s1
 * @s2:                 name to compare to @s1
 * @s2_len:             length in Unicode characters of @s2
 * @ic:                 ignore case bool
 * @upcase:             upcase table (only if @ic == IGNORE_CASE)
 * @upcase_size:        length in Unicode characters of @upcase (if present)
 *
 * Compare the names @s1 and @s2 and return TRUE (1) if the names are
 * identical, or FALSE (0) if they are not identical. If @ic is IGNORE_CASE,
 * the @upcase table is used to perform a case insensitive comparison.
 */
bool ntfs_names_are_equal(const __le16 *s1, size_t s1_len,
		const __le16 *s2, size_t s2_len,
		const u32 ic,
		const __le16 *upcase, const u32 upcase_size)
{
	if (s1_len != s2_len)
		return false;
	if (!s1_len)
		return true;
	if (ic == CASE_SENSITIVE)
		return ntfs_ucsncmp(s1, s2, s1_len) ? false : true;
	return ntfs_ucsncasecmp(s1, s2, s1_len, upcase, upcase_size) ? false : true;
}
