/*
 *  linux/fs/fat/dir.c
 *
 *  directory handling functions for fat-based filesystems
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 *
 *  VFAT extensions by Gordon Chaffee <chaffee@plateau.cs.berkeley.edu>
 *  Merged with msdos fs by Henrik Storner <storner@osiris.ping.dk>
 *  Rewritten for constant inumbers. Plugged buffer overrun in readdir(). AV
 *  Short name translation 1999, 2001 by Wolfram Pienkoss <wp@bszh.de>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/compat.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include "fat.h"

/*
 * Maximum buffer size of short name.
 * [(MSDOS_NAME + '.') * max one char + nul]
 * For msdos style, ['.' (hidden) + MSDOS_NAME + '.' + nul]
 */
#define FAT_MAX_SHORT_SIZE	((MSDOS_NAME + 1) * NLS_MAX_CHARSET_SIZE + 1)
/*
 * Maximum buffer size of unicode chars from slots.
 * [(max longname slots * 13 (size in a slot) + nul) * sizeof(wchar_t)]
 */
#define FAT_MAX_UNI_CHARS	((MSDOS_SLOTS - 1) * 13 + 1)
#define FAT_MAX_UNI_SIZE	(FAT_MAX_UNI_CHARS * sizeof(wchar_t))

static inline loff_t fat_make_i_pos(struct super_block *sb,
				    struct buffer_head *bh,
				    struct msdos_dir_entry *de)
{
	return ((loff_t)bh->b_blocknr << MSDOS_SB(sb)->dir_per_block_bits)
		| (de - (struct msdos_dir_entry *)bh->b_data);
}

static inline void fat_dir_readahead(struct inode *dir, sector_t iblock,
				     sector_t phys)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh;
	int sec;

	/* This is not a first sector of cluster, or sec_per_clus == 1 */
	if ((iblock & (sbi->sec_per_clus - 1)) || sbi->sec_per_clus == 1)
		return;
	/* root dir of FAT12/FAT16 */
	if ((sbi->fat_bits != 32) && (dir->i_ino == MSDOS_ROOT_INO))
		return;

	bh = sb_find_get_block(sb, phys);
	if (bh == NULL || !buffer_uptodate(bh)) {
		for (sec = 0; sec < sbi->sec_per_clus; sec++)
			sb_breadahead(sb, phys + sec);
	}
	brelse(bh);
}

/* Returns the inode number of the directory entry at offset pos. If bh is
   non-NULL, it is brelse'd before. Pos is incremented. The buffer header is
   returned in bh.
   AV. Most often we do it item-by-item. Makes sense to optimize.
   AV. OK, there we go: if both bh and de are non-NULL we assume that we just
   AV. want the next entry (took one explicit de=NULL in vfat/namei.c).
   AV. It's done in fat_get_entry() (inlined), here the slow case lives.
   AV. Additionally, when we return -1 (i.e. reached the end of directory)
   AV. we make bh NULL.
 */
static int fat__get_entry(struct inode *dir, loff_t *pos,
			  struct buffer_head **bh, struct msdos_dir_entry **de)
{
	struct super_block *sb = dir->i_sb;
	sector_t phys, iblock;
	unsigned long mapped_blocks;
	int err, offset;

next:
	if (*bh)
		brelse(*bh);

	*bh = NULL;
	iblock = *pos >> sb->s_blocksize_bits;
	err = fat_bmap(dir, iblock, &phys, &mapped_blocks, 0);
	if (err || !phys)
		return -1;	/* beyond EOF or error */

	fat_dir_readahead(dir, iblock, phys);

	*bh = sb_bread(sb, phys);
	if (*bh == NULL) {
		printk(KERN_ERR "FAT: Directory bread(block %llu) failed\n",
		       (llu)phys);
		/* skip this block */
		*pos = (iblock + 1) << sb->s_blocksize_bits;
		goto next;
	}

	offset = *pos & (sb->s_blocksize - 1);
	*pos += sizeof(struct msdos_dir_entry);
	*de = (struct msdos_dir_entry *)((*bh)->b_data + offset);

	return 0;
}

static inline int fat_get_entry(struct inode *dir, loff_t *pos,
				struct buffer_head **bh,
				struct msdos_dir_entry **de)
{
	/* Fast stuff first */
	if (*bh && *de &&
	    (*de - (struct msdos_dir_entry *)(*bh)->b_data) < MSDOS_SB(dir->i_sb)->dir_per_block - 1) {
		*pos += sizeof(struct msdos_dir_entry);
		(*de)++;
		return 0;
	}
	return fat__get_entry(dir, pos, bh, de);
}

/*
 * Convert Unicode 16 to UTF-8, translated Unicode, or ASCII.
 * If uni_xlate is enabled and we can't get a 1:1 conversion, use a
 * colon as an escape character since it is normally invalid on the vfat
 * filesystem. The following four characters are the hexadecimal digits
 * of Unicode value. This lets us do a full dump and restore of Unicode
 * filenames. We could get into some trouble with long Unicode names,
 * but ignore that right now.
 * Ahem... Stack smashing in ring 0 isn't fun. Fixed.
 */
