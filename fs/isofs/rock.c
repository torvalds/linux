/*
 *  linux/fs/isofs/rock.c
 *
 *  (C) 1992, 1993  Eric Youngdale
 *
 *  Rock Ridge Extensions to iso9660
 */

#include <linux/slab.h>
#include <linux/pagemap.h>

#include "isofs.h"
#include "rock.h"

/*
 * These functions are designed to read the system areas of a directory record
 * and extract relevant information.  There are different functions provided
 * depending upon what information we need at the time.  One function fills
 * out an inode structure, a second one extracts a filename, a third one
 * returns a symbolic link name, and a fourth one returns the extent number
 * for the file.
 */

#define SIG(A,B) ((A) | ((B) << 8))	/* isonum_721() */

struct rock_state {
	void *buffer;
	unsigned char *chr;
	int len;
	int cont_size;
	int cont_extent;
	int cont_offset;
	struct inode *inode;
};

/*
 * This is a way of ensuring that we have something in the system
 * use fields that is compatible with Rock Ridge.  Return zero on success.
 */

static int check_sp(struct rock_ridge *rr, struct inode *inode)
{
	if (rr->u.SP.magic[0] != 0xbe)
		return -1;
	if (rr->u.SP.magic[1] != 0xef)
		return -1;
	ISOFS_SB(inode->i_sb)->s_rock_offset = rr->u.SP.skip;
	return 0;
}

static void setup_rock_ridge(struct iso_directory_record *de,
			struct inode *inode, struct rock_state *rs)
{
	rs->len = sizeof(struct iso_directory_record) + de->name_len[0];
	if (rs->len & 1)
		(rs->len)++;
	rs->chr = (unsigned char *)de + rs->len;
	rs->len = *((unsigned char *)de) - rs->len;
	if (rs->len < 0)
		rs->len = 0;

	if (ISOFS_SB(inode->i_sb)->s_rock_offset != -1) {
		rs->len -= ISOFS_SB(inode->i_sb)->s_rock_offset;
		rs->chr += ISOFS_SB(inode->i_sb)->s_rock_offset;
		if (rs->len < 0)
			rs->len = 0;
	}
}

static void init_rock_state(struct rock_state *rs, struct inode *inode)
{
	memset(rs, 0, sizeof(*rs));
	rs->inode = inode;
}

/*
 * Returns 0 if the caller should continue scanning, 1 if the scan must end
 * and -ve on error.
 */
static int rock_continue(struct rock_state *rs)
{
	int ret = 1;
	int blocksize = 1 << rs->inode->i_blkbits;
	const int min_de_size = offsetof(struct rock_ridge, u);

	kfree(rs->buffer);
	rs->buffer = NULL;

	if ((unsigned)rs->cont_offset > blocksize - min_de_size ||
	    (unsigned)rs->cont_size > blocksize ||
	    (unsigned)(rs->cont_offset + rs->cont_size) > blocksize) {
		printk(KERN_NOTICE "rock: corrupted directory entry. "
			"extent=%d, offset=%d, size=%d\n",
			rs->cont_extent, rs->cont_offset, rs->cont_size);
		ret = -EIO;
		goto out;
	}

	if (rs->cont_extent) {
		struct buffer_head *bh;

		rs->buffer = kmalloc(rs->cont_size, GFP_KERNEL);
		if (!rs->buffer) {
			ret = -ENOMEM;
			goto out;
		}
		ret = -EIO;
		bh = sb_bread(rs->inode->i_sb, rs->cont_extent);
		if (bh) {
			memcpy(rs->buffer, bh->b_data + rs->cont_offset,
					rs->cont_size);
			put_bh(bh);
			rs->chr = rs->buffer;
			rs->len = rs->cont_size;
			rs->cont_extent = 0;
			rs->cont_size = 0;
			rs->cont_offset = 0;
			return 0;
		}
		printk("Unable to read rock-ridge attributes\n");
	}
out:
	kfree(rs->buffer);
	rs->buffer = NULL;
	return ret;
}

/*
 * We think there's a record of type `sig' at rs->chr.  Parse the signature
 * and make sure that there's really room for a record of that type.
 */
