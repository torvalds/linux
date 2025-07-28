// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/isofs/inode.c
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *      1992, 1993, 1994  Eric Youngdale Modified for ISO 9660 filesystem.
 *      1994  Eberhard Mönkeberg - multi session handling.
 *      1995  Mark Dobie - allow mounting of some weird VideoCDs and PhotoCDs.
 *	1997  Gordon Chaffee - Joliet CDs
 *	1998  Eric Lammerts - ISO 9660 Level 3
 *	2004  Paul Serice - Inode Support pushed out from 4GB to 128GB
 *	2004  Paul Serice - NFS Export Operations
 */

#include <linux/init.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/nls.h>
#include <linux/ctype.h>
#include <linux/statfs.h>
#include <linux/cdrom.h>
#include <linux/mpage.h>
#include <linux/user_namespace.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>

#include "isofs.h"
#include "zisofs.h"

/* max tz offset is 13 hours */
#define MAX_TZ_OFFSET (52*15*60)

#define BEQUIET

static int isofs_hashi(const struct dentry *parent, struct qstr *qstr);
static int isofs_dentry_cmpi(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name);

#ifdef CONFIG_JOLIET
static int isofs_hashi_ms(const struct dentry *parent, struct qstr *qstr);
static int isofs_hash_ms(const struct dentry *parent, struct qstr *qstr);
static int isofs_dentry_cmpi_ms(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name);
static int isofs_dentry_cmp_ms(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name);
#endif

static void isofs_put_super(struct super_block *sb)
{
	struct isofs_sb_info *sbi = ISOFS_SB(sb);

#ifdef CONFIG_JOLIET
	unload_nls(sbi->s_nls_iocharset);
#endif

	kfree(sbi);
	sb->s_fs_info = NULL;
	return;
}

static int isofs_read_inode(struct inode *, int relocated);
static int isofs_statfs (struct dentry *, struct kstatfs *);
static int isofs_show_options(struct seq_file *, struct dentry *);

static struct kmem_cache *isofs_inode_cachep;

