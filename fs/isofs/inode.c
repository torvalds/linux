/*
 *  linux/fs/isofs/inode.c
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *      1992, 1993, 1994  Eric Youngdale Modified for ISO 9660 filesystem.
 *      1994  Eberhard Moenkeberg - multi session handling.
 *      1995  Mark Dobie - allow mounting of some weird VideoCDs and PhotoCDs.
 *	1997  Gordon Chaffee - Joliet CDs
 *	1998  Eric Lammerts - ISO 9660 Level 3
 *	2004  Paul Serice - Inode Support pushed out from 4GB to 128GB
 *	2004  Paul Serice - NFS Export Operations
 */

#include <linux/init.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/nls.h>
#include <linux/ctype.h>
#include <linux/smp_lock.h>
#include <linux/statfs.h>
#include <linux/cdrom.h>
#include <linux/parser.h>

#include "isofs.h"
#include "zisofs.h"

#define BEQUIET

static int isofs_hashi(struct dentry *parent, struct qstr *qstr);
static int isofs_hash(struct dentry *parent, struct qstr *qstr);
static int isofs_dentry_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int isofs_dentry_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b);

#ifdef CONFIG_JOLIET
static int isofs_hashi_ms(struct dentry *parent, struct qstr *qstr);
static int isofs_hash_ms(struct dentry *parent, struct qstr *qstr);
static int isofs_dentry_cmpi_ms(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int isofs_dentry_cmp_ms(struct dentry *dentry, struct qstr *a, struct qstr *b);
#endif

static void isofs_put_super(struct super_block *sb)
{
	struct isofs_sb_info *sbi = ISOFS_SB(sb);
#ifdef CONFIG_JOLIET
	if (sbi->s_nls_iocharset) {
		unload_nls(sbi->s_nls_iocharset);
		sbi->s_nls_iocharset = NULL;
	}
#endif

	kfree(sbi);
	sb->s_fs_info = NULL;
	return;
}

static void isofs_read_inode(struct inode *);
static int isofs_statfs (struct dentry *, struct kstatfs *);

static struct kmem_cache *isofs_inode_cachep;

static struct inode *isofs_alloc_inode(struct super_block *sb)
{
	struct iso_inode_info *ei;
	ei = kmem_cache_alloc(isofs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void isofs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(isofs_inode_cachep, ISOFS_I(inode));
}

static void init_once(void *foo, struct kmem_cache * cachep, unsigned long flags)
{
	struct iso_inode_info *ei = foo;

	inode_init_once(&ei->vfs_inode);
}
 
static int init_inodecache(void)
{
	isofs_inode_cachep = kmem_cache_create("isofs_inode_cache",
					     sizeof(struct iso_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (isofs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(isofs_inode_cachep);
}

static int isofs_remount(struct super_block *sb, int *flags, char *data)
{
	/* we probably want a lot more here */
	*flags |= MS_RDONLY;
	return 0;
}

static const struct super_operations isofs_sops = {
	.alloc_inode	= isofs_alloc_inode,
	.destroy_inode	= isofs_destroy_inode,
	.read_inode	= isofs_read_inode,
	.put_super	= isofs_put_super,
	.statfs		= isofs_statfs,
	.remount_fs	= isofs_remount,
};


static struct dentry_operations isofs_dentry_ops[] = {
	{
		.d_hash		= isofs_hash,
		.d_compare	= isofs_dentry_cmp,
	},
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

struct iso9660_options{
	char map;
	char rock;
	char joliet;
	char cruft;
	char hide;
	char showassoc;
	char nocompress;
	unsigned char check;
	unsigned int blocksize;
	mode_t mode;
	gid_t gid;
	uid_t uid;
	char *iocharset;
	unsigned char utf8;
        /* LVE */
        s32 session;
        s32 sbsector;
};

/*
 * Compute the hash for the isofs name corresponding to the dentry.
 */
static int
isofs_hash_common(struct dentry *dentry, struct qstr *qstr, int ms)
{
	const char *name;
	int len;

	len = qstr->len;
	name = qstr->name;
	if (ms) {
		while (len && name[len-1] == '.')
			len--;
	}

	qstr->hash = full_name_hash(name, len);

	return 0;
}

/*
 * Compute the hash for the isofs name corresponding to the dentry.
 */
static int
isofs_hashi_common(struct dentry *dentry, struct qstr *qstr, int ms)
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

	hash = init_name_hash();
	while (len--) {
		c = tolower(*name++);
		hash = partial_name_hash(tolower(c), hash);
	}
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Case insensitive compare of two isofs names.
 */
static int isofs_dentry_cmpi_common(struct dentry *dentry, struct qstr *a,
				struct qstr *b, int ms)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	if (ms) {
		while (alen && a->name[alen-1] == '.')
			alen--;
		while (blen && b->name[blen-1] == '.')
			blen--;
	}
	if (alen == blen) {
		if (strnicmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

/*
 * Case sensitive compare of two isofs names.
 */
static int isofs_dentry_cmp_common(struct dentry *dentry, struct qstr *a,
					struct qstr *b, int ms)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	if (ms) {
		while (alen && a->name[alen-1] == '.')
			alen--;
		while (blen && b->name[blen-1] == '.')
			blen--;
	}
	if (alen == blen) {
		if (strncmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

static int
isofs_hash(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hash_common(dentry, qstr, 0);
}

static int
isofs_hashi(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hashi_common(dentry, qstr, 0);
}

static int
isofs_dentry_cmp(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_dentry_cmp_common(dentry, a, b, 0);
}

static int
isofs_dentry_cmpi(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_dentry_cmpi_common(dentry, a, b, 0);
}

#ifdef CONFIG_JOLIET
static int
isofs_hash_ms(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hash_common(dentry, qstr, 1);
}

static int
isofs_hashi_ms(struct dentry *dentry, struct qstr *qstr)
{
	return isofs_hashi_common(dentry, qstr, 1);
}

static int
isofs_dentry_cmp_ms(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_dentry_cmp_common(dentry, a, b, 1);
}

static int
isofs_dentry_cmpi_ms(struct dentry *dentry,struct qstr *a,struct qstr *b)
{
	return isofs_dentry_cmpi_common(dentry, a, b, 1);
}
#endif

enum {
	Opt_block, Opt_check_r, Opt_check_s, Opt_cruft, Opt_gid, Opt_ignore,
	Opt_iocharset, Opt_map_a, Opt_map_n, Opt_map_o, Opt_mode, Opt_nojoliet,
	Opt_norock, Opt_sb, Opt_session, Opt_uid, Opt_unhide, Opt_utf8, Opt_err,
	Opt_nocompress, Opt_hide, Opt_showassoc,
};

static match_table_t tokens = {
	{Opt_norock, "norock"},
	{Opt_nojoliet, "nojoliet"},
	{Opt_unhide, "unhide"},
	{Opt_hide, "hide"},
	{Opt_showassoc, "showassoc"},
	{Opt_cruft, "cruft"},
	{Opt_utf8, "utf8"},
	{Opt_iocharset, "iocharset=%s"},
	{Opt_map_a, "map=acorn"},
	{Opt_map_a, "map=a"},
	{Opt_map_n, "map=normal"},
	{Opt_map_n, "map=n"},
	{Opt_map_o, "map=off"},
	{Opt_map_o, "map=o"},
	{Opt_session, "session=%u"},
	{Opt_sb, "sbsector=%u"},
	{Opt_check_r, "check=relaxed"},
	{Opt_check_r, "check=r"},
	{Opt_check_s, "check=strict"},
	{Opt_check_s, "check=s"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_mode, "mode=%u"},
	{Opt_block, "block=%u"},
	{Opt_ignore, "conv=binary"},
	{Opt_ignore, "conv=b"},
	{Opt_ignore, "conv=text"},
	{Opt_ignore, "conv=t"},
	{Opt_ignore, "conv=mtext"},
	{Opt_ignore, "conv=m"},
	{Opt_ignore, "conv=auto"},
	{Opt_ignore, "conv=a"},
	{Opt_nocompress, "nocompress"},
	{Opt_err, NULL}
};

static int parse_options(char *options, struct iso9660_options *popt)
{
	char *p;
	int option;

	popt->map = 'n';
	popt->rock = 'y';
	popt->joliet = 'y';
	popt->cruft = 'n';
	popt->hide = 'n';
	popt->showassoc = 'n';
	popt->check = 'u';		/* unset */
	popt->nocompress = 0;
	popt->blocksize = 1024;
	popt->mode = S_IRUGO | S_IXUGO; /* r-x for all.  The disc could
					   be shared with DOS machines so
					   virtually anything could be
					   a valid executable. */
	popt->gid = 0;
	popt->uid = 0;
	popt->iocharset = NULL;
	popt->utf8 = 0;
	popt->session=-1;
	popt->sbsector=-1;
	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		substring_t args[MAX_OPT_ARGS];
		unsigned n;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_norock:
			popt->rock = 'n';
			break;
		case Opt_nojoliet:
			popt->joliet = 'n';
			break;
		case Opt_hide:
			popt->hide = 'y';
			break;
		case Opt_unhide:
		case Opt_showassoc:
			popt->showassoc = 'y';
			break;
		case Opt_cruft:
			popt->cruft = 'y';
			break;
		case Opt_utf8:
			popt->utf8 = 1;
			break;
#ifdef CONFIG_JOLIET
		case Opt_iocharset:
			popt->iocharset = match_strdup(&args[0]);
			break;
#endif
		case Opt_map_a:
			popt->map = 'a';
			break;
		case Opt_map_o:
			popt->map = 'o';
			break;
		case Opt_map_n:
			popt->map = 'n';
			break;
		case Opt_session:
			if (match_int(&args[0], &option))
				return 0;
			n = option;
			if (n > 99)
				return 0;
			popt->session = n + 1;
			break;
		case Opt_sb:
			if (match_int(&args[0], &option))
				return 0;
			popt->sbsector = option;
			break;
		case Opt_check_r:
			popt->check = 'r';
			break;
		case Opt_check_s:
			popt->check = 's';
			break;
		case Opt_ignore:
			break;
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 0;
			popt->uid = option;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
			popt->gid = option;
			break;
		case Opt_mode:
			if (match_int(&args[0], &option))
				return 0;
			popt->mode = option;
			break;
		case Opt_block:
			if (match_int(&args[0], &option))
				return 0;
			n = option;
			if (n != 512 && n != 1024 && n != 2048)
				return 0;
			popt->blocksize = n;
			break;
		case Opt_nocompress:
			popt->nocompress = 1;
			break;
		default:
			return 0;
		}
	}
	return 1;
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
	struct cdrom_multisession ms_info;
	unsigned int vol_desc_start;
	struct block_device *bdev = sb->s_bdev;
	int i;

	vol_desc_start=0;
	ms_info.addr_format=CDROM_LBA;
	if(session >= 0 && session <= 99) {
		struct cdrom_tocentry Te;
		Te.cdte_track=session;
		Te.cdte_format=CDROM_LBA;
		i = ioctl_by_bdev(bdev, CDROMREADTOCENTRY, (unsigned long) &Te);
		if (!i) {
			printk(KERN_DEBUG "Session %d start %d type %d\n",
			       session, Te.cdte_addr.lba,
			       Te.cdte_ctrl&CDROM_DATA_TRACK);
			if ((Te.cdte_ctrl&CDROM_DATA_TRACK) == 4)
				return Te.cdte_addr.lba;
		}
			
		printk(KERN_ERR "Invalid session number or type of track\n");
	}
	i = ioctl_by_bdev(bdev, CDROMMULTISESSION, (unsigned long) &ms_info);
	if (session > 0)
		printk(KERN_ERR "Invalid session number\n");
#if 0
	printk("isofs.inode: CDROMMULTISESSION: rc=%d\n",i);
	if (i==0) {
		printk("isofs.inode: XA disk: %s\n",ms_info.xa_flag?"yes":"no");
		printk("isofs.inode: vol_desc_start = %d\n", ms_info.addr.lba);
	}
#endif
	if (i==0)
#if WE_OBEY_THE_WRITTEN_STANDARDS
        if (ms_info.xa_flag) /* necessary for a valid ms_info.addr */
#endif
		vol_desc_start=ms_info.addr.lba;
	return vol_desc_start;
}

/*
 * Initialize the superblock and read the root inode.
 *
 * Note: a check_disk_change() has been done immediately prior
 * to this call, so we don't need to check again.
 */
static int isofs_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head	      * bh = NULL, *pri_bh = NULL;
	struct hs_primary_descriptor  * h_pri = NULL;
	struct iso_primary_descriptor * pri = NULL;
	struct iso_supplementary_descriptor *sec = NULL;
	struct iso_directory_record   * rootp;
	int				joliet_level = 0;
	int				iso_blknum, block;
	int				orig_zonesize;
	int				table;
	unsigned int			vol_desc_start;
	unsigned long			first_data_zone;
	struct inode		      * inode;
	struct iso9660_options		opt;
	struct isofs_sb_info	      * sbi;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;

	if (!parse_options((char *)data, &opt))
		goto out_freesbi;

	/*
	 * First of all, get the hardware blocksize for this device.
	 * If we don't know what it is, or the hardware blocksize is
	 * larger than the blocksize the user specified, then use
	 * that value.
	 */
	/*
	 * What if bugger tells us to go beyond page size?
	 */
	opt.blocksize = sb_min_blocksize(s, opt.blocksize);

	sbi->s_high_sierra = 0; /* default is iso9660 */

	vol_desc_start = (opt.sbsector != -1) ?
		opt.sbsector : isofs_get_last_session(s,opt.session);

  	for (iso_blknum = vol_desc_start+16;
             iso_blknum < vol_desc_start+100; iso_blknum++)
	{
	    struct hs_volume_descriptor   * hdp;
	    struct iso_volume_descriptor  * vdp;

	    block = iso_blknum << (ISOFS_BLOCK_BITS - s->s_blocksize_bits);
	    if (!(bh = sb_bread(s, block)))
		goto out_no_read;

	    vdp = (struct iso_volume_descriptor *)bh->b_data;
	    hdp = (struct hs_volume_descriptor *)bh->b_data;
	    
	    /* Due to the overlapping physical location of the descriptors, 
	     * ISO CDs can match hdp->id==HS_STANDARD_ID as well. To ensure 
	     * proper identification in this case, we first check for ISO.
	     */
	    if (strncmp (vdp->id, ISO_STANDARD_ID, sizeof vdp->id) == 0) {
		if (isonum_711 (vdp->type) == ISO_VD_END)
		    break;
		if (isonum_711 (vdp->type) == ISO_VD_PRIMARY) {
		    if (pri == NULL) {
			pri = (struct iso_primary_descriptor *)vdp;
			/* Save the buffer in case we need it ... */
			pri_bh = bh;
			bh = NULL;
		    }
		}
#ifdef CONFIG_JOLIET
		else if (isonum_711 (vdp->type) == ISO_VD_SUPPLEMENTARY) {
		    sec = (struct iso_supplementary_descriptor *)vdp;
		    if (sec->escape[0] == 0x25 && sec->escape[1] == 0x2f) {
			if (opt.joliet == 'y') {
			    if (sec->escape[2] == 0x40) {
				joliet_level = 1;
			    } else if (sec->escape[2] == 0x43) {
				joliet_level = 2;
			    } else if (sec->escape[2] == 0x45) {
				joliet_level = 3;
			    }
			    printk(KERN_DEBUG"ISO 9660 Extensions: Microsoft Joliet Level %d\n",
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
		    if (isonum_711 (hdp->type) != ISO_VD_PRIMARY)
		        goto out_freebh;
		
		    sbi->s_high_sierra = 1;
		    opt.rock = 'n';
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

	if (joliet_level && (pri == NULL || opt.rock == 'n')) {
	    /* This is the case of Joliet with the norock mount flag.
	     * A disc with both Joliet and Rock Ridge is handled later
	     */
	    pri = (struct iso_primary_descriptor *) sec;
	}

	if(sbi->s_high_sierra){
	  rootp = (struct iso_directory_record *) h_pri->root_directory_record;
	  sbi->s_nzones = isonum_733 (h_pri->volume_space_size);
	  sbi->s_log_zone_size = isonum_723 (h_pri->logical_block_size);
	  sbi->s_max_size = isonum_733(h_pri->volume_space_size);
	} else {
	  if (!pri)
	    goto out_freebh;
	  rootp = (struct iso_directory_record *) pri->root_directory_record;
	  sbi->s_nzones = isonum_733 (pri->volume_space_size);
	  sbi->s_log_zone_size = isonum_723 (pri->logical_block_size);
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
	if(orig_zonesize < opt.blocksize)
		goto out_bad_size;

	/* RDE: convert log zone size to bit shift */
	switch (sbi->s_log_zone_size)
	  { case  512: sbi->s_log_zone_size =  9; break;
	    case 1024: sbi->s_log_zone_size = 10; break;
	    case 2048: sbi->s_log_zone_size = 11; break;

	    default:
		goto out_bad_zone_size;
	  }

	s->s_magic = ISOFS_SUPER_MAGIC;
	s->s_maxbytes = 0xffffffff; /* We can handle files up to 4 GB */

	/* The CDROM is read-only, has no nodes (devices) on it, and since
	   all of the files appear to be owned by root, we really do not want
	   to allow suid.  (suid or devices will not show up unless we have
	   Rock Ridge extensions) */

	s->s_flags |= MS_RDONLY /* | MS_NODEV | MS_NOSUID */;

	/* Set this for reference. Its not currently used except on write
	   which we don't have .. */
	   
	first_data_zone = isonum_733 (rootp->extent) +
			  isonum_711 (rootp->ext_attr_length);
	sbi->s_firstdatazone = first_data_zone;
#ifndef BEQUIET
	printk(KERN_DEBUG "Max size:%ld   Log zone size:%ld\n",
	       sbi->s_max_size,
	       1UL << sbi->s_log_zone_size);
	printk(KERN_DEBUG "First datazone:%ld\n", sbi->s_firstdatazone);
	if(sbi->s_high_sierra)
		printk(KERN_DEBUG "Disc in High Sierra format.\n");
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
		first_data_zone = isonum_733 (rootp->extent) +
			  	isonum_711 (rootp->ext_attr_length);
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
	if (joliet_level && opt.utf8 == 0) {
		char * p = opt.iocharset ? opt.iocharset : CONFIG_NLS_DEFAULT;
		sbi->s_nls_iocharset = load_nls(p);
		if (! sbi->s_nls_iocharset) {
			/* Fail only if explicit charset specified */
			if (opt.iocharset)
				goto out_freesbi;
			sbi->s_nls_iocharset = load_nls_default();
		}
	}
#endif
	s->s_op = &isofs_sops;
	s->s_export_op = &isofs_export_ops;
	sbi->s_mapping = opt.map;
	sbi->s_rock = (opt.rock == 'y' ? 2 : 0);
	sbi->s_rock_offset = -1; /* initial offset, will guess until SP is found*/
	sbi->s_cruft = opt.cruft;
	sbi->s_hide = opt.hide;
	sbi->s_showassoc = opt.showassoc;
	sbi->s_uid = opt.uid;
	sbi->s_gid = opt.gid;
	sbi->s_utf8 = opt.utf8;
	sbi->s_nocompress = opt.nocompress;
	/*
	 * It would be incredibly stupid to allow people to mark every file
	 * on the disk as suid, so we merely allow them to set the default
	 * permissions.
	 */
	sbi->s_mode = opt.mode & 0777;

	/*
	 * Read the root inode, which _may_ result in changing
	 * the s_rock flag. Once we have the final s_rock value,
	 * we then decide whether to use the Joliet descriptor.
	 */
	inode = isofs_iget(s, sbi->s_firstdatazone, 0);

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
		}
	}

	if (opt.check == 'u') {
		/* Only Joliet is case insensitive by default */
		if (joliet_level) opt.check = 'r';
		else opt.check = 's';
	}
	sbi->s_joliet_level = joliet_level;

	/* check the root inode */
	if (!inode)
		goto out_no_root;
	if (!inode->i_op)
		goto out_bad_root;
	/* get the root dentry */
	s->s_root = d_alloc_root(inode);
	if (!(s->s_root))
		goto out_no_root;

	table = 0;
	if (joliet_level) table += 2;
	if (opt.check == 'r') table++;
	s->s_root->d_op = &isofs_dentry_ops[table];

	kfree(opt.iocharset);

	return 0;

	/*
	 * Display error messages and free resources.
	 */
out_bad_root:
	printk(KERN_WARNING "isofs_fill_super: root inode not initialized\n");
	goto out_iput;
out_no_root:
	printk(KERN_WARNING "isofs_fill_super: get root inode failed\n");
out_iput:
	iput(inode);
#ifdef CONFIG_JOLIET
	if (sbi->s_nls_iocharset)
		unload_nls(sbi->s_nls_iocharset);
#endif
	goto out_freesbi;
out_no_read:
	printk(KERN_WARNING "isofs_fill_super: "
		"bread failed, dev=%s, iso_blknum=%d, block=%d\n",
		s->s_id, iso_blknum, block);
	goto out_freesbi;
out_bad_zone_size:
	printk(KERN_WARNING "Bad logical zone size %ld\n",
		sbi->s_log_zone_size);
	goto out_freebh;
out_bad_size:
	printk(KERN_WARNING "Logical zone size(%d) < hardware blocksize(%u)\n",
		orig_zonesize, opt.blocksize);
	goto out_freebh;
out_unknown_format:
	if (!silent)
		printk(KERN_WARNING "Unable to identify CD-ROM format.\n");

out_freebh:
	brelse(bh);
out_freesbi:
	kfree(opt.iocharset);
	kfree(sbi);
	s->s_fs_info = NULL;
	return -EINVAL;
}

static int isofs_statfs (struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;

	buf->f_type = ISOFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (ISOFS_SB(sb)->s_nzones
                  << (ISOFS_SB(sb)->s_log_zone_size - sb->s_blocksize_bits));
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = ISOFS_SB(sb)->s_ninodes;
	buf->f_ffree = 0;
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Get a set of blocks; filling in buffer_heads if already allocated
 * or getblk() if they are not.  Returns the number of blocks inserted
 * (0 == error.)
 */
int isofs_get_blocks(struct inode *inode, sector_t iblock_s,
		     struct buffer_head **bh, unsigned long nblocks)
{
	unsigned long b_off;
	unsigned offset, sect_size;
	unsigned int firstext;
	unsigned long nextblk, nextoff;
	long iblock = (long)iblock_s;
	int section, rv;
	struct iso_inode_info *ei = ISOFS_I(inode);

	lock_kernel();

	rv = 0;
	if (iblock < 0 || iblock != iblock_s) {
		printk("isofs_get_blocks: block number too large\n");
		goto abort;
	}

	b_off = iblock;
	
	offset    = 0;
	firstext  = ei->i_first_extent;
	sect_size = ei->i_section_size >> ISOFS_BUFFER_BITS(inode);
	nextblk   = ei->i_next_section_block;
	nextoff   = ei->i_next_section_offset;
	section   = 0;

	while ( nblocks ) {
		/* If we are *way* beyond the end of the file, print a message.
		 * Access beyond the end of the file up to the next page boundary
		 * is normal, however because of the way the page cache works.
		 * In this case, we just return 0 so that we can properly fill
		 * the page with useless information without generating any
		 * I/O errors.
		 */
		if (b_off > ((inode->i_size + PAGE_CACHE_SIZE - 1) >> ISOFS_BUFFER_BITS(inode))) {
			printk("isofs_get_blocks: block >= EOF (%ld, %ld)\n",
			       iblock, (unsigned long) inode->i_size);
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
			if (!ninode)
				goto abort;
			firstext  = ISOFS_I(ninode)->i_first_extent;
			sect_size = ISOFS_I(ninode)->i_section_size >> ISOFS_BUFFER_BITS(ninode);
			nextblk   = ISOFS_I(ninode)->i_next_section_block;
			nextoff   = ISOFS_I(ninode)->i_next_section_offset;
			iput(ninode);

			if (++section > 100) {
				printk("isofs_get_blocks: More than 100 file sections ?!?, aborting...\n");
				printk("isofs_get_blocks: block=%ld firstext=%u sect_size=%u "
				       "nextblk=%lu nextoff=%lu\n",
				       iblock, firstext, (unsigned) sect_size,
				       nextblk, nextoff);
				goto abort;
			}
		}
		
		if ( *bh ) {
			map_bh(*bh, inode->i_sb, firstext + b_off - offset);
		} else {
			*bh = sb_getblk(inode->i_sb, firstext+b_off-offset);
			if ( !*bh )
				goto abort;
		}
		bh++;	/* Next buffer head */
		b_off++;	/* Next buffer offset */
		nblocks--;
		rv++;
	}

abort:
	unlock_kernel();
	return rv;
}

/*
 * Used by the standard interfaces.
 */
static int isofs_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	if (create) {
		printk("isofs_get_block: Kernel tries to allocate a block\n");
		return -EROFS;
	}

	return isofs_get_blocks(inode, iblock, &bh_result, 1) ? 0 : -EIO;
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

static int isofs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,isofs_get_block);
}

static sector_t _isofs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,isofs_get_block);
}

static const struct address_space_operations isofs_aops = {
	.readpage = isofs_readpage,
	.sync_page = block_sync_page,
	.bmap = _isofs_bmap
};

static inline void test_and_set_uid(uid_t *p, uid_t value)
{
	if (value)
		*p = value;
}

static inline void test_and_set_gid(gid_t *p, gid_t value)
{
        if (value)
                *p = value;
}

static int isofs_read_level3_size(struct inode *inode)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	int high_sierra = ISOFS_SB(inode->i_sb)->s_high_sierra;
	struct buffer_head * bh = NULL;
	unsigned long block, offset, block_saved, offset_saved;
	int i = 0;
	int more_entries = 0;
	struct iso_directory_record * tmpde = NULL;
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
		struct iso_directory_record * de;
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
	if (bh)
		brelse(bh);
	return 0;

out_nomem:
	if (bh)
		brelse(bh);
	return -ENOMEM;

out_noread:
	printk(KERN_INFO "ISOFS: unable to read i-node block %lu\n", block);
	kfree(tmpde);
	return -EIO;

out_toomany:
	printk(KERN_INFO "isofs_read_level3_size: "
		"More than 100 file sections ?!?, aborting...\n"
	  	"isofs_read_level3_size: inode=%lu\n",
		inode->i_ino);
	goto out;
}

static void isofs_read_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct isofs_sb_info *sbi = ISOFS_SB(sb);
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	unsigned long block;
	int high_sierra = sbi->s_high_sierra;
	struct buffer_head * bh = NULL;
	struct iso_directory_record * de;
	struct iso_directory_record * tmpde = NULL;
	unsigned int de_len;
	unsigned long offset;
	struct iso_inode_info *ei = ISOFS_I(inode);

	block = ei->i_iget5_block;
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		goto out_badread;

	offset = ei->i_iget5_offset;

	de = (struct iso_directory_record *) (bh->b_data + offset);
	de_len = *(unsigned char *) de;

	if (offset + de_len > bufsize) {
		int frag1 = bufsize - offset;

		tmpde = kmalloc(de_len, GFP_KERNEL);
		if (tmpde == NULL) {
			printk(KERN_INFO "isofs_read_inode: out of memory\n");
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
		inode->i_mode = S_IRUGO | S_IXUGO | S_IFDIR;
		inode->i_nlink = 1; /* Set to 1.  We know there are 2, but
				       the find utility tries to optimize
				       if it is 2, and it screws up.  It is
				       easier to give 1 which tells find to
				       do it the hard way. */
	} else {
 		/* Everybody gets to read the file. */
		inode->i_mode = sbi->s_mode;
		inode->i_nlink = 1;
	        inode->i_mode |= S_IFREG;
	}
	inode->i_uid = sbi->s_uid;
	inode->i_gid = sbi->s_gid;
	inode->i_blocks = 0;

	ei->i_format_parm[0] = 0;
	ei->i_format_parm[1] = 0;
	ei->i_format_parm[2] = 0;

	ei->i_section_size = isonum_733 (de->size);
	if (de->flags[-high_sierra] & 0x80) {
		if(isofs_read_level3_size(inode)) goto fail;
	} else {
		ei->i_next_section_block = 0;
		ei->i_next_section_offset = 0;
		inode->i_size = isonum_733 (de->size);
	}

	/*
	 * Some dipshit decided to store some other bit of information
	 * in the high byte of the file length.  Truncate size in case
	 * this CDROM was mounted with the cruft option.
	 */

	if (sbi->s_cruft == 'y')
		inode->i_size &= 0x00ffffff;

	if (de->interleave[0]) {
		printk("Interleaved files not (yet) supported.\n");
		inode->i_size = 0;
	}

	/* I have no idea what file_unit_size is used for, so
	   we will flag it for now */
	if (de->file_unit_size[0] != 0) {
		printk("File unit size != 0 for ISO file (%ld).\n",
		       inode->i_ino);
	}

	/* I have no idea what other flag bits are used for, so
	   we will flag it for now */
#ifdef DEBUG
	if((de->flags[-high_sierra] & ~2)!= 0){
		printk("Unusual flag settings for ISO file (%ld %x).\n",
		       inode->i_ino, de->flags[-high_sierra]);
	}
#endif

	inode->i_mtime.tv_sec =
	inode->i_atime.tv_sec =
	inode->i_ctime.tv_sec = iso_date(de->date, high_sierra);
	inode->i_mtime.tv_nsec =
	inode->i_atime.tv_nsec =
	inode->i_ctime.tv_nsec = 0;

	ei->i_first_extent = (isonum_733 (de->extent) +
			      isonum_711 (de->ext_attr_length));

	/* Set the number of blocks for stat() - should be done before RR */
	inode->i_blocks  = (inode->i_size + 511) >> 9;

	/*
	 * Now test for possible Rock Ridge extensions which will override
	 * some of these numbers in the inode structure.
	 */

	if (!high_sierra) {
		parse_rock_ridge_inode(de, inode);
		/* if we want uid/gid set, override the rock ridge setting */
		test_and_set_uid(&inode->i_uid, sbi->s_uid);
		test_and_set_gid(&inode->i_gid, sbi->s_gid);
	}

	/* Install the inode operations vector */
	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &generic_ro_fops;
		switch ( ei->i_file_format ) {
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
		inode->i_data.a_ops = &isofs_symlink_aops;
	} else
		/* XXX - parse_rock_ridge_inode() had already set i_rdev. */
		init_special_inode(inode, inode->i_mode, inode->i_rdev);

out:
	kfree(tmpde);
	if (bh)
		brelse(bh);
	return;

out_badread:
	printk(KERN_WARNING "ISOFS: unable to read i-node block\n");
fail:
	make_bad_inode(inode);
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
struct inode *isofs_iget(struct super_block *sb,
			 unsigned long block,
			 unsigned long offset)
{
	unsigned long hashval;
	struct inode *inode;
	struct isofs_iget5_callback_data data;

	if (offset >= 1ul << sb->s_blocksize_bits)
		return NULL;

	data.block = block;
	data.offset = offset;

	hashval = (block << sb->s_blocksize_bits) | offset;

	inode = iget5_locked(sb, hashval, &isofs_iget5_test,
			     &isofs_iget5_set, &data);

	if (inode && (inode->i_state & I_NEW)) {
		sb->s_op->read_inode(inode);
		unlock_new_inode(inode);
	}

	return inode;
}

static int isofs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, isofs_fill_super,
			   mnt);
}

static struct file_system_type iso9660_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "iso9660",
	.get_sb		= isofs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

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
MODULE_LICENSE("GPL");
/* Actual filesystem name is iso9660, as requested in filesystems.c */
MODULE_ALIAS("iso9660");