static int rock_check_overflow(struct rock_state *rs, int sig)
{
	int len;

	switch (sig) {
	case SIG('S', 'P'):
		len = sizeof(struct SU_SP_s);
		break;
	case SIG('C', 'E'):
		len = sizeof(struct SU_CE_s);
		break;
	case SIG('E', 'R'):
		len = sizeof(struct SU_ER_s);
		break;
	case SIG('R', 'R'):
		len = sizeof(struct RR_RR_s);
		break;
	case SIG('P', 'X'):
		len = sizeof(struct RR_PX_s);
		break;
	case SIG('P', 'N'):
		len = sizeof(struct RR_PN_s);
		break;
	case SIG('S', 'L'):
		len = sizeof(struct RR_SL_s);
		break;
	case SIG('N', 'M'):
		len = sizeof(struct RR_NM_s);
		break;
	case SIG('C', 'L'):
		len = sizeof(struct RR_CL_s);
		break;
	case SIG('P', 'L'):
		len = sizeof(struct RR_PL_s);
		break;
	case SIG('T', 'F'):
		len = sizeof(struct RR_TF_s);
		break;
	case SIG('Z', 'F'):
		len = sizeof(struct RR_ZF_s);
		break;
	default:
		len = 0;
		break;
	}
	len += offsetof(struct rock_ridge, u);
	if (len > rs->len) {
		printk(KERN_NOTICE "rock: directory entry would overflow "
				"storage\n");
		printk(KERN_NOTICE "rock: sig=0x%02x, size=%d, remaining=%d\n",
				sig, len, rs->len);
		return -EIO;
	}
	return 0;
}

/*
 * return length of name field; 0: not found, -1: to be ignored
 */
int get_rock_ridge_filename(struct iso_directory_record *de,
			    char *retname, struct inode *inode)
{
	struct rock_state rs;
	struct rock_ridge *rr;
	int sig;
	int retnamlen = 0;
	int truncate = 0;
	int ret = 0;

	if (!ISOFS_SB(inode->i_sb)->s_rock)
		return 0;
	*retname = 0;

	init_rock_state(&rs, inode);
	setup_rock_ridge(de, inode, &rs);
repeat:

	while (rs.len > 2) { /* There may be one byte for padding somewhere */
		rr = (struct rock_ridge *)rs.chr;
		/*
		 * Ignore rock ridge info if rr->len is out of range, but
		 * don't return -EIO because that would make the file
		 * invisible.
		 */
		if (rr->len < 3)
			goto out;	/* Something got screwed up here */
		sig = isonum_721(rs.chr);
		if (rock_check_overflow(&rs, sig))
			goto eio;
		rs.chr += rr->len;
		rs.len -= rr->len;
		/*
		 * As above, just ignore the rock ridge info if rr->len
		 * is bogus.
		 */
		if (rs.len < 0)
			goto out;	/* Something got screwed up here */

		switch (sig) {
		case SIG('R', 'R'):
			if ((rr->u.RR.flags[0] & RR_NM) == 0)
				goto out;
			break;
		case SIG('S', 'P'):
			if (check_sp(rr, inode))
				goto out;
			break;
		case SIG('C', 'E'):
			rs.cont_extent = isonum_733(rr->u.CE.extent);
			rs.cont_offset = isonum_733(rr->u.CE.offset);
			rs.cont_size = isonum_733(rr->u.CE.size);
			break;
		case SIG('N', 'M'):
			if (truncate)
				break;
			if (rr->len < 5)
				break;
			/*
			 * If the flags are 2 or 4, this indicates '.' or '..'.
			 * We don't want to do anything with this, because it
			 * screws up the code that calls us.  We don't really
			 * care anyways, since we can just use the non-RR
			 * name.
			 */
			if (rr->u.NM.flags & 6)
				break;

			if (rr->u.NM.flags & ~1) {
				printk("Unsupported NM flag settings (%d)\n",
					rr->u.NM.flags);
				break;
			}
			if ((strlen(retname) + rr->len - 5) >= 254) {
				truncate = 1;
				break;
			}
			strncat(retname, rr->u.NM.name, rr->len - 5);
			retnamlen += rr->len - 5;
			break;
		case SIG('R', 'E'):
			kfree(rs.buffer);
			return -1;
		default:
			break;
		}
	}
	ret = rock_continue(&rs);
	if (ret == 0)
		goto repeat;
	if (ret == 1)
		return retnamlen; /* If 0, this file did not have a NM field */
out:
	kfree(rs.buffer);
	return ret;
eio:
	ret = -EIO;
	goto out;
}

static int
parse_rock_ridge_inode_internal(struct iso_directory_record *de,
				struct inode *inode, int regard_xa)
{
	int symlink_len = 0;
	int cnt, sig;
	struct inode *reloc;
	struct rock_ridge *rr;
	int rootflag;
	struct rock_state rs;
	int ret = 0;

	if (!ISOFS_SB(inode->i_sb)->s_rock)
		return 0;