static struct inode *isofs_alloc_inode(struct super_block *sb)
{
	struct iso_inode_info *ei;
	ei = alloc_inode_sb(sb, isofs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void isofs_free_inode(struct inode *inode)
{
	kmem_cache_free(isofs_inode_cachep, ISOFS_I(inode));
}

static void init_once(void *foo)
{
	struct iso_inode_info *ei = foo;

	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	isofs_inode_cachep = kmem_cache_create("isofs_inode_cache",
					sizeof(struct iso_inode_info),
					0, (SLAB_RECLAIM_ACCOUNT|
					SLAB_ACCOUNT),
					init_once);
	if (!isofs_inode_cachep)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(isofs_inode_cachep);
}

static int isofs_reconfigure(struct fs_context *fc)
{
	sync_filesystem(fc->root->d_sb);
	if (!(fc->sb_flags & SB_RDONLY))
		return -EROFS;
	return 0;
}

static const struct super_operations isofs_sops = {
	.alloc_inode	= isofs_alloc_inode,
	.free_inode	= isofs_free_inode,
	.put_super	= isofs_put_super,
	.statfs		= isofs_statfs,
	.show_options	= isofs_show_options,
};


static const struct dentry_operations isofs_dentry_ops[] = {
	{
		.d_hash		= isofs_hashi,
		.d_compare	= isofs_dentry_cmpi,
	},
#ifdef CONFIG_JOLIET
	{
		.d_hash		= isofs_hash_ms,
		.d_compare	= isofs_dentry_cmp_ms,
	},
	{
		.d_hash		= isofs_hashi_ms,
		.d_compare	= isofs_dentry_cmpi_ms,
	},
#endif
};

struct isofs_options{
	unsigned int rock:1;
	unsigned int joliet:1;
	unsigned int cruft:1;
	unsigned int hide:1;
	unsigned int showassoc:1;
	unsigned int nocompress:1;
	unsigned int overriderockperm:1;
	unsigned int uid_set:1;
	unsigned int gid_set:1;
	unsigned char map;
	unsigned char check;
	unsigned int blocksize;
	umode_t fmode;
	umode_t dmode;
	kgid_t gid;
	kuid_t uid;
	char *iocharset;
	/* LVE */
	s32 session;
	s32 sbsector;
};

/*
 * Compute the hash for the isofs name corresponding to the dentry.
 */
static int
isofs_hashi_common(const struct dentry *dentry, struct qstr *qstr, int ms)
{
	const char *name;
	int len;
	char c;
	unsigned long hash;

	len = qstr->len;
	name = qstr->name;
	if (ms) {
		while (len && name[len-1] == '.')
			len--;
	}

	hash = init_name_hash(dentry);
	while (len--) {
		c = tolower(*name++);
		hash = partial_name_hash(c, hash);
	}
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Compare of two isofs names.
 */
static int isofs_dentry_cmp_common(
		unsigned int len, const char *str,
		const struct qstr *name, int ms, int ci)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = name->len;
	blen = len;
	if (ms) {
		while (alen && name->name[alen-1] == '.')
			alen--;
		while (blen && str[blen-1] == '.')
			blen--;
	}
	if (alen == blen) {
		if (ci) {
			if (strncasecmp(name->name, str, alen) == 0)
				return 0;
		} else {
			if (strncmp(name->name, str, alen) == 0)
				return 0;
		}
	}
	return 1;
}

static int
isofs_hashi(const struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hashi_common(dentry, qstr, 0);
}

static int
isofs_dentry_cmpi(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	return isofs_dentry_cmp_common(len, str, name, 0, 1);
}

#ifdef CONFIG_JOLIET
/*
 * Compute the hash for the isofs name corresponding to the dentry.
 */
static int
isofs_hash_common(const struct dentry *dentry, struct qstr *qstr, int ms)
{
	const char *name;
	int len;

	len = qstr->len;
	name = qstr->name;
	if (ms) {
		while (len && name[len-1] == '.')
			len--;
	}

	qstr->hash = full_name_hash(dentry, name, len);

	return 0;
}

static int
isofs_hash_ms(const struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hash_common(dentry, qstr, 1);
}

static int
isofs_hashi_ms(const struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hashi_common(dentry, qstr, 1);
}

static int
isofs_dentry_cmp_ms(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	return isofs_dentry_cmp_common(len, str, name, 1, 0);
}

static int
isofs_dentry_cmpi_ms(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	return isofs_dentry_cmp_common(len, str, name, 1, 1);
}
#endif

enum {
	Opt_block, Opt_check, Opt_cruft, Opt_gid, Opt_ignore, Opt_iocharset,
	Opt_map, Opt_mode, Opt_nojoliet, Opt_norock, Opt_sb, Opt_session,
	Opt_uid, Opt_unhide, Opt_utf8, Opt_err, Opt_nocompress, Opt_hide,
	Opt_showassoc, Opt_dmode, Opt_overriderockperm,
};

static const struct constant_table isofs_param_map[] = {
	{"acorn",	'a'},
	{"a",		'a'},
	{"normal",	'n'},
	{"n",		'n'},
	{"off",		'o'},
	{"o",		'o'},
	{}
};

static const struct constant_table isofs_param_check[] = {
	{"relaxed",	'r'},
	{"r",		'r'},
	{"strict",	's'},
	{"s",		's'},
	{}
};

static const struct fs_parameter_spec isofs_param_spec[] = {
	fsparam_flag	("norock",		Opt_norock),
	fsparam_flag	("nojoliet",		Opt_nojoliet),
	fsparam_flag	("unhide",		Opt_unhide),
	fsparam_flag	("hide",		Opt_hide),
	fsparam_flag	("showassoc",		Opt_showassoc),
	fsparam_flag	("cruft",		Opt_cruft),
	fsparam_flag	("utf8",		Opt_utf8),
	fsparam_string	("iocharset",		Opt_iocharset),
	fsparam_enum	("map",			Opt_map, isofs_param_map),
	fsparam_u32	("session",		Opt_session),
	fsparam_u32	("sbsector",		Opt_sb),
	fsparam_enum	("check",		Opt_check, isofs_param_check),
	fsparam_uid	("uid",			Opt_uid),
	fsparam_gid	("gid",			Opt_gid),
	/* Note: mode/dmode historically accepted %u not strictly %o */
	fsparam_u32	("mode",		Opt_mode),
	fsparam_u32	("dmode",		Opt_dmode),
	fsparam_flag	("overriderockperm",	Opt_overriderockperm),
	fsparam_u32	("block",		Opt_block),
	fsparam_string	("conv",		Opt_ignore),
	fsparam_flag	("nocompress",		Opt_nocompress),
	{}
};

static int isofs_parse_param(struct fs_context *fc,
			       struct fs_parameter *param)
{
	struct isofs_options *popt = fc->fs_private;
	struct fs_parse_result result;
	int opt;
	unsigned int n;

	/* There are no remountable options */
	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE)
		return 0;

	opt = fs_parse(fc, isofs_param_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_norock:
		popt->rock = 0;
		break;
	case Opt_nojoliet:
		popt->joliet = 0;
		break;
	case Opt_hide:
		popt->hide = 1;
		break;
	case Opt_unhide:
	case Opt_showassoc:
		popt->showassoc = 1;
		break;
	case Opt_cruft:
		popt->cruft = 1;
		break;
#ifdef CONFIG_JOLIET
	case Opt_utf8:
		kfree(popt->iocharset);
		popt->iocharset = kstrdup("utf8", GFP_KERNEL);
		if (!popt->iocharset)
			return -ENOMEM;
		break;
	case Opt_iocharset:
		kfree(popt->iocharset);
		popt->iocharset = kstrdup(param->string, GFP_KERNEL);
		if (!popt->iocharset)
			return -ENOMEM;
		break;
#endif
	case Opt_map:
		popt->map = result.uint_32;
		break;
	case Opt_session:
		n = result.uint_32;
		/*
		 * Track numbers are supposed to be in range 1-99, the
		 * mount option starts indexing at 0.
		 */
		if (n >= 99)
			return -EINVAL;
		popt->session = n + 1;
		break;
	case Opt_sb:
		popt->sbsector = result.uint_32;
		break;
	case Opt_check:
		popt->check = result.uint_32;
		break;
	case Opt_ignore:
		break;
	case Opt_uid:
		popt->uid = result.uid;
		popt->uid_set = 1;
		break;
	case Opt_gid:
		popt->gid = result.gid;
		popt->gid_set = 1;
		break;
	case Opt_mode:
		popt->fmode = result.uint_32;
		break;
	case Opt_dmode:
		popt->dmode = result.uint_32;
		break;
	case Opt_overriderockperm:
		popt->overriderockperm = 1;
		break;
	case Opt_block:
		n = result.uint_32;
		if (n != 512 && n != 1024 && n != 2048)
			return -EINVAL;
		popt->blocksize = n;
		break;
	case Opt_nocompress:
		popt->nocompress = 1;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Display the mount options in /proc/mounts.
 */
static int isofs_show_options(struct seq_file *m, struct dentry *root)
{
	struct isofs_sb_info *sbi = ISOFS_SB(root->d_sb);

	if (!sbi->s_rock)		seq_puts(m, ",norock");
	else if (!sbi->s_joliet_level)	seq_puts(m, ",nojoliet");
	if (sbi->s_cruft)		seq_puts(m, ",cruft");
	if (sbi->s_hide)		seq_puts(m, ",hide");
	if (sbi->s_nocompress)		seq_puts(m, ",nocompress");
	if (sbi->s_overriderockperm)	seq_puts(m, ",overriderockperm");
	if (sbi->s_showassoc)		seq_puts(m, ",showassoc");

	if (sbi->s_check)		seq_printf(m, ",check=%c", sbi->s_check);
	if (sbi->s_mapping)		seq_printf(m, ",map=%c", sbi->s_mapping);
	if (sbi->s_session != 255)	seq_printf(m, ",session=%u", sbi->s_session - 1);
	if (sbi->s_sbsector != -1)	seq_printf(m, ",sbsector=%u", sbi->s_sbsector);

	if (root->d_sb->s_blocksize != 1024)
		seq_printf(m, ",blocksize=%lu", root->d_sb->s_blocksize);

	if (sbi->s_uid_set)
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, sbi->s_uid));
	if (sbi->s_gid_set)
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, sbi->s_gid));

	if (sbi->s_dmode != ISOFS_INVALID_MODE)
		seq_printf(m, ",dmode=%o", sbi->s_dmode);
	if (sbi->s_fmode != ISOFS_INVALID_MODE)
		seq_printf(m, ",fmode=%o", sbi->s_fmode);

