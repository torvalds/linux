// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 *  directory handling functions for ntfs-based filesystems
 *
 */
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/iversion.h>
#include <linux/nls.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * Convert little endian utf16 to nls string
 */
int ntfs_utf16_to_nls(struct ntfs_sb_info *sbi, const struct le_str *uni,
		      u8 *buf, int buf_len)
{
	int ret, uni_len, warn;
	const __le16 *ip;
	u8 *op;
	struct nls_table *nls = sbi->options.nls;

	static_assert(sizeof(wchar_t) == sizeof(__le16));

	if (!nls) {
		/* utf16 -> utf8 */
		ret = utf16s_to_utf8s((wchar_t *)uni->name, uni->len,
				      UTF16_LITTLE_ENDIAN, buf, buf_len);
		buf[ret] = '\0';
		return ret;
	}

	ip = uni->name;
	op = buf;
	uni_len = uni->len;
	warn = 0;

	while (uni_len--) {
		u16 ec;
		int charlen;
		char dump[5];

		if (buf_len < NLS_MAX_CHARSET_SIZE) {
			ntfs_warn(sbi->sb,
				  "filename was truncated while converting.");
			break;
		}

		ec = le16_to_cpu(*ip++);
		charlen = nls->uni2char(ec, op, buf_len);

		if (charlen > 0) {
			op += charlen;
			buf_len -= charlen;
			continue;
		}

		*op++ = '_';
		buf_len -= 1;
		if (warn)
			continue;

		warn = 1;
		hex_byte_pack(&dump[0], ec >> 8);
		hex_byte_pack(&dump[2], ec);
		dump[4] = 0;

		ntfs_err(sbi->sb, "failed to convert \"%s\" to %s", dump,
			 nls->charset);
	}

	*op = '\0';
	return op - buf;
}

// clang-format off
#define PLANE_SIZE	0x00010000

#define SURROGATE_PAIR	0x0000d800
#define SURROGATE_LOW	0x00000400
#define SURROGATE_BITS	0x000003ff
// clang-format on

/*
 * modified version of put_utf16 from fs/nls/nls_base.c
 * is sparse warnings free
 */
static inline void put_utf16(wchar_t *s, unsigned int c,
			     enum utf16_endian endian)
{
	static_assert(sizeof(wchar_t) == sizeof(__le16));
	static_assert(sizeof(wchar_t) == sizeof(__be16));

	switch (endian) {
	default:
		*s = (wchar_t)c;
		break;
	case UTF16_LITTLE_ENDIAN:
		*(__le16 *)s = __cpu_to_le16(c);
		break;
	case UTF16_BIG_ENDIAN:
		*(__be16 *)s = __cpu_to_be16(c);
		break;
	}
}

/*
 * modified version of 'utf8s_to_utf16s' allows to
 * detect -ENAMETOOLONG without writing out of expected maximum
 */
static int _utf8s_to_utf16s(const u8 *s, int inlen, enum utf16_endian endian,
			    wchar_t *pwcs, int maxout)
{
	u16 *op;
	int size;
	unicode_t u;

	op = pwcs;
	while (inlen > 0 && *s) {
		if (*s & 0x80) {
			size = utf8_to_utf32(s, inlen, &u);
			if (size < 0)
				return -EINVAL;
			s += size;
			inlen -= size;

			if (u >= PLANE_SIZE) {
				if (maxout < 2)
					return -ENAMETOOLONG;

				u -= PLANE_SIZE;
				put_utf16(op++,
					  SURROGATE_PAIR |
						  ((u >> 10) & SURROGATE_BITS),
					  endian);
				put_utf16(op++,
					  SURROGATE_PAIR | SURROGATE_LOW |
						  (u & SURROGATE_BITS),
					  endian);
				maxout -= 2;
			} else {
				if (maxout < 1)
					return -ENAMETOOLONG;

				put_utf16(op++, u, endian);
				maxout--;
			}
		} else {
			if (maxout < 1)
				return -ENAMETOOLONG;

			put_utf16(op++, *s++, endian);
			inlen--;
			maxout--;
		}
	}
	return op - pwcs;
}

/*
 * Convert input string to utf16
 *
 * name, name_len - input name
 * uni, max_ulen - destination memory
 * endian - endian of target utf16 string
 *
 * This function is called:
 * - to create ntfs name
 * - to create symlink
 *
 * returns utf16 string length or error (if negative)
 */
int ntfs_nls_to_utf16(struct ntfs_sb_info *sbi, const u8 *name, u32 name_len,
		      struct cpu_str *uni, u32 max_ulen,
		      enum utf16_endian endian)
{
	int ret, slen;
	const u8 *end;
	struct nls_table *nls = sbi->options.nls;
	u16 *uname = uni->name;

	static_assert(sizeof(wchar_t) == sizeof(u16));

	if (!nls) {
		/* utf8 -> utf16 */
		ret = _utf8s_to_utf16s(name, name_len, endian, uname, max_ulen);
		uni->len = ret;
		return ret;
	}