static int uni16_to_x8(unsigned char *ascii, const wchar_t *uni, int len,
		       int uni_xlate, struct nls_table *nls)
{
	const wchar_t *ip;
	wchar_t ec;
	unsigned char *op;
	int charlen;

	ip = uni;
	op = ascii;

	while (*ip && ((len - NLS_MAX_CHARSET_SIZE) > 0)) {
		ec = *ip++;
		if ((charlen = nls->uni2char(ec, op, NLS_MAX_CHARSET_SIZE)) > 0) {
			op += charlen;
			len -= charlen;
		} else {
			if (uni_xlate == 1) {
				*op++ = ':';
				op = pack_hex_byte(op, ec >> 8);
				op = pack_hex_byte(op, ec);
				len -= 5;
			} else {
				*op++ = '?';
				len--;
			}
		}
	}

	if (unlikely(*ip)) {
		printk(KERN_WARNING "FAT: filename was truncated while "
		       "converting.");
	}

	*op = 0;
	return (op - ascii);
}

static inline int fat_uni_to_x8(struct msdos_sb_info *sbi, const wchar_t *uni,
				unsigned char *buf, int size)
{
	if (sbi->options.utf8)
		return utf16s_to_utf8s(uni, FAT_MAX_UNI_CHARS,
				UTF16_HOST_ENDIAN, buf, size);
	else
		return uni16_to_x8(buf, uni, size, sbi->options.unicode_xlate,
				   sbi->nls_io);
}

static inline int
fat_short2uni(struct nls_table *t, unsigned char *c, int clen, wchar_t *uni)
{
	int charlen;

	charlen = t->char2uni(c, clen, uni);
	if (charlen < 0) {
		*uni = 0x003f;	/* a question mark */
		charlen = 1;
	}
	return charlen;
}

static inline int
fat_short2lower_uni(struct nls_table *t, unsigned char *c, int clen, wchar_t *uni)
{
	int charlen;
	wchar_t wc;

	charlen = t->char2uni(c, clen, &wc);
	if (charlen < 0) {
		*uni = 0x003f;	/* a question mark */
		charlen = 1;
	} else if (charlen <= 1) {
		unsigned char nc = t->charset2lower[*c];

		if (!nc)
			nc = *c;

		if ( (charlen = t->char2uni(&nc, 1, uni)) < 0) {
			*uni = 0x003f;	/* a question mark */
			charlen = 1;
		}
	} else
		*uni = wc;

	return charlen;
}

static inline int
fat_shortname2uni(struct nls_table *nls, unsigned char *buf, int buf_size,
		  wchar_t *uni_buf, unsigned short opt, int lower)
{
	int len = 0;

	if (opt & VFAT_SFN_DISPLAY_LOWER)
		len =  fat_short2lower_uni(nls, buf, buf_size, uni_buf);
	else if (opt & VFAT_SFN_DISPLAY_WIN95)
		len = fat_short2uni(nls, buf, buf_size, uni_buf);
	else if (opt & VFAT_SFN_DISPLAY_WINNT) {
		if (lower)
			len = fat_short2lower_uni(nls, buf, buf_size, uni_buf);
		else
			len = fat_short2uni(nls, buf, buf_size, uni_buf);
	} else
		len = fat_short2uni(nls, buf, buf_size, uni_buf);

	return len;
}

static inline int fat_name_match(struct msdos_sb_info *sbi,
				 const unsigned char *a, int a_len,
				 const unsigned char *b, int b_len)
{
	if (a_len != b_len)
		return 0;

	if (sbi->options.name_check != 's')
		return !nls_strnicmp(sbi->nls_io, a, b, a_len);
	else
		return !memcmp(a, b, a_len);
}

enum { PARSE_INVALID = 1, PARSE_NOT_LONGNAME, PARSE_EOF, };

/**
 * fat_parse_long - Parse extended directory entry.
 *
 * This function returns zero on success, negative value on error, or one of
 * the following:
 *
 * %PARSE_INVALID - Directory entry is invalid.
 * %PARSE_NOT_LONGNAME - Directory entry does not contain longname.
 * %PARSE_EOF - Directory has no more entries.
 */
static int fat_parse_long(struct inode *dir, loff_t *pos,
			  struct buffer_head **bh, struct msdos_dir_entry **de,
			  wchar_t **unicode, unsigned char *nr_slots)
{
	struct msdos_dir_slot *ds;
	unsigned char id, slot, slots, alias_checksum;

	if (!*unicode) {
		*unicode = __getname();
		if (!*unicode) {
			brelse(*bh);
			return -ENOMEM;
		}
	}
parse_long:
	slots = 0;
	ds = (struct msdos_dir_slot *)*de;
	id = ds->id;
	if (!(id & 0x40))
		return PARSE_INVALID;
	slots = id & ~0x40;
	if (slots > 20 || !slots)	/* ceil(256 * 2 / 26) */
		return PARSE_INVALID;
	*nr_slots = slots;
	alias_checksum = ds->alias_checksum;

	slot = slots;
	while (1) {
		int offset;

		slot--;
		offset = slot * 13;
		fat16_towchar(*unicode + offset, ds->name0_4, 5);
		fat16_towchar(*unicode + offset + 5, ds->name5_10, 6);
		fat16_towchar(*unicode + offset + 11, ds->name11_12, 2);

		if (ds->id & 0x40)
			(*unicode)[offset + 13] = 0;
		if (fat_get_entry(dir, pos, bh, de) < 0)
			return PARSE_EOF;
		if (slot == 0)
			break;
		ds = (struct msdos_dir_slot *)*de;
		if (ds->attr != ATTR_EXT)
			return PARSE_NOT_LONGNAME;
		if ((ds->id & ~0x40) != slot)
			goto parse_long;
		if (ds->alias_checksum != alias_checksum)
			goto parse_long;
	}
	if ((*de)->name[0] == DELETED_FLAG)
		return PARSE_INVALID;
	if ((*de)->attr == ATTR_EXT)
		goto parse_long;
	if (IS_FREE((*de)->name) || ((*de)->attr & ATTR_VOLUME))
		return PARSE_INVALID;
	if (fat_checksum((*de)->name) != alias_checksum)
		*nr_slots = 0;

	return 0;
}