#ifdef CONFIG_JOLIET
	if (sbi->s_nls_iocharset)
		seq_printf(m, ",iocharset=%s", sbi->s_nls_iocharset->charset);
	else
		seq_puts(m, ",iocharset=utf8");
#endif
	return 0;
}

/*
 * look if the driver can tell the multi session redirection value
 *
 * don't change this if you don't know what you do, please!
 * Multisession is legal only with XA disks.
 * A non-XA disk with more than one volume descriptor may do it right, but
 * usually is written in a nowhere standardized "multi-partition" manner.
 * Multisession uses absolute addressing (solely the first frame of the whole
 * track is #0), multi-partition uses relative addressing (each first frame of
 * each track is #0), and a track is not a session.
 *
 * A broken CDwriter software or drive firmware does not set new standards,
 * at least not if conflicting with the existing ones.
 *
 * emoenke@gwdg.de
 */
#define WE_OBEY_THE_WRITTEN_STANDARDS 1

static unsigned int isofs_get_last_session(struct super_block *sb, s32 session)
{
	struct cdrom_device_info *cdi = disk_to_cdi(sb->s_bdev->bd_disk);
	unsigned int vol_desc_start = 0;

	if (session > 0) {
		struct cdrom_tocentry te;

		if (!cdi)
			return 0;

		te.cdte_track = session;
		te.cdte_format = CDROM_LBA;
		if (cdrom_read_tocentry(cdi, &te) == 0) {
			printk(KERN_DEBUG "ISOFS: Session %d start %d type %d\n",
				session, te.cdte_addr.lba,
				te.cdte_ctrl & CDROM_DATA_TRACK);
			if ((te.cdte_ctrl & CDROM_DATA_TRACK) == 4)
				return te.cdte_addr.lba;
		}

		printk(KERN_ERR "ISOFS: Invalid session number or type of track\n");
	}

	if (cdi) {
		struct cdrom_multisession ms_info;

		ms_info.addr_format = CDROM_LBA;
		if (cdrom_multisession(cdi, &ms_info) == 0) {
#if WE_OBEY_THE_WRITTEN_STANDARDS
			/* necessary for a valid ms_info.addr */
			if (ms_info.xa_flag)
#endif
				vol_desc_start = ms_info.addr.lba;
		}
	}

	return vol_desc_start;
}

/*
 * Check if root directory is empty (has less than 3 files).
 *
 * Used to detect broken CDs where ISO root directory is empty but Joliet root
 * directory is OK. If such CD has Rock Ridge extensions, they will be disabled
 * (and Joliet used instead) or else no files would be visible.
 */
static bool rootdir_empty(struct super_block *sb, unsigned long block)
{
	int offset = 0, files = 0, de_len;
	struct iso_directory_record *de;
	struct buffer_head *bh;

	bh = sb_bread(sb, block);
	if (!bh)
		return true;
	while (files < 3) {
		de = (struct iso_directory_record *) (bh->b_data + offset);
		de_len = *(unsigned char *) de;
		if (de_len == 0)
			break;
		files++;
		offset += de_len;
	}
	brelse(bh);
	return files < 3;
}

/*
 * Initialize the superblock and read the root inode.
 */
static int isofs_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct buffer_head *bh = NULL, *pri_bh = NULL;
	struct hs_primary_descriptor *h_pri = NULL;
	struct iso_primary_descriptor *pri = NULL;
	struct iso_supplementary_descriptor *sec = NULL;
	struct iso_directory_record *rootp;
	struct inode *inode;
	struct isofs_options *opt = fc->fs_private;
	struct isofs_sb_info *sbi;
	unsigned long first_data_zone;
	int joliet_level = 0;
	int iso_blknum, block;
	int orig_zonesize;
	int table, error = -EINVAL;
	unsigned int vol_desc_start;
	int silent = fc->sb_flags & SB_SILENT;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;

	/*
	 * First of all, get the hardware blocksize for this device.
	 * If we don't know what it is, or the hardware blocksize is
	 * larger than the blocksize the user specified, then use
	 * that value.
	 */
	/*
	 * What if bugger tells us to go beyond page size?
	 */
	if (bdev_logical_block_size(s->s_bdev) > 2048) {
		printk(KERN_WARNING
		       "ISOFS: unsupported/invalid hardware sector size %d\n",
			bdev_logical_block_size(s->s_bdev));
		goto out_freesbi;
	}
	opt->blocksize = sb_min_blocksize(s, opt->blocksize);

	sbi->s_high_sierra = 0; /* default is iso9660 */
	sbi->s_session = opt->session;
	sbi->s_sbsector = opt->sbsector;

	vol_desc_start = (opt->sbsector != -1) ?
		opt->sbsector : isofs_get_last_session(s, opt->session);

	for (iso_blknum = vol_desc_start+16;
		iso_blknum < vol_desc_start+100; iso_blknum++) {
		struct hs_volume_descriptor *hdp;
		struct iso_volume_descriptor  *vdp;

		block = iso_blknum << (ISOFS_BLOCK_BITS - s->s_blocksize_bits);
		if (!(bh = sb_bread(s, block)))
			goto out_no_read;

		vdp = (struct iso_volume_descriptor *)bh->b_data;
		hdp = (struct hs_volume_descriptor *)bh->b_data;

		/*
		 * Due to the overlapping physical location of the descriptors,
		 * ISO CDs can match hdp->id==HS_STANDARD_ID as well. To ensure
		 * proper identification in this case, we first check for ISO.
		 */
		if (strncmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) == 0) {
			if (isonum_711(vdp->type) == ISO_VD_END)
				break;
			if (isonum_711(vdp->type) == ISO_VD_PRIMARY) {
				if (!pri) {
					pri = (struct iso_primary_descriptor *)vdp;
					/* Save the buffer in case we need it ... */
					pri_bh = bh;
					bh = NULL;
				}
			}
#ifdef CONFIG_JOLIET
			else if (isonum_711(vdp->type) == ISO_VD_SUPPLEMENTARY) {
				sec = (struct iso_supplementary_descriptor *)vdp;
				if (sec->escape[0] == 0x25 && sec->escape[1] == 0x2f) {
					if (opt->joliet) {
						if (sec->escape[2] == 0x40)
							joliet_level = 1;
						else if (sec->escape[2] == 0x43)
							joliet_level = 2;
						else if (sec->escape[2] == 0x45)
							joliet_level = 3;

						printk(KERN_DEBUG "ISO 9660 Extensions: "
							"Microsoft Joliet Level %d\n",
							joliet_level);
					}
					goto root_found;
				} else {
				/* Unknown supplementary volume descriptor */
				sec = NULL;
				}
			}
#endif
		} else {
			if (strncmp (hdp->id, HS_STANDARD_ID, sizeof hdp->id) == 0) {
				if (isonum_711(hdp->type) != ISO_VD_PRIMARY)
					goto out_freebh;

				sbi->s_high_sierra = 1;
				opt->rock = 0;
				h_pri = (struct hs_primary_descriptor *)vdp;
				goto root_found;
			}
		}

		/* Just skip any volume descriptors we don't recognize */

		brelse(bh);
		bh = NULL;
	}
	/*
	 * If we fall through, either no volume descriptor was found,
	 * or else we passed a primary descriptor looking for others.
	 */
	if (!pri)
		goto out_unknown_format;
	brelse(bh);
	bh = pri_bh;
	pri_bh = NULL;