	for (ret = 0, end = name + name_len; name < end; ret++, name += slen) {
		if (ret >= max_ulen)
			return -ENAMETOOLONG;

		slen = nls->char2uni(name, end - name, uname + ret);
		if (!slen)
			return -EINVAL;
		if (slen < 0)
			return slen;
	}

#ifdef __BIG_ENDIAN
	if (endian == UTF16_LITTLE_ENDIAN) {
		int i = ret;

		while (i--) {
			__cpu_to_le16s(uname);
			uname++;
		}
	}
#else
	if (endian == UTF16_BIG_ENDIAN) {
		int i = ret;

		while (i--) {
			__cpu_to_be16s(uname);
			uname++;
		}
	}
#endif

	uni->len = ret;
	return ret;
}

/* helper function */
struct inode *dir_search_u(struct inode *dir, const struct cpu_str *uni,
			   struct ntfs_fnd *fnd)
{
	int err = 0;
	struct super_block *sb = dir->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *ni = ntfs_i(dir);
	struct NTFS_DE *e;
	int diff;
	struct inode *inode = NULL;
	struct ntfs_fnd *fnd_a = NULL;

	if (!fnd) {
		fnd_a = fnd_get();
		if (!fnd_a) {
			err = -ENOMEM;
			goto out;
		}
		fnd = fnd_a;
	}

	err = indx_find(&ni->dir, ni, NULL, uni, 0, sbi, &diff, &e, fnd);

	if (err)
		goto out;

	if (diff) {
		err = -ENOENT;
		goto out;
	}

	inode = ntfs_iget5(sb, &e->ref, uni);
	if (!IS_ERR(inode) && is_bad_inode(inode)) {
		iput(inode);
		err = -EINVAL;
	}
out:
	fnd_put(fnd_a);

	return err == -ENOENT ? NULL : err ? ERR_PTR(err) : inode;
}

static inline int ntfs_filldir(struct ntfs_sb_info *sbi, struct ntfs_inode *ni,
			       const struct NTFS_DE *e, u8 *name,
			       struct dir_context *ctx)
{
	const struct ATTR_FILE_NAME *fname;
	unsigned long ino;
	int name_len;
	u32 dt_type;

	fname = Add2Ptr(e, sizeof(struct NTFS_DE));

	if (fname->type == FILE_NAME_DOS)
		return 0;

	if (!mi_is_ref(&ni->mi, &fname->home))
		return 0;

	ino = ino_get(&e->ref);

	if (ino == MFT_REC_ROOT)
		return 0;

	/* Skip meta files ( unless option to show metafiles is set ) */
	if (!sbi->options.showmeta && ntfs_is_meta_file(sbi, ino))
		return 0;

	if (sbi->options.nohidden && (fname->dup.fa & FILE_ATTRIBUTE_HIDDEN))
		return 0;

	name_len = ntfs_utf16_to_nls(sbi, (struct le_str *)&fname->name_len,
				     name, PATH_MAX);
	if (name_len <= 0) {
		ntfs_warn(sbi->sb, "failed to convert name for inode %lx.",
			  ino);
		return 0;
	}