/*
 * Return values: negative -> error, 0 -> not found, positive -> found,
 * value is the total amount of slots, including the shortname entry.
 */
int fat_search_long(struct inode *inode, const unsigned char *name,
		    int name_len, struct fat_slot_info *sinfo)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	struct nls_table *nls_disk = sbi->nls_disk;
	unsigned char nr_slots;
	wchar_t bufuname[14];
	wchar_t *unicode = NULL;
	unsigned char work[MSDOS_NAME];
	unsigned char bufname[FAT_MAX_SHORT_SIZE];
	unsigned short opt_shortname = sbi->options.shortname;
	loff_t cpos = 0;
	int chl, i, j, last_u, err, len;

	err = -ENOENT;
	while (1) {
		if (fat_get_entry(inode, &cpos, &bh, &de) == -1)
			goto end_of_dir;
parse_record:
		nr_slots = 0;
		if (de->name[0] == DELETED_FLAG)
			continue;
		if (de->attr != ATTR_EXT && (de->attr & ATTR_VOLUME))
			continue;
		if (de->attr != ATTR_EXT && IS_FREE(de->name))
			continue;
		if (de->attr == ATTR_EXT) {
			int status = fat_parse_long(inode, &cpos, &bh, &de,
						    &unicode, &nr_slots);
			if (status < 0) {
				err = status;
				goto end_of_dir;
			} else if (status == PARSE_INVALID)
				continue;
			else if (status == PARSE_NOT_LONGNAME)
				goto parse_record;
			else if (status == PARSE_EOF)
				goto end_of_dir;
		}

		memcpy(work, de->name, sizeof(de->name));
		/* see namei.c, msdos_format_name */
		if (work[0] == 0x05)
			work[0] = 0xE5;
		for (i = 0, j = 0, last_u = 0; i < 8;) {
			if (!work[i])
				break;
			chl = fat_shortname2uni(nls_disk, &work[i], 8 - i,
						&bufuname[j++], opt_shortname,
						de->lcase & CASE_LOWER_BASE);
			if (chl <= 1) {
				if (work[i] != ' ')
					last_u = j;
			} else {
				last_u = j;
			}
			i += chl;
		}
		j = last_u;
		fat_short2uni(nls_disk, ".", 1, &bufuname[j++]);
		for (i = 8; i < MSDOS_NAME;) {
			if (!work[i])
				break;
			chl = fat_shortname2uni(nls_disk, &work[i],
						MSDOS_NAME - i,
						&bufuname[j++], opt_shortname,
						de->lcase & CASE_LOWER_EXT);
			if (chl <= 1) {
				if (work[i] != ' ')
					last_u = j;
			} else {
				last_u = j;
			}
			i += chl;
		}
		if (!last_u)
			continue;

		/* Compare shortname */
		bufuname[last_u] = 0x0000;
		len = fat_uni_to_x8(sbi, bufuname, bufname, sizeof(bufname));
		if (fat_name_match(sbi, name, name_len, bufname, len))
			goto found;

		if (nr_slots) {
			void *longname = unicode + FAT_MAX_UNI_CHARS;
			int size = PATH_MAX - FAT_MAX_UNI_SIZE;

			/* Compare longname */
			len = fat_uni_to_x8(sbi, unicode, longname, size);
			if (fat_name_match(sbi, name, name_len, longname, len))
				goto found;
		}
	}

found:
	nr_slots++;	/* include the de */
	sinfo->slot_off = cpos - nr_slots * sizeof(*de);
	sinfo->nr_slots = nr_slots;
	sinfo->de = de;
	sinfo->bh = bh;
	sinfo->i_pos = fat_make_i_pos(sb, sinfo->bh, sinfo->de);
	err = 0;
end_of_dir:
	if (unicode)
		__putname(unicode);

	return err;
}

EXPORT_SYMBOL_GPL(fat_search_long);

struct fat_ioctl_filldir_callback {
	void __user *dirent;
	int result;
	/* for dir ioctl */
	const char *longname;
	int long_len;
	const char *shortname;
	int short_len;
};

static int __fat_readdir(struct inode *inode, struct file *filp, void *dirent,
			 filldir_t filldir, int short_only, int both)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct nls_table *nls_disk = sbi->nls_disk;
	unsigned char nr_slots;
	wchar_t bufuname[14];
	wchar_t *unicode = NULL;
	unsigned char c, work[MSDOS_NAME];
	unsigned char bufname[FAT_MAX_SHORT_SIZE], *ptname = bufname;
	unsigned short opt_shortname = sbi->options.shortname;
	int isvfat = sbi->options.isvfat;
	int nocase = sbi->options.nocase;
	const char *fill_name = NULL;
	unsigned long inum;
	unsigned long lpos, dummy, *furrfu = &lpos;
	loff_t cpos;
	int chi, chl, i, i2, j, last, last_u, dotoffset = 0, fill_len = 0;
	int ret = 0;

	lock_super(sb);

	cpos = filp->f_pos;
	/* Fake . and .. for the root directory. */
	if (inode->i_ino == MSDOS_ROOT_INO) {
		while (cpos < 2) {
			if (filldir(dirent, "..", cpos+1, cpos, MSDOS_ROOT_INO, DT_DIR) < 0)
				goto out;
			cpos++;
			filp->f_pos++;
		}
		if (cpos == 2) {
			dummy = 2;
			furrfu = &dummy;
			cpos = 0;
		}
	}
	if (cpos & (sizeof(struct msdos_dir_entry) - 1)) {
		ret = -ENOENT;
		goto out;
	}

	bh = NULL;