root_found:
	/* We don't support read-write mounts */
	if (!sb_rdonly(s)) {
		error = -EACCES;
		goto out_freebh;
	}

	if (joliet_level && (!pri || !opt->rock)) {
		/* This is the case of Joliet with the norock mount flag.
		 * A disc with both Joliet and Rock Ridge is handled later
		 */
		pri = (struct iso_primary_descriptor *) sec;
	}

	if(sbi->s_high_sierra){
		rootp = (struct iso_directory_record *) h_pri->root_directory_record;
		sbi->s_nzones = isonum_733(h_pri->volume_space_size);
		sbi->s_log_zone_size = isonum_723(h_pri->logical_block_size);
		sbi->s_max_size = isonum_733(h_pri->volume_space_size);
	} else {
		if (!pri)
			goto out_freebh;
		rootp = (struct iso_directory_record *) pri->root_directory_record;
		sbi->s_nzones = isonum_733(pri->volume_space_size);
		sbi->s_log_zone_size = isonum_723(pri->logical_block_size);
		sbi->s_max_size = isonum_733(pri->volume_space_size);
	}

	sbi->s_ninodes = 0; /* No way to figure this out easily */

	orig_zonesize = sbi->s_log_zone_size;
	/*
	 * If the zone size is smaller than the hardware sector size,
	 * this is a fatal error.  This would occur if the disc drive
	 * had sectors that were 2048 bytes, but the filesystem had
	 * blocks that were 512 bytes (which should only very rarely
	 * happen.)
	 */
	if (orig_zonesize < opt->blocksize)
		goto out_bad_size;

	/* RDE: convert log zone size to bit shift */
	switch (sbi->s_log_zone_size) {
	case  512: sbi->s_log_zone_size =  9; break;
	case 1024: sbi->s_log_zone_size = 10; break;
	case 2048: sbi->s_log_zone_size = 11; break;

	default:
		goto out_bad_zone_size;
	}

	s->s_magic = ISOFS_SUPER_MAGIC;

	/*
	 * With multi-extent files, file size is only limited by the maximum
	 * size of a file system, which is 8 TB.
	 */
	s->s_maxbytes = 0x80000000000LL;

	/* ECMA-119 timestamp from 1900/1/1 with tz offset */
	s->s_time_min = mktime64(1900, 1, 1, 0, 0, 0) - MAX_TZ_OFFSET;
	s->s_time_max = mktime64(U8_MAX+1900, 12, 31, 23, 59, 59) + MAX_TZ_OFFSET;

	/* Set this for reference. Its not currently used except on write
	   which we don't have .. */

	first_data_zone = isonum_733(rootp->extent) +
			  isonum_711(rootp->ext_attr_length);
	sbi->s_firstdatazone = first_data_zone;
#ifndef BEQUIET
	printk(KERN_DEBUG "ISOFS: Max size:%ld   Log zone size:%ld\n",
		sbi->s_max_size, 1UL << sbi->s_log_zone_size);
	printk(KERN_DEBUG "ISOFS: First datazone:%ld\n", sbi->s_firstdatazone);
	if(sbi->s_high_sierra)
		printk(KERN_DEBUG "ISOFS: Disc in High Sierra format.\n");
#endif

	/*
	 * If the Joliet level is set, we _may_ decide to use the
	 * secondary descriptor, but can't be sure until after we
	 * read the root inode. But before reading the root inode
	 * we may need to change the device blocksize, and would
	 * rather release the old buffer first. So, we cache the
	 * first_data_zone value from the secondary descriptor.
	 */
	if (joliet_level) {
		pri = (struct iso_primary_descriptor *) sec;
		rootp = (struct iso_directory_record *)
			pri->root_directory_record;
		first_data_zone = isonum_733(rootp->extent) +
				isonum_711(rootp->ext_attr_length);
	}

	/*
	 * We're all done using the volume descriptor, and may need
	 * to change the device blocksize, so release the buffer now.
	 */
	brelse(pri_bh);
	brelse(bh);

	/*
	 * Force the blocksize to 512 for 512 byte sectors.  The file
	 * read primitives really get it wrong in a bad way if we don't
	 * do this.
	 *
	 * Note - we should never be setting the blocksize to something
	 * less than the hardware sector size for the device.  If we
	 * do, we would end up having to read larger buffers and split
	 * out portions to satisfy requests.
	 *
	 * Note2- the idea here is that we want to deal with the optimal
	 * zonesize in the filesystem.  If we have it set to something less,
	 * then we have horrible problems with trying to piece together
	 * bits of adjacent blocks in order to properly read directory
	 * entries.  By forcing the blocksize in this way, we ensure
	 * that we will never be required to do this.
	 */
	sb_set_blocksize(s, orig_zonesize);

	sbi->s_nls_iocharset = NULL;