	dt_type = (fname->dup.fa & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;

	return !dir_emit(ctx, (s8 *)name, name_len, ino, dt_type);
}

/*
 * ntfs_read_hdr
 *
 * helper function 'ntfs_readdir'
 */
static int ntfs_read_hdr(struct ntfs_sb_info *sbi, struct ntfs_inode *ni,
			 const struct INDEX_HDR *hdr, u64 vbo, u64 pos,
			 u8 *name, struct dir_context *ctx)
{
	int err;
	const struct NTFS_DE *e;
	u32 e_size;
	u32 end = le32_to_cpu(hdr->used);
	u32 off = le32_to_cpu(hdr->de_off);

	for (;; off += e_size) {
		if (off + sizeof(struct NTFS_DE) > end)
			return -1;

		e = Add2Ptr(hdr, off);
		e_size = le16_to_cpu(e->size);
		if (e_size < sizeof(struct NTFS_DE) || off + e_size > end)
			return -1;

		if (de_is_last(e))
			return 0;

		/* Skip already enumerated*/
		if (vbo + off < pos)
			continue;

		if (le16_to_cpu(e->key_size) < SIZEOF_ATTRIBUTE_FILENAME)
			return -1;

		ctx->pos = vbo + off;

		/* Submit the name to the filldir callback. */
		err = ntfs_filldir(sbi, ni, e, name, ctx);
		if (err)
			return err;
	}
}

/*
 * file_operations::iterate_shared
 *
 * Use non sorted enumeration.
 * We have an example of broken volume where sorted enumeration
 * counts each name twice
 */
static int ntfs_readdir(struct file *file, struct dir_context *ctx)
{
	const struct INDEX_ROOT *root;
	u64 vbo;
	size_t bit;
	loff_t eod;
	int err = 0;
	struct inode *dir = file_inode(file);
	struct ntfs_inode *ni = ntfs_i(dir);
	struct super_block *sb = dir->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	loff_t i_size = i_size_read(dir);
	u32 pos = ctx->pos;
	u8 *name = NULL;
	struct indx_node *node = NULL;
	u8 index_bits = ni->dir.index_bits;

	/* name is a buffer of PATH_MAX length */
	static_assert(NTFS_NAME_LEN * 4 < PATH_MAX);

	eod = i_size + sbi->record_size;

	if (pos >= eod)
		return 0;

	if (!dir_emit_dots(file, ctx))
		return 0;

	/* allocate PATH_MAX bytes */
	name = __getname();
	if (!name)
		return -ENOMEM;

	if (!ni->mi_loaded && ni->attr_list.size) {
		/*
		 * directory inode is locked for read
		 * load all subrecords to avoid 'write' access to 'ni' during
		 * directory reading
		 */
		ni_lock(ni);
		if (!ni->mi_loaded && ni->attr_list.size) {
			err = ni_load_all_mi(ni);
			if (!err)
				ni->mi_loaded = true;
		}
		ni_unlock(ni);
		if (err)
			goto out;
	}

	root = indx_get_root(&ni->dir, ni, NULL, NULL);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	if (pos >= sbi->record_size) {
		bit = (pos - sbi->record_size) >> index_bits;
	} else {
		err = ntfs_read_hdr(sbi, ni, &root->ihdr, 0, pos, name, ctx);
		if (err)
			goto out;
		bit = 0;
	}

	if (!i_size) {
		ctx->pos = eod;
		goto out;
	}

	for (;;) {
		vbo = (u64)bit << index_bits;
		if (vbo >= i_size) {
			ctx->pos = eod;
			goto out;
		}

		err = indx_used_bit(&ni->dir, ni, &bit);
		if (err)
			goto out;

		if (bit == MINUS_ONE_T) {
			ctx->pos = eod;
			goto out;
		}

		vbo = (u64)bit << index_bits;
		if (vbo >= i_size) {
			ntfs_inode_err(dir, "Looks like your dir is corrupt");
			err = -EINVAL;
			goto out;
		}

		err = indx_read(&ni->dir, ni, bit << ni->dir.idx2vbn_bits,
				&node);
		if (err)
			goto out;

		err = ntfs_read_hdr(sbi, ni, &node->index->ihdr,
				    vbo + sbi->record_size, pos, name, ctx);
		if (err)
			goto out;

		bit += 1;
	}

out:

	__putname(name);
	put_indx_node(node);

	if (err == -ENOENT) {
		err = 0;
		ctx->pos = pos;
	}

	return err;
}

static int ntfs_dir_count(struct inode *dir, bool *is_empty, size_t *dirs,
			  size_t *files)
{
	int err = 0;
	struct ntfs_inode *ni = ntfs_i(dir);
	struct NTFS_DE *e = NULL;
	struct INDEX_ROOT *root;
	struct INDEX_HDR *hdr;
	const struct ATTR_FILE_NAME *fname;
	u32 e_size, off, end;
	u64 vbo = 0;
	size_t drs = 0, fles = 0, bit = 0;
	loff_t i_size = ni->vfs_inode.i_size;
	struct indx_node *node = NULL;
	u8 index_bits = ni->dir.index_bits;

	if (is_empty)
		*is_empty = true;

	root = indx_get_root(&ni->dir, ni, NULL, NULL);
	if (!root)
		return -EINVAL;

	hdr = &root->ihdr;

	for (;;) {
		end = le32_to_cpu(hdr->used);
		off = le32_to_cpu(hdr->de_off);

		for (; off + sizeof(struct NTFS_DE) <= end; off += e_size) {
			e = Add2Ptr(hdr, off);
			e_size = le16_to_cpu(e->size);
			if (e_size < sizeof(struct NTFS_DE) ||
			    off + e_size > end)
				break;

			if (de_is_last(e))
				break;

			fname = de_get_fname(e);
			if (!fname)
				continue;

			if (fname->type == FILE_NAME_DOS)
				continue;

			if (is_empty) {
				*is_empty = false;
				if (!dirs && !files)
					goto out;
			}

			if (fname->dup.fa & FILE_ATTRIBUTE_DIRECTORY)
				drs += 1;
			else
				fles += 1;
		}

		if (vbo >= i_size)
			goto out;

		err = indx_used_bit(&ni->dir, ni, &bit);
		if (err)
			goto out;

		if (bit == MINUS_ONE_T)
			goto out;

		vbo = (u64)bit << index_bits;
		if (vbo >= i_size)
			goto out;

		err = indx_read(&ni->dir, ni, bit << ni->dir.idx2vbn_bits,
				&node);
		if (err)
			goto out;

		hdr = &node->index->ihdr;
		bit += 1;
		vbo = (u64)bit << ni->dir.idx2vbn_bits;
	}

out:
	put_indx_node(node);
	if (dirs)
		*dirs = drs;
	if (files)
		*files = fles;

	return err;
}

bool dir_is_empty(struct inode *dir)
{
	bool is_empty = false;

	ntfs_dir_count(dir, &is_empty, NULL, NULL);

	return is_empty;
}

// clang-format off
const struct file_operations ntfs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= ntfs_readdir,
	.fsync		= generic_file_fsync,
	.open		= ntfs_file_open,
};
// clang-format on