get_new:
	if (fat_get_entry(inode, &cpos, &bh, &de) == -1)
		goto end_of_dir;
parse_record:
	nr_slots = 0;
	/*
	 * Check for long filename entry, but if short_only, we don't
	 * need to parse long filename.
	 */
	if (isvfat && !short_only) {
		if (de->name[0] == DELETED_FLAG)
			goto record_end;
		if (de->attr != ATTR_EXT && (de->attr & ATTR_VOLUME))
			goto record_end;
		if (de->attr != ATTR_EXT && IS_FREE(de->name))
			goto record_end;
	} else {
		if ((de->attr & ATTR_VOLUME) || IS_FREE(de->name))
			goto record_end;
	}

	if (isvfat && de->attr == ATTR_EXT) {
		int status = fat_parse_long(inode, &cpos, &bh, &de,
					    &unicode, &nr_slots);
		if (status < 0) {
			filp->f_pos = cpos;
			ret = status;
			goto out;
		} else if (status == PARSE_INVALID)
			goto record_end;
		else if (status == PARSE_NOT_LONGNAME)
			goto parse_record;
		else if (status == PARSE_EOF)
			goto end_of_dir;

		if (nr_slots) {
			void *longname = unicode + FAT_MAX_UNI_CHARS;
			int size = PATH_MAX - FAT_MAX_UNI_SIZE;
			int len = fat_uni_to_x8(sbi, unicode, longname, size);

			fill_name = longname;
			fill_len = len;
			/* !both && !short_only, so we don't need shortname. */
			if (!both)
				goto start_filldir;
		}
	}

	if (sbi->options.dotsOK) {
		ptname = bufname;
		dotoffset = 0;
		if (de->attr & ATTR_HIDDEN) {
			*ptname++ = '.';
			dotoffset = 1;
		}
	}

	memcpy(work, de->name, sizeof(de->name));
	/* see namei.c, msdos_format_name */
	if (work[0] == 0x05)
		work[0] = 0xE5;
	for (i = 0, j = 0, last = 0, last_u = 0; i < 8;) {
		if (!(c = work[i]))
			break;
		chl = fat_shortname2uni(nls_disk, &work[i], 8 - i,
					&bufuname[j++], opt_shortname,
					de->lcase & CASE_LOWER_BASE);
		if (chl <= 1) {
			ptname[i++] = (!nocase && c>='A' && c<='Z') ? c+32 : c;
			if (c != ' ') {
				last = i;
				last_u = j;
			}
		} else {
			last_u = j;
			for (chi = 0; chi < chl && i < 8; chi++) {
				ptname[i] = work[i];
				i++; last = i;
			}
		}
	}
	i = last;
	j = last_u;
	fat_short2uni(nls_disk, ".", 1, &bufuname[j++]);
	ptname[i++] = '.';
	for (i2 = 8; i2 < MSDOS_NAME;) {
		if (!(c = work[i2]))
			break;
		chl = fat_shortname2uni(nls_disk, &work[i2], MSDOS_NAME - i2,
					&bufuname[j++], opt_shortname,
					de->lcase & CASE_LOWER_EXT);
		if (chl <= 1) {
			i2++;
			ptname[i++] = (!nocase && c>='A' && c<='Z') ? c+32 : c;
			if (c != ' ') {
				last = i;
				last_u = j;
			}
		} else {
			last_u = j;
			for (chi = 0; chi < chl && i2 < MSDOS_NAME; chi++) {
				ptname[i++] = work[i2++];
				last = i;
			}
		}
	}
	if (!last)
		goto record_end;

	i = last + dotoffset;
	j = last_u;

	if (isvfat) {
		bufuname[j] = 0x0000;
		i = fat_uni_to_x8(sbi, bufuname, bufname, sizeof(bufname));
	}
	if (nr_slots) {
		/* hack for fat_ioctl_filldir() */
		struct fat_ioctl_filldir_callback *p = dirent;

		p->longname = fill_name;
		p->long_len = fill_len;
		p->shortname = bufname;
		p->short_len = i;
		fill_name = NULL;
		fill_len = 0;
	} else {
		fill_name = bufname;
		fill_len = i;
	}

start_filldir:
	lpos = cpos - (nr_slots + 1) * sizeof(struct msdos_dir_entry);
	if (!memcmp(de->name, MSDOS_DOT, MSDOS_NAME))
		inum = inode->i_ino;
	else if (!memcmp(de->name, MSDOS_DOTDOT, MSDOS_NAME)) {
		inum = parent_ino(filp->f_path.dentry);
	} else {
		loff_t i_pos = fat_make_i_pos(sb, bh, de);
		struct inode *tmp = fat_iget(sb, i_pos);
		if (tmp) {
			inum = tmp->i_ino;
			iput(tmp);
		} else
			inum = iunique(sb, MSDOS_ROOT_INO);
	}

	if (filldir(dirent, fill_name, fill_len, *furrfu, inum,
		    (de->attr & ATTR_DIR) ? DT_DIR : DT_REG) < 0)
		goto fill_failed;

record_end:
	furrfu = &lpos;
	filp->f_pos = cpos;
	goto get_new;
end_of_dir:
	filp->f_pos = cpos;
fill_failed:
	brelse(bh);
	if (unicode)
		__putname(unicode);
out:
	unlock_super(sb);
	return ret;
}