	init_rock_state(&rs, inode);
	setup_rock_ridge(de, inode, &rs);
	if (regard_xa) {
		rs.chr += 14;
		rs.len -= 14;
		if (rs.len < 0)
			rs.len = 0;
	}

repeat:
	while (rs.len > 2) { /* There may be one byte for padding somewhere */
		rr = (struct rock_ridge *)rs.chr;
		/*
		 * Ignore rock ridge info if rr->len is out of range, but
		 * don't return -EIO because that would make the file
		 * invisible.
		 */
		if (rr->len < 3)
			goto out;	/* Something got screwed up here */
		sig = isonum_721(rs.chr);
		if (rock_check_overflow(&rs, sig))
			goto eio;
		rs.chr += rr->len;
		rs.len -= rr->len;
		/*
		 * As above, just ignore the rock ridge info if rr->len
		 * is bogus.
		 */
		if (rs.len < 0)
			goto out;	/* Something got screwed up here */

		switch (sig) {
#ifndef CONFIG_ZISOFS		/* No flag for SF or ZF */
		case SIG('R', 'R'):
			if ((rr->u.RR.flags[0] &
			     (RR_PX | RR_TF | RR_SL | RR_CL)) == 0)
				goto out;
			break;
#endif
		case SIG('S', 'P'):
			if (check_sp(rr, inode))
				goto out;
			break;
		case SIG('C', 'E'):
			rs.cont_extent = isonum_733(rr->u.CE.extent);
			rs.cont_offset = isonum_733(rr->u.CE.offset);
			rs.cont_size = isonum_733(rr->u.CE.size);
			break;
		case SIG('E', 'R'):
			ISOFS_SB(inode->i_sb)->s_rock = 1;
			printk(KERN_DEBUG "ISO 9660 Extensions: ");
			{
				int p;
				for (p = 0; p < rr->u.ER.len_id; p++)
					printk("%c", rr->u.ER.data[p]);
			}
			printk("\n");
			break;
		case SIG('P', 'X'):
			inode->i_mode = isonum_733(rr->u.PX.mode);
			set_nlink(inode, isonum_733(rr->u.PX.n_links));
			i_uid_write(inode, isonum_733(rr->u.PX.uid));
			i_gid_write(inode, isonum_733(rr->u.PX.gid));
			break;
		case SIG('P', 'N'):
			{
				int high, low;
				high = isonum_733(rr->u.PN.dev_high);
				low = isonum_733(rr->u.PN.dev_low);
				/*
				 * The Rock Ridge standard specifies that if
				 * sizeof(dev_t) <= 4, then the high field is
				 * unused, and the device number is completely
				 * stored in the low field.  Some writers may
				 * ignore this subtlety,
				 * and as a result we test to see if the entire
				 * device number is
				 * stored in the low field, and use that.
				 */
				if ((low & ~0xff) && high == 0) {
					inode->i_rdev =
					    MKDEV(low >> 8, low & 0xff);
				} else {
					inode->i_rdev =
					    MKDEV(high, low);
				}
			}
			break;
		case SIG('T', 'F'):
			/*
			 * Some RRIP writers incorrectly place ctime in the
			 * TF_CREATE field. Try to handle this correctly for
			 * either case.
			 */
			/* Rock ridge never appears on a High Sierra disk */
			cnt = 0;
			if (rr->u.TF.flags & TF_CREATE) {
				inode->i_ctime.tv_sec =
				    iso_date(rr->u.TF.times[cnt++].time,
					     0);
				inode->i_ctime.tv_nsec = 0;
			}
			if (rr->u.TF.flags & TF_MODIFY) {
				inode->i_mtime.tv_sec =
				    iso_date(rr->u.TF.times[cnt++].time,
					     0);
				inode->i_mtime.tv_nsec = 0;
			}
			if (rr->u.TF.flags & TF_ACCESS) {
				inode->i_atime.tv_sec =
				    iso_date(rr->u.TF.times[cnt++].time,
					     0);
				inode->i_atime.tv_nsec = 0;
			}
			if (rr->u.TF.flags & TF_ATTRIBUTES) {
				inode->i_ctime.tv_sec =
				    iso_date(rr->u.TF.times[cnt++].time,
					     0);
				inode->i_ctime.tv_nsec = 0;
			}
			break;
		case SIG('S', 'L'):
			{
				int slen;
				struct SL_component *slp;
				struct SL_component *oldslp;
				slen = rr->len - 5;
				slp = &rr->u.SL.link;
				inode->i_size = symlink_len;
				while (slen > 1) {
					rootflag = 0;
					switch (slp->flags & ~1) {
					case 0:
						inode->i_size +=
						    slp->len;
						break;
					case 2:
						inode->i_size += 1;
						break;
					case 4:
						inode->i_size += 2;
						break;
					case 8:
						rootflag = 1;
						inode->i_size += 1;
						break;
					default:
						printk("Symlink component flag "
							"not implemented\n");
					}
					slen -= slp->len + 2;
					oldslp = slp;
					slp = (struct SL_component *)
						(((char *)slp) + slp->len + 2);

					if (slen < 2) {
						if (((rr->u.SL.
						      flags & 1) != 0)
						    &&
						    ((oldslp->
						      flags & 1) == 0))
							inode->i_size +=
							    1;
						break;
					}

					/*
					 * If this component record isn't
					 * continued, then append a '/'.
					 */
					if (!rootflag
					    && (oldslp->flags & 1) == 0)
						inode->i_size += 1;
				}
			}
			symlink_len = inode->i_size;
			break;
		case SIG('R', 'E'):
			printk(KERN_WARNING "Attempt to read inode for "
					"relocated directory\n");
			goto out;
		case SIG('C', 'L'):
			ISOFS_I(inode)->i_first_extent =
			    isonum_733(rr->u.CL.location);
			reloc =
			    isofs_iget(inode->i_sb,
				       ISOFS_I(inode)->i_first_extent,
				       0);
			if (IS_ERR(reloc)) {
				ret = PTR_ERR(reloc);
				goto out;
			}
			inode->i_mode = reloc->i_mode;
			set_nlink(inode, reloc->i_nlink);
			inode->i_uid = reloc->i_uid;
			inode->i_gid = reloc->i_gid;
			inode->i_rdev = reloc->i_rdev;
			inode->i_size = reloc->i_size;
			inode->i_blocks = reloc->i_blocks;
			inode->i_atime = reloc->i_atime;
			inode->i_ctime = reloc->i_ctime;
			inode->i_mtime = reloc->i_mtime;
			iput(reloc);
			break;
#ifdef CONFIG_ZISOFS
		case SIG('Z', 'F'): {
			int algo;

			if (ISOFS_SB(inode->i_sb)->s_nocompress)
				break;
			algo = isonum_721(rr->u.ZF.algorithm);
			if (algo == SIG('p', 'z')) {
				int block_shift =
					isonum_711(&rr->u.ZF.parms[1]);
				if (block_shift > 17) {
					printk(KERN_WARNING "isofs: "
						"Can't handle ZF block "
						"size of 2^%d\n",
						block_shift);
				} else {
					/*
					 * Note: we don't change
					 * i_blocks here
					 */
					ISOFS_I(inode)->i_file_format =
						isofs_file_compressed;
					/*
					 * Parameters to compression
					 * algorithm (header size,
					 * block size)
					 */
					ISOFS_I(inode)->i_format_parm[0] =
						isonum_711(&rr->u.ZF.parms[0]);
					ISOFS_I(inode)->i_format_parm[1] =
						isonum_711(&rr->u.ZF.parms[1]);
					inode->i_size =
					    isonum_733(rr->u.ZF.
						       real_size);
				}
			} else {
				printk(KERN_WARNING
				       "isofs: Unknown ZF compression "
						"algorithm: %c%c\n",
				       rr->u.ZF.algorithm[0],
				       rr->u.ZF.algorithm[1]);
			}
			break;
		}
#endif
		default:
			break;
		}
	}
	ret = rock_continue(&rs);
	if (ret == 0)
		goto repeat;
	if (ret == 1)
		ret = 0;
out:
	kfree(rs.buffer);
	return ret;
eio:
	ret = -EIO;
	goto out;
}

