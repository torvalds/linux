// SPDX-License-Identifier: GPL-2.0
/*
 * QNX6 file system, Linux implementation.
 *
 * Version : 1.0.0
 *
 * History :
 *
 * 01-02-2012 by Kai Bankett (chaosman@ontika.net) : first release.
 * 16-02-2012 pagemap extension by Al Viro
 *
 */

#include "qnx6.h"

static unsigned qnx6_lfile_checksum(char *name, unsigned size)
{
	unsigned crc = 0;
	char *end = name + size;
	while (name < end) {
		crc = ((crc >> 1) + *(name++)) ^
			((crc & 0x00000001) ? 0x80000000 : 0);
	}
	return crc;
}

static void *qnx6_get_folio(struct inode *dir, unsigned long n,
		struct folio **foliop)
{
	struct folio *folio = read_mapping_folio(dir->i_mapping, n, NULL);

	if (IS_ERR(folio))
		return folio;
	*foliop = folio;
	return kmap_local_folio(folio, 0);
}

static unsigned last_entry(struct inode *inode, unsigned long page_nr)
{
	unsigned long last_byte = inode->i_size;
	last_byte -= page_nr << PAGE_SHIFT;
	if (last_byte > PAGE_SIZE)
		last_byte = PAGE_SIZE;
	return last_byte / QNX6_DIR_ENTRY_SIZE;
}

static struct qnx6_long_filename *qnx6_longname(struct super_block *sb,
					 struct qnx6_long_dir_entry *de,
					 struct folio **foliop)
{
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	u32 s = fs32_to_cpu(sbi, de->de_long_inode); /* in block units */
	u32 n = s >> (PAGE_SHIFT - sb->s_blocksize_bits); /* in pages */
	u32 offs;
	struct address_space *mapping = sbi->longfile->i_mapping;
	struct folio *folio = read_mapping_folio(mapping, n, NULL);

	if (IS_ERR(folio))
		return ERR_CAST(folio);
	offs = offset_in_folio(folio, s << sb->s_blocksize_bits);
	*foliop = folio;
	return kmap_local_folio(folio, offs);
}

static int qnx6_dir_longfilename(struct inode *inode,
			struct qnx6_long_dir_entry *de,
			struct dir_context *ctx,
			unsigned de_inode)
{
	struct qnx6_long_filename *lf;
	struct super_block *s = inode->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct folio *folio;
	int lf_size;

	if (de->de_size != 0xff) {
		/* error - long filename entries always have size 0xff
		   in direntry */
		pr_err("invalid direntry size (%i).\n", de->de_size);
		return 0;
	}
	lf = qnx6_longname(s, de, &folio);
	if (IS_ERR(lf)) {
		pr_err("Error reading longname\n");
		return 0;
	}

	lf_size = fs16_to_cpu(sbi, lf->lf_size);

	if (lf_size > QNX6_LONG_NAME_MAX) {
		pr_debug("file %s\n", lf->lf_fname);
		pr_err("Filename too long (%i)\n", lf_size);
		folio_release_kmap(folio, lf);
		return 0;
	}

	/* calc & validate longfilename checksum
	   mmi 3g filesystem does not have that checksum */
	if (!test_opt(s, MMI_FS) && fs32_to_cpu(sbi, de->de_checksum) !=
			qnx6_lfile_checksum(lf->lf_fname, lf_size))
		pr_info("long filename checksum error.\n");

	pr_debug("qnx6_readdir:%.*s inode:%u\n",
		 lf_size, lf->lf_fname, de_inode);
	if (!dir_emit(ctx, lf->lf_fname, lf_size, de_inode, DT_UNKNOWN)) {
		folio_release_kmap(folio, lf);
		return 0;
	}

	folio_release_kmap(folio, lf);
	/* success */
	return 1;
}