#ifdef CONFIG_JOLIET
	if (joliet_level) {
		char *p = opt->iocharset ? opt->iocharset : CONFIG_NLS_DEFAULT;
		if (strcmp(p, "utf8") != 0) {
			sbi->s_nls_iocharset = opt->iocharset ?
				load_nls(opt->iocharset) : load_nls_default();
			if (!sbi->s_nls_iocharset)
				goto out_freesbi;
		}
	}
#endif
	s->s_op = &isofs_sops;
	s->s_export_op = &isofs_export_ops;
	sbi->s_mapping = opt->map;
	sbi->s_rock = (opt->rock ? 2 : 0);
	sbi->s_rock_offset = -1; /* initial offset, will guess until SP is found*/
	sbi->s_cruft = opt->cruft;
	sbi->s_hide = opt->hide;
	sbi->s_showassoc = opt->showassoc;
	sbi->s_uid = opt->uid;
	sbi->s_gid = opt->gid;
	sbi->s_uid_set = opt->uid_set;
	sbi->s_gid_set = opt->gid_set;
	sbi->s_nocompress = opt->nocompress;
	sbi->s_overriderockperm = opt->overriderockperm;
	/*
	 * It would be incredibly stupid to allow people to mark every file
	 * on the disk as suid, so we merely allow them to set the default
	 * permissions.
	 */
	if (opt->fmode != ISOFS_INVALID_MODE)
		sbi->s_fmode = opt->fmode & 0777;
	else
		sbi->s_fmode = ISOFS_INVALID_MODE;
	if (opt->dmode != ISOFS_INVALID_MODE)
		sbi->s_dmode = opt->dmode & 0777;
	else
		sbi->s_dmode = ISOFS_INVALID_MODE;

	/*
	 * Read the root inode, which _may_ result in changing
	 * the s_rock flag. Once we have the final s_rock value,
	 * we then decide whether to use the Joliet descriptor.
	 */
	inode = isofs_iget(s, sbi->s_firstdatazone, 0);

	/*
	 * Fix for broken CDs with a corrupt root inode but a correct Joliet
	 * root directory.
	 */
	if (IS_ERR(inode)) {
		if (joliet_level && sbi->s_firstdatazone != first_data_zone) {
			printk(KERN_NOTICE
			       "ISOFS: root inode is unusable. "
			       "Disabling Rock Ridge and switching to Joliet.");
			sbi->s_rock = 0;
			inode = NULL;
		} else {
			goto out_no_root;
		}
	}

	/*
	 * Fix for broken CDs with Rock Ridge and empty ISO root directory but
	 * correct Joliet root directory.
	 */
	if (sbi->s_rock == 1 && joliet_level &&
				rootdir_empty(s, sbi->s_firstdatazone)) {
		printk(KERN_NOTICE
			"ISOFS: primary root directory is empty. "
			"Disabling Rock Ridge and switching to Joliet.");
		sbi->s_rock = 0;
	}

	/*
	 * If this disk has both Rock Ridge and Joliet on it, then we
	 * want to use Rock Ridge by default.  This can be overridden
	 * by using the norock mount option.  There is still one other
	 * possibility that is not taken into account: a Rock Ridge
	 * CD with Unicode names.  Until someone sees such a beast, it
	 * will not be supported.
	 */
	if (sbi->s_rock == 1) {
		joliet_level = 0;
	} else if (joliet_level) {
		sbi->s_rock = 0;
		if (sbi->s_firstdatazone != first_data_zone) {
			sbi->s_firstdatazone = first_data_zone;
			printk(KERN_DEBUG
				"ISOFS: changing to secondary root\n");
			iput(inode);
			inode = isofs_iget(s, sbi->s_firstdatazone, 0);
			if (IS_ERR(inode))
				goto out_no_root;
		}
	}

	if (opt->check == 'u') {
		/* Only Joliet is case insensitive by default */
		if (joliet_level)
			opt->check = 'r';
		else
			opt->check = 's';
	}
	sbi->s_joliet_level = joliet_level;

	/* Make sure the root inode is a directory */
	if (!S_ISDIR(inode->i_mode)) {
		printk(KERN_WARNING
			"isofs_fill_super: root inode is not a directory. "
			"Corrupted media?\n");
		goto out_iput;
	}

	table = 0;
	if (joliet_level)
		table += 2;
	if (opt->check == 'r')
		table++;
	sbi->s_check = opt->check;

	if (table)
		s->s_d_op = &isofs_dentry_ops[table - 1];

	/* get the root dentry */
	s->s_root = d_make_root(inode);
	if (!(s->s_root)) {
		error = -ENOMEM;
		goto out_no_inode;
	}

	return 0;

	/*
	 * Display error messages and free resources.
	 */
out_iput:
	iput(inode);
	goto out_no_inode;
out_no_root:
	error = PTR_ERR(inode);
	if (error != -ENOMEM)
		printk(KERN_WARNING "%s: get root inode failed\n", __func__);
out_no_inode:
#ifdef CONFIG_JOLIET
	unload_nls(sbi->s_nls_iocharset);
#endif
	goto out_freesbi;
out_no_read:
	printk(KERN_WARNING "%s: bread failed, dev=%s, iso_blknum=%d, block=%d\n",
		__func__, s->s_id, iso_blknum, block);
	goto out_freebh;
out_bad_zone_size:
	printk(KERN_WARNING "ISOFS: Bad logical zone size %ld\n",
		sbi->s_log_zone_size);
	goto out_freebh;
out_bad_size:
	printk(KERN_WARNING "ISOFS: Logical zone size(%d) < hardware blocksize(%u)\n",
		orig_zonesize, opt->blocksize);
	goto out_freebh;
out_unknown_format:
	if (!silent)
		printk(KERN_WARNING "ISOFS: Unable to identify CD-ROM format.\n");

out_freebh:
	brelse(bh);
	brelse(pri_bh);
out_freesbi:
	kfree(sbi);
	s->s_fs_info = NULL;
	return error;
}