static int fat_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	return __fat_readdir(inode, filp, dirent, filldir, 0, 0);
}

#define FAT_IOCTL_FILLDIR_FUNC(func, dirent_type)			   \
static int func(void *__buf, const char *name, int name_len,		   \
			     loff_t offset, u64 ino, unsigned int d_type)  \
{									   \
	struct fat_ioctl_filldir_callback *buf = __buf;			   \
	struct dirent_type __user *d1 = buf->dirent;			   \
	struct dirent_type __user *d2 = d1 + 1;				   \
									   \
	if (buf->result)						   \
		return -EINVAL;						   \
	buf->result++;							   \
									   \
	if (name != NULL) {						   \
		/* dirent has only short name */			   \
		if (name_len >= sizeof(d1->d_name))			   \
			name_len = sizeof(d1->d_name) - 1;		   \
									   \
		if (put_user(0, d2->d_name)			||	   \
		    put_user(0, &d2->d_reclen)			||	   \
		    copy_to_user(d1->d_name, name, name_len)	||	   \
		    put_user(0, d1->d_name + name_len)		||	   \
		    put_user(name_len, &d1->d_reclen))			   \
			goto efault;					   \
	} else {							   \
		/* dirent has short and long name */			   \
		const char *longname = buf->longname;			   \
		int long_len = buf->long_len;				   \
		const char *shortname = buf->shortname;			   \
		int short_len = buf->short_len;				   \
									   \
		if (long_len >= sizeof(d1->d_name))			   \
			long_len = sizeof(d1->d_name) - 1;		   \
		if (short_len >= sizeof(d1->d_name))			   \
			short_len = sizeof(d1->d_name) - 1;		   \
									   \
		if (copy_to_user(d2->d_name, longname, long_len)	|| \
		    put_user(0, d2->d_name + long_len)			|| \
		    put_user(long_len, &d2->d_reclen)			|| \
		    put_user(ino, &d2->d_ino)				|| \
		    put_user(offset, &d2->d_off)			|| \
		    copy_to_user(d1->d_name, shortname, short_len)	|| \
		    put_user(0, d1->d_name + short_len)			|| \
		    put_user(short_len, &d1->d_reclen))			   \
			goto efault;					   \
	}								   \
	return 0;							   \
efault:									   \
	buf->result = -EFAULT;						   \
	return -EFAULT;							   \
}

FAT_IOCTL_FILLDIR_FUNC(fat_ioctl_filldir, __fat_dirent)

static int fat_ioctl_readdir(struct inode *inode, struct file *filp,
			     void __user *dirent, filldir_t filldir,
			     int short_only, int both)
{
	struct fat_ioctl_filldir_callback buf;
	int ret;

	buf.dirent = dirent;
	buf.result = 0;
	mutex_lock(&inode->i_mutex);
	ret = -ENOENT;
	if (!IS_DEADDIR(inode)) {
		ret = __fat_readdir(inode, filp, &buf, filldir,
				    short_only, both);
	}
	mutex_unlock(&inode->i_mutex);
	if (ret >= 0)
		ret = buf.result;
	return ret;
}

static int fat_ioctl_volume_id(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	return sbi->vol_id;
}

static long fat_dir_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct __fat_dirent __user *d1 = (struct __fat_dirent __user *)arg;
	int short_only, both;

	switch (cmd) {
	case VFAT_IOCTL_READDIR_SHORT:
		short_only = 1;
		both = 0;
		break;
	case VFAT_IOCTL_READDIR_BOTH:
		short_only = 0;
		both = 1;
		break;
	case VFAT_IOCTL_GET_VOLUME_ID:
		return fat_ioctl_volume_id(inode);
	default:
		return fat_generic_ioctl(filp, cmd, arg);
	}

	if (!access_ok(VERIFY_WRITE, d1, sizeof(struct __fat_dirent[2])))
		return -EFAULT;
	/*
	 * Yes, we don't need this put_user() absolutely. However old
	 * code didn't return the right value. So, app use this value,
	 * in order to check whether it is EOF.
	 */
	if (put_user(0, &d1->d_reclen))
		return -EFAULT;

	return fat_ioctl_readdir(inode, filp, d1, fat_ioctl_filldir,
				 short_only, both);
}

#ifdef CONFIG_COMPAT
#define	VFAT_IOCTL_READDIR_BOTH32	_IOR('r', 1, struct compat_dirent[2])
#define	VFAT_IOCTL_READDIR_SHORT32	_IOR('r', 2, struct compat_dirent[2])

FAT_IOCTL_FILLDIR_FUNC(fat_compat_ioctl_filldir, compat_dirent)

static long fat_compat_dir_ioctl(struct file *filp, unsigned cmd,
				 unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct compat_dirent __user *d1 = compat_ptr(arg);
	int short_only, both;

	switch (cmd) {
	case VFAT_IOCTL_READDIR_SHORT32:
		short_only = 1;
		both = 0;
		break;
	case VFAT_IOCTL_READDIR_BOTH32:
		short_only = 0;
		both = 1;
		break;
	default:
		return fat_generic_ioctl(filp, cmd, (unsigned long)arg);
	}

	if (!access_ok(VERIFY_WRITE, d1, sizeof(struct compat_dirent[2])))
		return -EFAULT;
	/*
	 * Yes, we don't need this put_user() absolutely. However old
	 * code didn't return the right value. So, app use this value,
	 * in order to check whether it is EOF.
	 */
	if (put_user(0, &d1->d_reclen))
		return -EFAULT;

	return fat_ioctl_readdir(inode, filp, d1, fat_compat_ioctl_filldir,
				 short_only, both);
}
#endif /* CONFIG_COMPAT */