static char *get_symlink_chunk(char *rpnt, struct rock_ridge *rr, char *plimit)
{
	int slen;
	int rootflag;
	struct SL_component *oldslp;
	struct SL_component *slp;
	slen = rr->len - 5;
	slp = &rr->u.SL.link;
	while (slen > 1) {
		rootflag = 0;
		switch (slp->flags & ~1) {
		case 0:
			if (slp->len > plimit - rpnt)
				return NULL;
			memcpy(rpnt, slp->text, slp->len);
			rpnt += slp->len;
			break;
		case 2:
			if (rpnt >= plimit)
				return NULL;
			*rpnt++ = '.';
			break;
		case 4:
			if (2 > plimit - rpnt)
				return NULL;
			*rpnt++ = '.';
			*rpnt++ = '.';
			break;
		case 8:
			if (rpnt >= plimit)
				return NULL;
			rootflag = 1;
			*rpnt++ = '/';
			break;
		default:
			printk("Symlink component flag not implemented (%d)\n",
			       slp->flags);
		}
		slen -= slp->len + 2;
		oldslp = slp;
		slp = (struct SL_component *)((char *)slp + slp->len + 2);

		if (slen < 2) {
			/*
			 * If there is another SL record, and this component
			 * record isn't continued, then add a slash.
			 */
			if ((!rootflag) && (rr->u.SL.flags & 1) &&
			    !(oldslp->flags & 1)) {
				if (rpnt >= plimit)
					return NULL;
				*rpnt++ = '/';
			}
			break;
		}

		/*
		 * If this component record isn't continued, then append a '/'.
		 */
		if (!rootflag && !(oldslp->flags & 1)) {
			if (rpnt >= plimit)
				return NULL;
			*rpnt++ = '/';
		}
	}
	return rpnt;
}