static int isofs_statfs (struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = ISOFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (ISOFS_SB(sb)->s_nzones
		<< (ISOFS_SB(sb)->s_log_zone_size - sb->s_blocksize_bits));
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = ISOFS_SB(sb)->s_ninodes;
	buf->f_ffree = 0;
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Get a set of blocks; filling in buffer_heads if already allocated
 * or getblk() if they are not.  Returns the number of blocks inserted
 * (-ve == error.)
 */
int isofs_get_blocks(struct inode *inode, sector_t iblock,
		     struct buffer_head **bh, unsigned long nblocks)
{
	unsigned long b_off = iblock;
	unsigned offset, sect_size;
	unsigned int firstext;
	unsigned long nextblk, nextoff;
	int section, rv, error;
	struct iso_inode_info *ei = ISOFS_I(inode);

	error = -EIO;
	rv = 0;
	if (iblock != b_off) {
		printk(KERN_DEBUG "%s: block number too large\n", __func__);
		goto abort;
	}


	offset = 0;
	firstext = ei->i_first_extent;
	sect_size = ei->i_section_size >> ISOFS_BUFFER_BITS(inode);
	nextblk = ei->i_next_section_block;
	nextoff = ei->i_next_section_offset;
	section = 0;

	while (nblocks) {
		/* If we are *way* beyond the end of the file, print a message.
		 * Access beyond the end of the file up to the next page boundary
		 * is normal, however because of the way the page cache works.
		 * In this case, we just return 0 so that we can properly fill
		 * the page with useless information without generating any
		 * I/O errors.
		 */
		if (b_off > ((inode->i_size + PAGE_SIZE - 1) >> ISOFS_BUFFER_BITS(inode))) {
			printk(KERN_DEBUG "%s: block >= EOF (%lu, %llu)\n",
				__func__, b_off,
				(unsigned long long)inode->i_size);
			goto abort;
		}

		/* On the last section, nextblk == 0, section size is likely to
		 * exceed sect_size by a partial block, and access beyond the
		 * end of the file will reach beyond the section size, too.
		 */
		while (nextblk && (b_off >= (offset + sect_size))) {
			struct inode *ninode;

			offset += sect_size;
			ninode = isofs_iget(inode->i_sb, nextblk, nextoff);
			if (IS_ERR(ninode)) {
				error = PTR_ERR(ninode);
				goto abort;
			}
			firstext  = ISOFS_I(ninode)->i_first_extent;
			sect_size = ISOFS_I(ninode)->i_section_size >> ISOFS_BUFFER_BITS(ninode);
			nextblk   = ISOFS_I(ninode)->i_next_section_block;
			nextoff   = ISOFS_I(ninode)->i_next_section_offset;
			iput(ninode);

			if (++section > 100) {
				printk(KERN_DEBUG "%s: More than 100 file sections ?!?"
					" aborting...\n", __func__);
				printk(KERN_DEBUG "%s: block=%lu firstext=%u sect_size=%u "
					"nextblk=%lu nextoff=%lu\n", __func__,
					b_off, firstext, (unsigned) sect_size,
					nextblk, nextoff);
				goto abort;
			}
		}

		if (*bh) {
			map_bh(*bh, inode->i_sb, firstext + b_off - offset);
		} else {
			*bh = sb_getblk(inode->i_sb, firstext+b_off-offset);
			if (!*bh)
				goto abort;
		}
		bh++;	/* Next buffer head */
		b_off++;	/* Next buffer offset */
		nblocks--;
		rv++;
	}

	error = 0;
abort:
	return rv != 0 ? rv : error;
}

/*
 * Used by the standard interfaces.
 */
static int isofs_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	int ret;

	if (create) {
		printk(KERN_DEBUG "%s: Kernel tries to allocate a block\n", __func__);
		return -EROFS;
	}

	ret = isofs_get_blocks(inode, iblock, &bh_result, 1);
	return ret < 0 ? ret : 0;
}

static int isofs_bmap(struct inode *inode, sector_t block)
{
	struct buffer_head dummy;
	int error;

	dummy.b_state = 0;
	dummy.b_blocknr = -1000;
	error = isofs_get_block(inode, block, &dummy, 0);
	if (!error)
		return dummy.b_blocknr;
	return 0;
}

struct buffer_head *isofs_bread(struct inode *inode, sector_t block)
{
	sector_t blknr = isofs_bmap(inode, block);
	if (!blknr)
		return NULL;
	return sb_bread(inode->i_sb, blknr);
}

static int isofs_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, isofs_get_block);
}

static void isofs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, isofs_get_block);
}

static sector_t _isofs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,isofs_get_block);
}

static const struct address_space_operations isofs_aops = {
	.read_folio = isofs_read_folio,
	.readahead = isofs_readahead,
	.bmap = _isofs_bmap
};

static int isofs_read_level3_size(struct inode *inode)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	int high_sierra = ISOFS_SB(inode->i_sb)->s_high_sierra;
	struct buffer_head *bh = NULL;
	unsigned long block, offset, block_saved, offset_saved;
	int i = 0;
	int more_entries = 0;
	struct iso_directory_record *tmpde = NULL;
	struct iso_inode_info *ei = ISOFS_I(inode);

	inode->i_size = 0;

	/* The first 16 blocks are reserved as the System Area.  Thus,
	 * no inodes can appear in block 0.  We use this to flag that
	 * this is the last section. */
	ei->i_next_section_block = 0;
	ei->i_next_section_offset = 0;

	block = ei->i_iget5_block;
	offset = ei->i_iget5_offset;

	do {
		struct iso_directory_record *de;
		unsigned int de_len;

		if (!bh) {
			bh = sb_bread(inode->i_sb, block);
			if (!bh)
				goto out_noread;
		}
		de = (struct iso_directory_record *) (bh->b_data + offset);
		de_len = *(unsigned char *) de;

		if (de_len == 0) {
			brelse(bh);
			bh = NULL;
			++block;
			offset = 0;
			continue;
		}

		block_saved = block;
		offset_saved = offset;
		offset += de_len;

		/* Make sure we have a full directory entry */
		if (offset >= bufsize) {
			int slop = bufsize - offset + de_len;
			if (!tmpde) {
				tmpde = kmalloc(256, GFP_KERNEL);
				if (!tmpde)
					goto out_nomem;
			}
			memcpy(tmpde, de, slop);
			offset &= bufsize - 1;
			block++;
			brelse(bh);
			bh = NULL;
			if (offset) {
				bh = sb_bread(inode->i_sb, block);
				if (!bh)
					goto out_noread;
				memcpy((void *)tmpde+slop, bh->b_data, offset);
			}
			de = tmpde;
		}

		inode->i_size += isonum_733(de->size);
		if (i == 1) {
			ei->i_next_section_block = block_saved;
			ei->i_next_section_offset = offset_saved;
		}

		more_entries = de->flags[-high_sierra] & 0x80;

		i++;
		if (i > 100)
			goto out_toomany;
	} while (more_entries);