const struct file_operations fat_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= fat_readdir,
	.unlocked_ioctl	= fat_dir_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= fat_compat_dir_ioctl,
#endif
	.fsync		= fat_file_fsync,
};

static int fat_get_short_entry(struct inode *dir, loff_t *pos,
			       struct buffer_head **bh,
			       struct msdos_dir_entry **de)
{
	while (fat_get_entry(dir, pos, bh, de) >= 0) {
		/* free entry or long name entry or volume label */
		if (!IS_FREE((*de)->name) && !((*de)->attr & ATTR_VOLUME))
			return 0;
	}
	return -ENOENT;
}

/*
 * The ".." entry can not provide the "struct fat_slot_info" informations
 * for inode. So, this function provide the some informations only.
 */
int fat_get_dotdot_entry(struct inode *dir, struct buffer_head **bh,
			 struct msdos_dir_entry **de, loff_t *i_pos)
{
	loff_t offset;

	offset = 0;
	*bh = NULL;
	while (fat_get_short_entry(dir, &offset, bh, de) >= 0) {
		if (!strncmp((*de)->name, MSDOS_DOTDOT, MSDOS_NAME)) {
			*i_pos = fat_make_i_pos(dir->i_sb, *bh, *de);
			return 0;
		}
	}
	return -ENOENT;
}

EXPORT_SYMBOL_GPL(fat_get_dotdot_entry);

/* See if directory is empty */
int fat_dir_empty(struct inode *dir)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	loff_t cpos;
	int result = 0;

	bh = NULL;
	cpos = 0;
	while (fat_get_short_entry(dir, &cpos, &bh, &de) >= 0) {
		if (strncmp(de->name, MSDOS_DOT   , MSDOS_NAME) &&
		    strncmp(de->name, MSDOS_DOTDOT, MSDOS_NAME)) {
			result = -ENOTEMPTY;
			break;
		}
	}
	brelse(bh);
	return result;
}

EXPORT_SYMBOL_GPL(fat_dir_empty);

/*
 * fat_subdirs counts the number of sub-directories of dir. It can be run
 * on directories being created.
 */
int fat_subdirs(struct inode *dir)
{
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	loff_t cpos;
	int count = 0;

	bh = NULL;
	cpos = 0;
	while (fat_get_short_entry(dir, &cpos, &bh, &de) >= 0) {
		if (de->attr & ATTR_DIR)
			count++;
	}
	brelse(bh);
	return count;
}

/*
 * Scans a directory for a given file (name points to its formatted name).
 * Returns an error code or zero.
 */
int fat_scan(struct inode *dir, const unsigned char *name,
	     struct fat_slot_info *sinfo)
{
	struct super_block *sb = dir->i_sb;

	sinfo->slot_off = 0;
	sinfo->bh = NULL;
	while (fat_get_short_entry(dir, &sinfo->slot_off, &sinfo->bh,
				   &sinfo->de) >= 0) {
		if (!strncmp(sinfo->de->name, name, MSDOS_NAME)) {
			sinfo->slot_off -= sizeof(*sinfo->de);
			sinfo->nr_slots = 1;
			sinfo->i_pos = fat_make_i_pos(sb, sinfo->bh, sinfo->de);
			return 0;
		}
	}
	return -ENOENT;
}

EXPORT_SYMBOL_GPL(fat_scan);

static int __fat_remove_entries(struct inode *dir, loff_t pos, int nr_slots)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de, *endp;
	int err = 0, orig_slots;

	while (nr_slots) {
		bh = NULL;
		if (fat_get_entry(dir, &pos, &bh, &de) < 0) {
			err = -EIO;
			break;
		}

		orig_slots = nr_slots;
		endp = (struct msdos_dir_entry *)(bh->b_data + sb->s_blocksize);
		while (nr_slots && de < endp) {
			de->name[0] = DELETED_FLAG;
			de++;
			nr_slots--;
		}
		mark_buffer_dirty_inode(bh, dir);
		if (IS_DIRSYNC(dir))
			err = sync_dirty_buffer(bh);
		brelse(bh);
		if (err)
			break;

		/* pos is *next* de's position, so this does `- sizeof(de)' */
		pos += ((orig_slots - nr_slots) * sizeof(*de)) - sizeof(*de);
	}

	return err;
}

int fat_remove_entries(struct inode *dir, struct fat_slot_info *sinfo)
{
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	int err = 0, nr_slots;

	/*
	 * First stage: Remove the shortname. By this, the directory
	 * entry is removed.
	 */
	nr_slots = sinfo->nr_slots;
	de = sinfo->de;
	sinfo->de = NULL;
	bh = sinfo->bh;
	sinfo->bh = NULL;
	while (nr_slots && de >= (struct msdos_dir_entry *)bh->b_data) {
		de->name[0] = DELETED_FLAG;
		de--;
		nr_slots--;
	}
	mark_buffer_dirty_inode(bh, dir);
	if (IS_DIRSYNC(dir))
		err = sync_dirty_buffer(bh);
	brelse(bh);
	if (err)
		return err;
	dir->i_version++;

	if (nr_slots) {
		/*
		 * Second stage: remove the remaining longname slots.
		 * (This directory entry is already removed, and so return
		 * the success)
		 */
		err = __fat_remove_entries(dir, sinfo->slot_off, nr_slots);
		if (err) {
			printk(KERN_WARNING
			       "FAT: Couldn't remove the long name slots\n");
		}
	}

	dir->i_mtime = dir->i_atime = CURRENT_TIME_SEC;
	if (IS_DIRSYNC(dir))
		(void)fat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	return 0;
}

