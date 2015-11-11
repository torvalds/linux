/*
 *  linux/fs/isofs/namei.c
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/gfp.h>
#include "isofs.h"

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use isofs_match. No big problem. Match also makes
 * some sanity tests.
 */
static int
isofs_cmp(struct dentry *dentry, const char *compare, int dlen)
{
	struct qstr qstr;
	qstr.name = compare;
	qstr.len = dlen;
	if (likely(!dentry->d_op))
		return dentry->d_name.len != dlen || memcmp(dentry->d_name.name, compare, dlen);
	return dentry->d_op->d_compare(NULL, NULL, dentry->d_name.len, dentry->d_name.name, &qstr);
}

/*
 *	isofs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the inode number of the found entry, or 0 on error.
 */
static unsigned long
isofs_find_entry(struct inode *dir, struct dentry *dentry,
	unsigned long *block_rv, unsigned long *offset_rv,
	char *tmpname, struct iso_directory_record *tmpde)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(dir);
	unsigned char bufbits = ISOFS_BUFFER_BITS(dir);
	unsigned long block, f_pos, offset, block_saved, offset_saved;
	struct buffer_head *bh = NULL;
	struct isofs_sb_info *sbi = ISOFS_SB(dir->i_sb);

	if (!ISOFS_I(dir)->i_first_extent)
		return 0;

	f_pos = 0;
	offset = 0;
	block = 0;

	while (f_pos < dir->i_size) {
		struct iso_directory_record *de;
		int de_len, match, i, dlen;
		char *dpnt;

		if (!bh) {
			bh = isofs_bread(dir, block);
			if (!bh)
				return 0;
		}

		de = (struct iso_directory_record *) (bh->b_data + offset);

		de_len = *(unsigned char *) de;
		if (!de_len) {
			brelse(bh);
			bh = NULL;
			f_pos = (f_pos + ISOFS_BLOCK_SIZE) & ~(ISOFS_BLOCK_SIZE - 1);
			block = f_pos >> bufbits;
			offset = 0;
			continue;
		}

		block_saved = bh->b_blocknr;
		offset_saved = offset;
		offset += de_len;
		f_pos += de_len;

		/* Make sure we have a full directory entry */
		if (offset >= bufsize) {
			int slop = bufsize - offset + de_len;
			memcpy(tmpde, de, slop);
			offset &= bufsize - 1;
			block++;
			brelse(bh);
			bh = NULL;
			if (offset) {
				bh = isofs_bread(dir, block);
				if (!bh)
					return 0;
				memcpy((void *) tmpde + slop, bh->b_data, offset);
			}
			de = tmpde;
		}

		dlen = de->name_len[0];
		dpnt = de->name;
		/* Basic sanity check, whether name doesn't exceed dir entry */
		if (de_len < dlen + sizeof(struct iso_directory_record)) {
			printk(KERN_NOTICE "iso9660: Corrupted directory entry"
			       " in block %lu of inode %lu\n", block,
			       dir->i_ino);
			return 0;
		}

		if (sbi->s_rock &&
		    ((i = get_rock_ridge_filename(de, tmpname, dir)))) {
			dlen = i;	/* possibly -1 */
			dpnt = tmpname;
#ifdef CONFIG_JOLIET
		} else if (sbi->s_joliet_level) {
			dlen = get_joliet_filename(de, tmpname, dir);
			dpnt = tmpname;
#endif
		} else if (sbi->s_mapping == 'a') {
			dlen = get_acorn_filename(de, tmpname, dir);
			dpnt = tmpname;
		} else if (sbi->s_mapping == 'n') {
			dlen = isofs_name_translate(de, tmpname, dir);
			dpnt = tmpname;
		}

		/*
		 * Skip hidden or associated files unless hide or showassoc,
		 * respectively, is set
		 */
		match = 0;
		if (dlen > 0 &&
			(!sbi->s_hide ||
				(!(de->flags[-sbi->s_high_sierra] & 1))) &&
			(sbi->s_showassoc ||
				(!(de->flags[-sbi->s_high_sierra] & 4)))) {
			if (dpnt && (dlen > 1 || dpnt[0] > 1))
				match = (isofs_cmp(dentry, dpnt, dlen) == 0);
		}
		if (match) {
			isofs_normalize_block_and_offset(de,
							 &block_saved,
							 &offset_saved);
			*block_rv = block_saved;
			*offset_rv = offset_saved;
			brelse(bh);
			return 1;
		}
	}
	brelse(bh);
	return 0;
}

struct dentry *isofs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	int found;
	unsigned long uninitialized_var(block);
	unsigned long uninitialized_var(offset);
	struct inode *inode;
	struct page *page;

	page = alloc_page(GFP_USER);
	if (!page)
		return ERR_PTR(-ENOMEM);

	found = isofs_find_entry(dir, dentry,
				&block, &offset,
				page_address(page),
				1024 + page_address(page));
	__free_page(page);

	inode = found ? isofs_iget(dir->i_sb, block, offset) : NULL;

	return d_splice_alias(inode, dentry);
}