int parse_rock_ridge_inode(struct iso_directory_record *de, struct inode *inode)
{
	int result = parse_rock_ridge_inode_internal(de, inode, 0);

	/*
	 * if rockridge flag was reset and we didn't look for attributes
	 * behind eventual XA attributes, have a look there
	 */
	if ((ISOFS_SB(inode->i_sb)->s_rock_offset == -1)
	    && (ISOFS_SB(inode->i_sb)->s_rock == 2)) {
		result = parse_rock_ridge_inode_internal(de, inode, 14);
	}
	return result;
}

/*
 * readpage() for symlinks: reads symlink contents into the page and either
 * makes it uptodate and returns 0 or returns error (-EIO)
 */
static int rock_ridge_symlink_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct iso_inode_info *ei = ISOFS_I(inode);
	struct isofs_sb_info *sbi = ISOFS_SB(inode->i_sb);
	char *link = kmap(page);
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	struct buffer_head *bh;
	char *rpnt = link;
	unsigned char *pnt;
	struct iso_directory_record *raw_de;
	unsigned long block, offset;
	int sig;
	struct rock_ridge *rr;
	struct rock_state rs;
	int ret;

	if (!sbi->s_rock)
		goto error;

	init_rock_state(&rs, inode);
	block = ei->i_iget5_block;
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		goto out_noread;

	offset = ei->i_iget5_offset;
	pnt = (unsigned char *)bh->b_data + offset;

	raw_de = (struct iso_directory_record *)pnt;

	/*
	 * If we go past the end of the buffer, there is some sort of error.
	 */
	if (offset + *pnt > bufsize)
		goto out_bad_span;

	/*
	 * Now test for possible Rock Ridge extensions which will override
	 * some of these numbers in the inode structure.
	 */

	setup_rock_ridge(raw_de, inode, &rs);

repeat:
	while (rs.len > 2) { /* There may be one byte for padding somewhere */
		rr = (struct rock_ridge *)rs.chr;
		if (rr->len < 3)
			goto out;	/* Something got screwed up here */
		sig = isonum_721(rs.chr);
		if (rock_check_overflow(&rs, sig))
			goto out;
		rs.chr += rr->len;
		rs.len -= rr->len;
		if (rs.len < 0)
			goto out;	/* corrupted isofs */

		switch (sig) {
		case SIG('R', 'R'):
			if ((rr->u.RR.flags[0] & RR_SL) == 0)
				goto out;
			break;
		case SIG('S', 'P'):
			if (check_sp(rr, inode))
				goto out;
			break;
		case SIG('S', 'L'):
			rpnt = get_symlink_chunk(rpnt, rr,
						 link + (PAGE_SIZE - 1));
			if (rpnt == NULL)
				goto out;
			break;
		case SIG('C', 'E'):
			/* This tells is if there is a continuation record */
			rs.cont_extent = isonum_733(rr->u.CE.extent);
			rs.cont_offset = isonum_733(rr->u.CE.offset);
			rs.cont_size = isonum_733(rr->u.CE.size);
		default:
			break;
		}
	}
	ret = rock_continue(&rs);
	if (ret == 0)
		goto repeat;
	if (ret < 0)
		goto fail;

	if (rpnt == link)
		goto fail;
	brelse(bh);
	*rpnt = '\0';
	SetPageUptodate(page);
	kunmap(page);
	unlock_page(page);
	return 0;

	/* error exit from macro */
out:
	kfree(rs.buffer);
	goto fail;
out_noread:
	printk("unable to read i-node block");
	goto fail;
out_bad_span:
	printk("symlink spans iso9660 blocks\n");
fail:
	brelse(bh);
error:
	SetPageError(page);
	kunmap(page);
	unlock_page(page);
	return -EIO;
}

const struct address_space_operations isofs_symlink_aops = {
	.readpage = rock_ridge_symlink_readpage
};