EXPORT_SYMBOL_GPL(fat_remove_entries);

static int fat_zeroed_cluster(struct inode *dir, sector_t blknr, int nr_used,
			      struct buffer_head **bhs, int nr_bhs)
{
	struct super_block *sb = dir->i_sb;
	sector_t last_blknr = blknr + MSDOS_SB(sb)->sec_per_clus;
	int err, i, n;

	/* Zeroing the unused blocks on this cluster */
	blknr += nr_used;
	n = nr_used;
	while (blknr < last_blknr) {
		bhs[n] = sb_getblk(sb, blknr);
		if (!bhs[n]) {
			err = -ENOMEM;
			goto error;
		}
		memset(bhs[n]->b_data, 0, sb->s_blocksize);
		set_buffer_uptodate(bhs[n]);
		mark_buffer_dirty_inode(bhs[n], dir);

		n++;
		blknr++;
		if (n == nr_bhs) {
			if (IS_DIRSYNC(dir)) {
				err = fat_sync_bhs(bhs, n);
				if (err)
					goto error;
			}
			for (i = 0; i < n; i++)
				brelse(bhs[i]);
			n = 0;
		}
	}
	if (IS_DIRSYNC(dir)) {
		err = fat_sync_bhs(bhs, n);
		if (err)
			goto error;
	}
	for (i = 0; i < n; i++)
		brelse(bhs[i]);

	return 0;

error:
	for (i = 0; i < n; i++)
		bforget(bhs[i]);
	return err;
}

int fat_alloc_new_dir(struct inode *dir, struct timespec *ts)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	struct msdos_dir_entry *de;
	sector_t blknr;
	__le16 date, time;
	u8 time_cs;
	int err, cluster;

	err = fat_alloc_clusters(dir, &cluster, 1);
	if (err)
		goto error;

	blknr = fat_clus_to_blknr(sbi, cluster);
	bhs[0] = sb_getblk(sb, blknr);
	if (!bhs[0]) {
		err = -ENOMEM;
		goto error_free;
	}

	fat_time_unix2fat(sbi, ts, &time, &date, &time_cs);

	de = (struct msdos_dir_entry *)bhs[0]->b_data;
	/* filling the new directory slots ("." and ".." entries) */
	memcpy(de[0].name, MSDOS_DOT, MSDOS_NAME);
	memcpy(de[1].name, MSDOS_DOTDOT, MSDOS_NAME);
	de->attr = de[1].attr = ATTR_DIR;
	de[0].lcase = de[1].lcase = 0;
	de[0].time = de[1].time = time;
	de[0].date = de[1].date = date;
	if (sbi->options.isvfat) {
		/* extra timestamps */
		de[0].ctime = de[1].ctime = time;
		de[0].ctime_cs = de[1].ctime_cs = time_cs;
		de[0].adate = de[0].cdate = de[1].adate = de[1].cdate = date;
	} else {
		de[0].ctime = de[1].ctime = 0;
		de[0].ctime_cs = de[1].ctime_cs = 0;
		de[0].adate = de[0].cdate = de[1].adate = de[1].cdate = 0;
	}
	de[0].start = cpu_to_le16(cluster);
	de[0].starthi = cpu_to_le16(cluster >> 16);
	de[1].start = cpu_to_le16(MSDOS_I(dir)->i_logstart);
	de[1].starthi = cpu_to_le16(MSDOS_I(dir)->i_logstart >> 16);
	de[0].size = de[1].size = 0;
	memset(de + 2, 0, sb->s_blocksize - 2 * sizeof(*de));
	set_buffer_uptodate(bhs[0]);
	mark_buffer_dirty_inode(bhs[0], dir);

	err = fat_zeroed_cluster(dir, blknr, 1, bhs, MAX_BUF_PER_PAGE);
	if (err)
		goto error_free;

	return cluster;

error_free:
	fat_free_clusters(dir, cluster);
error:
	return err;
}

EXPORT_SYMBOL_GPL(fat_alloc_new_dir);

static int fat_add_new_entries(struct inode *dir, void *slots, int nr_slots,
			       int *nr_cluster, struct msdos_dir_entry **de,
			       struct buffer_head **bh, loff_t *i_pos)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	sector_t blknr, start_blknr, last_blknr;
	unsigned long size, copy;
	int err, i, n, offset, cluster[2];

	/*
	 * The minimum cluster size is 512bytes, and maximum entry
	 * size is 32*slots (672bytes).  So, iff the cluster size is
	 * 512bytes, we may need two clusters.
	 */
	size = nr_slots * sizeof(struct msdos_dir_entry);
	*nr_cluster = (size + (sbi->cluster_size - 1)) >> sbi->cluster_bits;
	BUG_ON(*nr_cluster > 2);

	err = fat_alloc_clusters(dir, cluster, *nr_cluster);
	if (err)
		goto error;

	/*
	 * First stage: Fill the directory entry.  NOTE: This cluster
	 * is not referenced from any inode yet, so updates order is
	 * not important.
	 */
	i = n = copy = 0;
	do {
		start_blknr = blknr = fat_clus_to_blknr(sbi, cluster[i]);
		last_blknr = start_blknr + sbi->sec_per_clus;
		while (blknr < last_blknr) {
			bhs[n] = sb_getblk(sb, blknr);
			if (!bhs[n]) {
				err = -ENOMEM;
				goto error_nomem;
			}

			/* fill the directory entry */
			copy = min(size, sb->s_blocksize);
			memcpy(bhs[n]->b_data, slots, copy);
			slots += copy;
			size -= copy;
			set_buffer_uptodate(bhs[n]);
			mark_buffer_dirty_inode(bhs[n], dir);
			if (!size)
				break;
			n++;
			blknr++;
		}
	} while (++i < *nr_cluster);

	memset(bhs[n]->b_data + copy, 0, sb->s_blocksize - copy);
	offset = copy - sizeof(struct msdos_dir_entry);
	get_bh(bhs[n]);
	*bh = bhs[n];
	*de = (struct msdos_dir_entry *)((*bh)->b_data + offset);
	*i_pos = fat_make_i_pos(sb, *bh, *de);

	/* Second stage: clear the rest of cluster, and write outs */
	err = fat_zeroed_cluster(dir, start_blknr, ++n, bhs, MAX_BUF_PER_PAGE);
	if (err)
		goto error_free;

	return cluster[0];

