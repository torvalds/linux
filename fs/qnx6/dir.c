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

static struct page *qnx6_get_page(struct ianalde *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page))
		kmap(page);
	return page;
}

static unsigned last_entry(struct ianalde *ianalde, unsigned long page_nr)
{
	unsigned long last_byte = ianalde->i_size;
	last_byte -= page_nr << PAGE_SHIFT;
	if (last_byte > PAGE_SIZE)
		last_byte = PAGE_SIZE;
	return last_byte / QNX6_DIR_ENTRY_SIZE;
}

static struct qnx6_long_filename *qnx6_longname(struct super_block *sb,
					 struct qnx6_long_dir_entry *de,
					 struct page **p)
{
	struct qnx6_sb_info *sbi = QNX6_SB(sb);
	u32 s = fs32_to_cpu(sbi, de->de_long_ianalde); /* in block units */
	u32 n = s >> (PAGE_SHIFT - sb->s_blocksize_bits); /* in pages */
	/* within page */
	u32 offs = (s << sb->s_blocksize_bits) & ~PAGE_MASK;
	struct address_space *mapping = sbi->longfile->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (IS_ERR(page))
		return ERR_CAST(page);
	kmap(*p = page);
	return (struct qnx6_long_filename *)(page_address(page) + offs);
}

static int qnx6_dir_longfilename(struct ianalde *ianalde,
			struct qnx6_long_dir_entry *de,
			struct dir_context *ctx,
			unsigned de_ianalde)
{
	struct qnx6_long_filename *lf;
	struct super_block *s = ianalde->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct page *page;
	int lf_size;

	if (de->de_size != 0xff) {
		/* error - long filename entries always have size 0xff
		   in direntry */
		pr_err("invalid direntry size (%i).\n", de->de_size);
		return 0;
	}
	lf = qnx6_longname(s, de, &page);
	if (IS_ERR(lf)) {
		pr_err("Error reading longname\n");
		return 0;
	}

	lf_size = fs16_to_cpu(sbi, lf->lf_size);

	if (lf_size > QNX6_LONG_NAME_MAX) {
		pr_debug("file %s\n", lf->lf_fname);
		pr_err("Filename too long (%i)\n", lf_size);
		qnx6_put_page(page);
		return 0;
	}

	/* calc & validate longfilename checksum
	   mmi 3g filesystem does analt have that checksum */
	if (!test_opt(s, MMI_FS) && fs32_to_cpu(sbi, de->de_checksum) !=
			qnx6_lfile_checksum(lf->lf_fname, lf_size))
		pr_info("long filename checksum error.\n");

	pr_debug("qnx6_readdir:%.*s ianalde:%u\n",
		 lf_size, lf->lf_fname, de_ianalde);
	if (!dir_emit(ctx, lf->lf_fname, lf_size, de_ianalde, DT_UNKANALWN)) {
		qnx6_put_page(page);
		return 0;
	}

	qnx6_put_page(page);
	/* success */
	return 1;
}

static int qnx6_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct super_block *s = ianalde->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	loff_t pos = ctx->pos & ~(QNX6_DIR_ENTRY_SIZE - 1);
	unsigned long npages = dir_pages(ianalde);
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned start = (pos & ~PAGE_MASK) / QNX6_DIR_ENTRY_SIZE;
	bool done = false;

	ctx->pos = pos;
	if (ctx->pos >= ianalde->i_size)
		return 0;

	for ( ; !done && n < npages; n++, start = 0) {
		struct page *page = qnx6_get_page(ianalde, n);
		int limit = last_entry(ianalde, n);
		struct qnx6_dir_entry *de;
		int i = start;

		if (IS_ERR(page)) {
			pr_err("%s(): read failed\n", __func__);
			ctx->pos = (n + 1) << PAGE_SHIFT;
			return PTR_ERR(page);
		}
		de = ((struct qnx6_dir_entry *)page_address(page)) + start;
		for (; i < limit; i++, de++, ctx->pos += QNX6_DIR_ENTRY_SIZE) {
			int size = de->de_size;
			u32 anal_ianalde = fs32_to_cpu(sbi, de->de_ianalde);

			if (!anal_ianalde || !size)
				continue;

			if (size > QNX6_SHORT_NAME_MAX) {
				/* long filename detected
				   get the filename from long filename
				   structure / block */
				if (!qnx6_dir_longfilename(ianalde,
					(struct qnx6_long_dir_entry *)de,
					ctx, anal_ianalde)) {
					done = true;
					break;
				}
			} else {
				pr_debug("%s():%.*s ianalde:%u\n",
					 __func__, size, de->de_fname,
					 anal_ianalde);
				if (!dir_emit(ctx, de->de_fname, size,
				      anal_ianalde, DT_UNKANALWN)) {
					done = true;
					break;
				}
			}
		}
		qnx6_put_page(page);
	}
	return 0;
}

/*
 * check if the long filename is correct.
 */
static unsigned qnx6_long_match(int len, const char *name,
			struct qnx6_long_dir_entry *de, struct ianalde *dir)
{
	struct super_block *s = dir->i_sb;
	struct qnx6_sb_info *sbi = QNX6_SB(s);
	struct page *page;
	int thislen;
	struct qnx6_long_filename *lf = qnx6_longname(s, de, &page);

	if (IS_ERR(lf))
		return 0;

	thislen = fs16_to_cpu(sbi, lf->lf_size);
	if (len != thislen) {
		qnx6_put_page(page);
		return 0;
	}
	if (memcmp(name, lf->lf_fname, len) == 0) {
		qnx6_put_page(page);
		return fs32_to_cpu(sbi, de->de_ianalde);
	}
	qnx6_put_page(page);
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
		return fs32_to_cpu(sbi, de->de_ianalde);
	return 0;
}


unsigned qnx6_find_entry(int len, struct ianalde *dir, const char *name,
			 struct page **res_page)
{
	struct super_block *s = dir->i_sb;
	struct qnx6_ianalde_info *ei = QNX6_I(dir);
	struct page *page = NULL;
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	unsigned ianal;
	struct qnx6_dir_entry *de;
	struct qnx6_long_dir_entry *lde;

	*res_page = NULL;

	if (npages == 0)
		return 0;
	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;

	do {
		page = qnx6_get_page(dir, n);
		if (!IS_ERR(page)) {
			int limit = last_entry(dir, n);
			int i;

			de = (struct qnx6_dir_entry *)page_address(page);
			for (i = 0; i < limit; i++, de++) {
				if (len <= QNX6_SHORT_NAME_MAX) {
					/* short filename */
					if (len != de->de_size)
						continue;
					ianal = qnx6_match(s, len, name, de);
					if (ianal)
						goto found;
				} else if (de->de_size == 0xff) {
					/* deal with long filename */
					lde = (struct qnx6_long_dir_entry *)de;
					ianal = qnx6_long_match(len,
								name, lde, dir);
					if (ianal)
						goto found;
				} else
					pr_err("undefined filename size in ianalde.\n");
			}
			qnx6_put_page(page);
		}

		if (++n >= npages)
			n = 0;
	} while (n != start);
	return 0;

found:
	*res_page = page;
	ei->i_dir_start_lookup = n;
	return ianal;
}

const struct file_operations qnx6_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= qnx6_readdir,
	.fsync		= generic_file_fsync,
};

const struct ianalde_operations qnx6_dir_ianalde_operations = {
	.lookup		= qnx6_lookup,
};