out:
	kfree(tmpde);
	brelse(bh);
	return 0;

out_nomem:
	brelse(bh);
	return -ENOMEM;

out_noread:
	printk(KERN_INFO "ISOFS: unable to read i-node block %lu\n", block);
	kfree(tmpde);
	return -EIO;

out_toomany:
	printk(KERN_INFO "%s: More than 100 file sections ?!?, aborting...\n"
		"isofs_read_level3_size: inode=%lu\n",
		__func__, inode->i_ino);
	goto out;
}

static int isofs_read_inode(struct inode *inode, int relocated)
{
	struct super_block *sb = inode->i_sb;
	struct isofs_sb_info *sbi = ISOFS_SB(sb);
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	unsigned long block;
	int high_sierra = sbi->s_high_sierra;
	struct buffer_head *bh;
	struct iso_directory_record *de;
	struct iso_directory_record *tmpde = NULL;
	unsigned int de_len;
	unsigned long offset;
	struct iso_inode_info *ei = ISOFS_I(inode);
	int ret = -EIO;
	struct timespec64 ts;

	block = ei->i_iget5_block;
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		goto out_badread;

	offset = ei->i_iget5_offset;

	de = (struct iso_directory_record *) (bh->b_data + offset);
	de_len = *(unsigned char *) de;
	if (de_len < sizeof(struct iso_directory_record))
		goto fail;

	if (offset + de_len > bufsize) {
		int frag1 = bufsize - offset;

		tmpde = kmalloc(de_len, GFP_KERNEL);
		if (!tmpde) {
			ret = -ENOMEM;
			goto fail;
		}
		memcpy(tmpde, bh->b_data + offset, frag1);
		brelse(bh);
		bh = sb_bread(inode->i_sb, ++block);
		if (!bh)
			goto out_badread;
		memcpy((char *)tmpde+frag1, bh->b_data, de_len - frag1);
		de = tmpde;
	}

	inode->i_ino = isofs_get_ino(ei->i_iget5_block,
					ei->i_iget5_offset,
					ISOFS_BUFFER_BITS(inode));

	/* Assume it is a normal-format file unless told otherwise */
	ei->i_file_format = isofs_file_normal;

	if (de->flags[-high_sierra] & 2) {
		if (sbi->s_dmode != ISOFS_INVALID_MODE)
			inode->i_mode = S_IFDIR | sbi->s_dmode;
		else
			inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
		set_nlink(inode, 1);	/*
					 * Set to 1.  We know there are 2, but
					 * the find utility tries to optimize
					 * if it is 2, and it screws up.  It is
					 * easier to give 1 which tells find to
					 * do it the hard way.
					 */
	} else {
		if (sbi->s_fmode != ISOFS_INVALID_MODE) {
			inode->i_mode = S_IFREG | sbi->s_fmode;
		} else {
			/*
			 * Set default permissions: r-x for all.  The disc
			 * could be shared with DOS machines so virtually
			 * anything could be a valid executable.
			 */
			inode->i_mode = S_IFREG | S_IRUGO | S_IXUGO;
		}
		set_nlink(inode, 1);
	}
	inode->i_uid = sbi->s_uid;
	inode->i_gid = sbi->s_gid;
	inode->i_blocks = 0;

	ei->i_format_parm[0] = 0;
	ei->i_format_parm[1] = 0;
	ei->i_format_parm[2] = 0;

	ei->i_section_size = isonum_733(de->size);
	if (de->flags[-high_sierra] & 0x80) {
		ret = isofs_read_level3_size(inode);
		if (ret < 0)
			goto fail;
		ret = -EIO;
	} else {
		ei->i_next_section_block = 0;
		ei->i_next_section_offset = 0;
		inode->i_size = isonum_733(de->size);
	}

	/*
	 * Some dipshit decided to store some other bit of information
	 * in the high byte of the file length.  Truncate size in case
	 * this CDROM was mounted with the cruft option.
	 */

	if (sbi->s_cruft)
		inode->i_size &= 0x00ffffff;

	if (de->interleave[0]) {
		printk(KERN_DEBUG "ISOFS: Interleaved files not (yet) supported.\n");
		inode->i_size = 0;
	}

	/* I have no idea what file_unit_size is used for, so
	   we will flag it for now */
	if (de->file_unit_size[0] != 0) {
		printk(KERN_DEBUG "ISOFS: File unit size != 0 for ISO file (%ld).\n",
			inode->i_ino);
	}

	/* I have no idea what other flag bits are used for, so
	   we will flag it for now */
#ifdef DEBUG
	if((de->flags[-high_sierra] & ~2)!= 0){
		printk(KERN_DEBUG "ISOFS: Unusual flag settings for ISO file "
				"(%ld %x).\n",
			inode->i_ino, de->flags[-high_sierra]);
	}
#endif
	ts = iso_date(de->date, high_sierra ? ISO_DATE_HIGH_SIERRA : 0);
	inode_set_ctime_to_ts(inode, ts);
	inode_set_atime_to_ts(inode, ts);
	inode_set_mtime_to_ts(inode, ts);

	ei->i_first_extent = (isonum_733(de->extent) +
			isonum_711(de->ext_attr_length));

	/* Set the number of blocks for stat() - should be done before RR */
	inode->i_blocks = (inode->i_size + 511) >> 9;

	/*
	 * Now test for possible Rock Ridge extensions which will override
	 * some of these numbers in the inode structure.
	 */

	if (!high_sierra) {
		parse_rock_ridge_inode(de, inode, relocated);
		/* if we want uid/gid set, override the rock ridge setting */
		if (sbi->s_uid_set)
			inode->i_uid = sbi->s_uid;
		if (sbi->s_gid_set)
			inode->i_gid = sbi->s_gid;
	}
	/* Now set final access rights if overriding rock ridge setting */
	if (S_ISDIR(inode->i_mode) && sbi->s_overriderockperm &&
	    sbi->s_dmode != ISOFS_INVALID_MODE)
		inode->i_mode = S_IFDIR | sbi->s_dmode;
	if (S_ISREG(inode->i_mode) && sbi->s_overriderockperm &&
	    sbi->s_fmode != ISOFS_INVALID_MODE)
		inode->i_mode = S_IFREG | sbi->s_fmode;

	/* Install the inode operations vector */
	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &generic_ro_fops;
		switch (ei->i_file_format) {
#ifdef CONFIG_ZISOFS
		case isofs_file_compressed:
			inode->i_data.a_ops = &zisofs_aops;
			break;
#endif
		default:
			inode->i_data.a_ops = &isofs_aops;
			break;
		}
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &isofs_dir_inode_operations;
		inode->i_fop = &isofs_dir_operations;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &page_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_data.a_ops = &isofs_symlink_aops;
	} else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
		   S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		/* XXX - parse_rock_ridge_inode() had already set i_rdev. */
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
	} else {
		printk(KERN_DEBUG "ISOFS: Invalid file type 0%04o for inode %lu.\n",
			inode->i_mode, inode->i_ino);
		ret = -EIO;
		goto fail;
	}

	ret = 0;