static int qnx6_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *s = inode->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	loff_t pos = ctx->pos & ~(QNX6_DIR_ENTRY_SIZE - 1);
	unsigned long npages = dir_pages(inode);
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned offset = (pos & ~PAGE_MASK) / QNX6_DIR_ENTRY_SIZE;
	bool done = false;

	ctx->pos = pos;
	if (ctx->pos >= inode->i_size)
		return 0;

	for ( ; !done && n < npages; n++, offset = 0) {
		struct qnx6_dir_entry *de;
		struct folio *folio;
		char *kaddr = qnx6_get_folio(inode, n, &folio);
		char *limit;

		if (IS_ERR(kaddr)) {
			pr_err("%s(): read failed\n", __func__);
			ctx->pos = (n + 1) << PAGE_SHIFT;
			return PTR_ERR(kaddr);
		}
		de = (struct qnx6_dir_entry *)(kaddr + offset);
		limit = kaddr + last_entry(inode, n);
		for (; (char *)de < limit; de++, ctx->pos += QNX6_DIR_ENTRY_SIZE) {
			int size = de->de_size;
			u32 no_inode = fs32_to_cpu(sbi, de->de_inode);

			if (!no_inode || !size)
				continue;

			if (size > QNX6_SHORT_NAME_MAX) {
				/* long filename detected
				   get the filename from long filename
				   structure / block */
				if (!qnx6_dir_longfilename(inode,
					(struct qnx6_long_dir_entry *)de,
					ctx, no_inode)) {
					done = true;
					break;
				}
			} else {
				pr_debug("%s():%.*s inode:%u\n",
					 __func__, size, de->de_fname,
					 no_inode);
				if (!dir_emit(ctx, de->de_fname, size,
				      no_inode, DT_UNKNOWN)) {
					done = true;
					break;
				}
			}
		}
		folio_release_kmap(folio, kaddr);
	}
	return 0;
}

/*
 * check if the long filename is correct.
 */
static unsigned qnx6_long_match(int len, const char *name,
			struct qnx6_long_dir_entry *de, struct inode *dir)
{
	struct super_block *s = dir->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct folio *folio;
	int thislen;
	struct qnx6_long_filename *lf = qnx6_longname(s, de, &folio);

	if (IS_ERR(lf))
		return 0;

	thislen = fs16_to_cpu(sbi, lf->lf_size);
	if (len != thislen) {
		folio_release_kmap(folio, lf);
		return 0;
	}
	if (memcmp(name, lf->lf_fname, len) == 0) {
		folio_release_kmap(folio, lf);
		return fs32_to_cpu(sbi, de->de_inode);
	}
	folio_release_kmap(folio, lf);
	return 0;
}

/*
 * check if the filename is correct.
 */
static unsigned qnx6_match(struct super_block *s, int len, const char *name,
			struct qnx6_dir_entry *de)
{
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	if (memcmp(name, de->de_fname, len) == 0)
		return fs32_to_cpu(sbi, de->de_inode);
	return 0;
}


unsigned qnx6_find_ino(int len, struct inode *dir, const char *name)
{
	struct super_block *s = dir->i_sb;
	struct qnx6_inode_info *ei = QNX6_I(dir);
	struct folio *folio;
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	unsigned ino;
	struct qnx6_dir_entry *de;
	struct qnx6_long_dir_entry *lde;

	if (npages == 0)
		return 0;
	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;

	do {
		de = qnx6_get_folio(dir, n, &folio);
		if (!IS_ERR(de)) {
			int limit = last_entry(dir, n);
			int i;

			for (i = 0; i < limit; i++, de++) {
				if (len <= QNX6_SHORT_NAME_MAX) {
					/* short filename */
					if (len != de->de_size)
						continue;
					ino = qnx6_match(s, len, name, de);
					if (ino)
						goto found;
				} else if (de->de_size == 0xff) {
					/* deal with long filename */
					lde = (struct qnx6_long_dir_entry *)de;
					ino = qnx6_long_match(len,
								name, lde, dir);
					if (ino)
						goto found;
				} else
					pr_err("undefined filename size in inode.\n");
			}
			folio_release_kmap(folio, de - i);
		}

		if (++n >= npages)
			n = 0;
	} while (n != start);
	return 0;

found:
	ei->i_dir_start_lookup = n;
	folio_release_kmap(folio, de);
	return ino;
}

const struct file_operations qnx6_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= qnx6_readdir,
	.fsync		= generic_file_fsync,
};

const struct inode_operations qnx6_dir_inode_operations = {
	.lookup		= qnx6_lookup,
};