error_free:
	brelse(*bh);
	*bh = NULL;
	n = 0;
error_nomem:
	for (i = 0; i < n; i++)
		bforget(bhs[i]);
	fat_free_clusters(dir, cluster[0]);
error:
	return err;
}

int fat_add_entries(struct inode *dir, void *slots, int nr_slots,
		    struct fat_slot_info *sinfo)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh, *prev, *bhs[3]; /* 32*slots (672bytes) */
	struct msdos_dir_entry *de;
	int err, free_slots, i, nr_bhs;
	loff_t pos, i_pos;

	sinfo->nr_slots = nr_slots;

	/* First stage: search free direcotry entries */
	free_slots = nr_bhs = 0;
	bh = prev = NULL;
	pos = 0;
	err = -ENOSPC;
	while (fat_get_entry(dir, &pos, &bh, &de) > -1) {
		/* check the maximum size of directory */
		if (pos >= FAT_MAX_DIR_SIZE)
			goto error;

		if (IS_FREE(de->name)) {
			if (prev != bh) {
				get_bh(bh);
				bhs[nr_bhs] = prev = bh;
				nr_bhs++;
			}
			free_slots++;
			if (free_slots == nr_slots)
				goto found;
		} else {
			for (i = 0; i < nr_bhs; i++)
				brelse(bhs[i]);
			prev = NULL;
			free_slots = nr_bhs = 0;
		}
	}
	if (dir->i_ino == MSDOS_ROOT_INO) {
		if (sbi->fat_bits != 32)
			goto error;
	} else if (MSDOS_I(dir)->i_start == 0) {
		printk(KERN_ERR "FAT: Corrupted directory (i_pos %lld)\n",
		       MSDOS_I(dir)->i_pos);
		err = -EIO;
		goto error;
	}

found:
	err = 0;
	pos -= free_slots * sizeof(*de);
	nr_slots -= free_slots;
	if (free_slots) {
		/*
		 * Second stage: filling the free entries with new entries.
		 * NOTE: If this slots has shortname, first, we write
		 * the long name slots, then write the short name.
		 */
		int size = free_slots * sizeof(*de);
		int offset = pos & (sb->s_blocksize - 1);
		int long_bhs = nr_bhs - (nr_slots == 0);

		/* Fill the long name slots. */
		for (i = 0; i < long_bhs; i++) {
			int copy = min_t(int, sb->s_blocksize - offset, size);
			memcpy(bhs[i]->b_data + offset, slots, copy);
			mark_buffer_dirty_inode(bhs[i], dir);
			offset = 0;
			slots += copy;
			size -= copy;
		}
		if (long_bhs && IS_DIRSYNC(dir))
			err = fat_sync_bhs(bhs, long_bhs);
		if (!err && i < nr_bhs) {
			/* Fill the short name slot. */
			int copy = min_t(int, sb->s_blocksize - offset, size);
			memcpy(bhs[i]->b_data + offset, slots, copy);
			mark_buffer_dirty_inode(bhs[i], dir);
			if (IS_DIRSYNC(dir))
				err = sync_dirty_buffer(bhs[i]);
		}
		for (i = 0; i < nr_bhs; i++)
			brelse(bhs[i]);
		if (err)
			goto error_remove;
	}

	if (nr_slots) {
		int cluster, nr_cluster;

		/*
		 * Third stage: allocate the cluster for new entries.
		 * And initialize the cluster with new entries, then
		 * add the cluster to dir.
		 */
		cluster = fat_add_new_entries(dir, slots, nr_slots, &nr_cluster,
					      &de, &bh, &i_pos);
		if (cluster < 0) {
			err = cluster;
			goto error_remove;
		}
		err = fat_chain_add(dir, cluster, nr_cluster);
		if (err) {
			fat_free_clusters(dir, cluster);
			goto error_remove;
		}
		if (dir->i_size & (sbi->cluster_size - 1)) {
			fat_fs_error(sb, "Odd directory size");
			dir->i_size = (dir->i_size + sbi->cluster_size - 1)
				& ~((loff_t)sbi->cluster_size - 1);
		}
		dir->i_size += nr_cluster << sbi->cluster_bits;
		MSDOS_I(dir)->mmu_private += nr_cluster << sbi->cluster_bits;
	}
	sinfo->slot_off = pos;
	sinfo->de = de;
	sinfo->bh = bh;
	sinfo->i_pos = fat_make_i_pos(sb, sinfo->bh, sinfo->de);

	return 0;

error:
	brelse(bh);
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);
	return err;

error_remove:
	brelse(bh);
	if (free_slots)
		__fat_remove_entries(dir, pos, free_slots);
	return err;
}

EXPORT_SYMBOL_GPL(fat_add_entries);