out:
	kfree(tmpde);
	brelse(bh);
	return ret;

out_badread:
	printk(KERN_WARNING "ISOFS: unable to read i-node block\n");
fail:
	goto out;
}

struct isofs_iget5_callback_data {
	unsigned long block;
	unsigned long offset;
};

static int isofs_iget5_test(struct inode *ino, void *data)
{
	struct iso_inode_info *i = ISOFS_I(ino);
	struct isofs_iget5_callback_data *d =
		(struct isofs_iget5_callback_data*)data;
	return (i->i_iget5_block == d->block)
		&& (i->i_iget5_offset == d->offset);
}

static int isofs_iget5_set(struct inode *ino, void *data)
{
	struct iso_inode_info *i = ISOFS_I(ino);
	struct isofs_iget5_callback_data *d =
		(struct isofs_iget5_callback_data*)data;
	i->i_iget5_block = d->block;
	i->i_iget5_offset = d->offset;
	return 0;
}

/* Store, in the inode's containing structure, the block and block
 * offset that point to the underlying meta-data for the inode.  The
 * code below is otherwise similar to the iget() code in
 * include/linux/fs.h */
struct inode *__isofs_iget(struct super_block *sb,
			   unsigned long block,
			   unsigned long offset,
			   int relocated)
{
	unsigned long hashval;
	struct inode *inode;
	struct isofs_iget5_callback_data data;
	long ret;

	if (offset >= 1ul << sb->s_blocksize_bits)
		return ERR_PTR(-EINVAL);

	data.block = block;
	data.offset = offset;

	hashval = (block << sb->s_blocksize_bits) | offset;

	inode = iget5_locked(sb, hashval, &isofs_iget5_test,
				&isofs_iget5_set, &data);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		ret = isofs_read_inode(inode, relocated);
		if (ret < 0) {
			iget_failed(inode);
			inode = ERR_PTR(ret);
		} else {
			unlock_new_inode(inode);
		}
	}

	return inode;
}

static int isofs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, isofs_fill_super);
}

static void isofs_free_fc(struct fs_context *fc)
{
	struct isofs_options *opt = fc->fs_private;

	kfree(opt->iocharset);
	kfree(opt);
}

static const struct fs_context_operations isofs_context_ops = {
	.parse_param	= isofs_parse_param,
	.get_tree	= isofs_get_tree,
	.reconfigure	= isofs_reconfigure,
	.free		= isofs_free_fc,
};

static int isofs_init_fs_context(struct fs_context *fc)
{
	struct isofs_options *opt;

	opt = kzalloc(sizeof(*opt), GFP_KERNEL);
	if (!opt)
		return -ENOMEM;

	opt->map = 'n';
	opt->rock = 1;
	opt->joliet = 1;
	opt->cruft = 0;
	opt->hide = 0;
	opt->showassoc = 0;
	opt->check = 'u';		/* unset */
	opt->nocompress = 0;
	opt->blocksize = 1024;
	opt->fmode = opt->dmode = ISOFS_INVALID_MODE;
	opt->uid_set = 0;
	opt->gid_set = 0;
	opt->gid = GLOBAL_ROOT_GID;
	opt->uid = GLOBAL_ROOT_UID;
	opt->iocharset = NULL;
	opt->overriderockperm = 0;
	opt->session = -1;
	opt->sbsector = -1;

	fc->fs_private = opt;
	fc->ops = &isofs_context_ops;

	return 0;
}

static struct file_system_type iso9660_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "iso9660",
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
	.init_fs_context = isofs_init_fs_context,
	.parameters	= isofs_param_spec,
};
MODULE_ALIAS_FS("iso9660");
MODULE_ALIAS("iso9660");

static int __init init_iso9660_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out;
#ifdef CONFIG_ZISOFS
	err = zisofs_init();
	if (err)
		goto out1;
#endif
	err = register_filesystem(&iso9660_fs_type);
	if (err)
		goto out2;
	return 0;
out2:
#ifdef CONFIG_ZISOFS
	zisofs_cleanup();
out1:
#endif
	destroy_inodecache();
out:
	return err;
}

static void __exit exit_iso9660_fs(void)
{
        unregister_filesystem(&iso9660_fs_type);
#ifdef CONFIG_ZISOFS
	zisofs_cleanup();
#endif
	destroy_inodecache();
}

module_init(init_iso9660_fs)
module_exit(exit_iso9660_fs)
MODULE_DESCRIPTION("ISO 9660 CDROM file system support");
MODULE_LICENSE("GPL");
