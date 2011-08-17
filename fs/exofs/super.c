/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/vfs.h>
#include <linux/random.h>
#include <linux/exportfs.h>
#include <linux/slab.h>

#include "exofs.h"

#define EXOFS_DBGMSG2(M...) do {} while (0)

/******************************************************************************
 * MOUNT OPTIONS
 *****************************************************************************/

/*
 * struct to hold what we get from mount options
 */
struct exofs_mountopt {
	bool is_osdname;
	const char *dev_name;
	uint64_t pid;
	int timeout;
};

/*
 * exofs-specific mount-time options.
 */
enum { Opt_name, Opt_pid, Opt_to, Opt_err };

/*
 * Our mount-time options.  These should ideally be 64-bit unsigned, but the
 * kernel's parsing functions do not currently support that.  32-bit should be
 * sufficient for most applications now.
 */
static match_table_t tokens = {
	{Opt_name, "osdname=%s"},
	{Opt_pid, "pid=%u"},
	{Opt_to, "to=%u"},
	{Opt_err, NULL}
};

/*
 * The main option parsing method.  Also makes sure that all of the mandatory
 * mount options were set.
 */
static int parse_options(char *options, struct exofs_mountopt *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	bool s_pid = false;

	EXOFS_DBGMSG("parse_options %s\n", options);
	/* defaults */
	memset(opts, 0, sizeof(*opts));
	opts->timeout = BLK_DEFAULT_SG_TIMEOUT;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		char str[32];

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_name:
			opts->dev_name = match_strdup(&args[0]);
			if (unlikely(!opts->dev_name)) {
				EXOFS_ERR("Error allocating dev_name");
				return -ENOMEM;
			}
			opts->is_osdname = true;
			break;
		case Opt_pid:
			if (0 == match_strlcpy(str, &args[0], sizeof(str)))
				return -EINVAL;
			opts->pid = simple_strtoull(str, NULL, 0);
			if (opts->pid < EXOFS_MIN_PID) {
				EXOFS_ERR("Partition ID must be >= %u",
					  EXOFS_MIN_PID);
				return -EINVAL;
			}
			s_pid = 1;
			break;
		case Opt_to:
			if (match_int(&args[0], &option))
				return -EINVAL;
			if (option <= 0) {
				EXOFS_ERR("Timout must be > 0");
				return -EINVAL;
			}
			opts->timeout = option * HZ;
			break;
		}
	}

	if (!s_pid) {
		EXOFS_ERR("Need to specify the following options:\n");
		EXOFS_ERR("    -o pid=pid_no_to_use\n");
		return -EINVAL;
	}

	return 0;
}

/******************************************************************************
 * INODE CACHE
 *****************************************************************************/

/*
 * Our inode cache.  Isn't it pretty?
 */
static struct kmem_cache *exofs_inode_cachep;

/*
 * Allocate an inode in the cache
 */
static struct inode *exofs_alloc_inode(struct super_block *sb)
{
	struct exofs_i_info *oi;

	oi = kmem_cache_alloc(exofs_inode_cachep, GFP_KERNEL);
	if (!oi)
		return NULL;

	oi->vfs_inode.i_version = 1;
	return &oi->vfs_inode;
}

static void exofs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	INIT_LIST_HEAD(&inode->i_dentry);
	kmem_cache_free(exofs_inode_cachep, exofs_i(inode));
}

/*
 * Remove an inode from the cache
 */
static void exofs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, exofs_i_callback);
}

/*
 * Initialize the inode
 */
static void exofs_init_once(void *foo)
{
	struct exofs_i_info *oi = foo;

	inode_init_once(&oi->vfs_inode);
}

/*
 * Create and initialize the inode cache
 */
static int init_inodecache(void)
{
	exofs_inode_cachep = kmem_cache_create("exofs_inode_cache",
				sizeof(struct exofs_i_info), 0,
				SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
				exofs_init_once);
	if (exofs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

/*
 * Destroy the inode cache
 */
static void destroy_inodecache(void)
{
	kmem_cache_destroy(exofs_inode_cachep);
}

/******************************************************************************
 * Some osd helpers
 *****************************************************************************/
void exofs_make_credential(u8 cred_a[OSD_CAP_LEN], const struct osd_obj_id *obj)
{
	osd_sec_init_nosec_doall_caps(cred_a, obj, false, true);
}

static int exofs_read_kern(struct osd_dev *od, u8 *cred, struct osd_obj_id *obj,
		    u64 offset, void *p, unsigned length)
{
	struct osd_request *or = osd_start_request(od, GFP_KERNEL);
/*	struct osd_sense_info osi = {.key = 0};*/
	int ret;

	if (unlikely(!or)) {
		EXOFS_DBGMSG("%s: osd_start_request failed.\n", __func__);
		return -ENOMEM;
	}
	ret = osd_req_read_kern(or, obj, offset, p, length);
	if (unlikely(ret)) {
		EXOFS_DBGMSG("%s: osd_req_read_kern failed.\n", __func__);
		goto out;
	}

	ret = osd_finalize_request(or, 0, cred, NULL);
	if (unlikely(ret)) {
		EXOFS_DBGMSG("Failed to osd_finalize_request() => %d\n", ret);
		goto out;
	}

	ret = osd_execute_request(or);
	if (unlikely(ret))
		EXOFS_DBGMSG("osd_execute_request() => %d\n", ret);
	/* osd_req_decode_sense(or, ret); */

out:
	osd_end_request(or);
	EXOFS_DBGMSG2("read_kern(0x%llx) offset=0x%llx "
		      "length=0x%llx dev=%p ret=>%d\n",
		      _LLU(obj->id), _LLU(offset), _LLU(length), od, ret);
	return ret;
}

static const struct osd_attr g_attr_sb_stats = ATTR_DEF(
	EXOFS_APAGE_SB_DATA,
	EXOFS_ATTR_SB_STATS,
	sizeof(struct exofs_sb_stats));

static int __sbi_read_stats(struct exofs_sb_info *sbi)
{
	struct osd_attr attrs[] = {
		[0] = g_attr_sb_stats,
	};
	struct ore_io_state *ios;
	int ret;

	ret = ore_get_io_state(&sbi->layout, &sbi->comps, &ios);
	if (unlikely(ret)) {
		EXOFS_ERR("%s: ore_get_io_state failed.\n", __func__);
		return ret;
	}

	ios->in_attr = attrs;
	ios->in_attr_len = ARRAY_SIZE(attrs);

	ret = ore_read(ios);
	if (unlikely(ret)) {
		EXOFS_ERR("Error reading super_block stats => %d\n", ret);
		goto out;
	}

	ret = extract_attr_from_ios(ios, &attrs[0]);
	if (ret) {
		EXOFS_ERR("%s: extract_attr of sb_stats failed\n", __func__);
		goto out;
	}
	if (attrs[0].len) {
		struct exofs_sb_stats *ess;

		if (unlikely(attrs[0].len != sizeof(*ess))) {
			EXOFS_ERR("%s: Wrong version of exofs_sb_stats "
				  "size(%d) != expected(%zd)\n",
				  __func__, attrs[0].len, sizeof(*ess));
			goto out;
		}

		ess = attrs[0].val_ptr;
		sbi->s_nextid = le64_to_cpu(ess->s_nextid);
		sbi->s_numfiles = le32_to_cpu(ess->s_numfiles);
	}

out:
	ore_put_io_state(ios);
	return ret;
}

static void stats_done(struct ore_io_state *ios, void *p)
{
	ore_put_io_state(ios);
	/* Good thanks nothing to do anymore */
}

/* Asynchronously write the stats attribute */
int exofs_sbi_write_stats(struct exofs_sb_info *sbi)
{
	struct osd_attr attrs[] = {
		[0] = g_attr_sb_stats,
	};
	struct ore_io_state *ios;
	int ret;

	ret = ore_get_io_state(&sbi->layout, &sbi->comps, &ios);
	if (unlikely(ret)) {
		EXOFS_ERR("%s: ore_get_io_state failed.\n", __func__);
		return ret;
	}

	sbi->s_ess.s_nextid   = cpu_to_le64(sbi->s_nextid);
	sbi->s_ess.s_numfiles = cpu_to_le64(sbi->s_numfiles);
	attrs[0].val_ptr = &sbi->s_ess;


	ios->done = stats_done;
	ios->private = sbi;
	ios->out_attr = attrs;
	ios->out_attr_len = ARRAY_SIZE(attrs);

	ret = ore_write(ios);
	if (unlikely(ret)) {
		EXOFS_ERR("%s: ore_write failed.\n", __func__);
		ore_put_io_state(ios);
	}

	return ret;
}

/******************************************************************************
 * SUPERBLOCK FUNCTIONS
 *****************************************************************************/
static const struct super_operations exofs_sops;
static const struct export_operations exofs_export_ops;

/*
 * Write the superblock to the OSD
 */
int exofs_sync_fs(struct super_block *sb, int wait)
{
	struct exofs_sb_info *sbi;
	struct exofs_fscb *fscb;
	struct ore_comp one_comp;
	struct ore_components comps;
	struct ore_io_state *ios;
	int ret = -ENOMEM;

	fscb = kmalloc(sizeof(*fscb), GFP_KERNEL);
	if (unlikely(!fscb))
		return -ENOMEM;

	sbi = sb->s_fs_info;

	/* NOTE: We no longer dirty the super_block anywhere in exofs. The
	 * reason we write the fscb here on unmount is so we can stay backwards
	 * compatible with fscb->s_version == 1. (What we are not compatible
	 * with is if a new version FS crashed and then we try to mount an old
	 * version). Otherwise the exofs_fscb is read-only from mkfs time. All
	 * the writeable info is set in exofs_sbi_write_stats() above.
	 */

	exofs_init_comps(&comps, &one_comp, sbi, EXOFS_SUPER_ID);

	ret = ore_get_io_state(&sbi->layout, &comps, &ios);
	if (unlikely(ret))
		goto out;

	lock_super(sb);

	ios->length = offsetof(struct exofs_fscb, s_dev_table_oid);
	memset(fscb, 0, ios->length);
	fscb->s_nextid = cpu_to_le64(sbi->s_nextid);
	fscb->s_numfiles = cpu_to_le32(sbi->s_numfiles);
	fscb->s_magic = cpu_to_le16(sb->s_magic);
	fscb->s_newfs = 0;
	fscb->s_version = EXOFS_FSCB_VER;

	ios->offset = 0;
	ios->kern_buff = fscb;

	ret = ore_write(ios);
	if (unlikely(ret))
		EXOFS_ERR("%s: ore_write failed.\n", __func__);
	else
		sb->s_dirt = 0;


	unlock_super(sb);
out:
	EXOFS_DBGMSG("s_nextid=0x%llx ret=%d\n", _LLU(sbi->s_nextid), ret);
	ore_put_io_state(ios);
	kfree(fscb);
	return ret;
}

static void exofs_write_super(struct super_block *sb)
{
	if (!(sb->s_flags & MS_RDONLY))
		exofs_sync_fs(sb, 1);
	else
		sb->s_dirt = 0;
}

static void _exofs_print_device(const char *msg, const char *dev_path,
				struct osd_dev *od, u64 pid)
{
	const struct osd_dev_info *odi = osduld_device_info(od);

	printk(KERN_NOTICE "exofs: %s %s osd_name-%s pid-0x%llx\n",
		msg, dev_path ?: "", odi->osdname, _LLU(pid));
}

void exofs_free_sbi(struct exofs_sb_info *sbi)
{
	while (sbi->comps.numdevs) {
		int i = --sbi->comps.numdevs;
		struct osd_dev *od = sbi->comps.ods[i];

		if (od) {
			sbi->comps.ods[i] = NULL;
			osduld_put_device(od);
		}
	}
	if (sbi->comps.ods != sbi->_min_one_dev)
		kfree(sbi->comps.ods);
	kfree(sbi);
}

/*
 * This function is called when the vfs is freeing the superblock.  We just
 * need to free our own part.
 */
static void exofs_put_super(struct super_block *sb)
{
	int num_pend;
	struct exofs_sb_info *sbi = sb->s_fs_info;

	/* make sure there are no pending commands */
	for (num_pend = atomic_read(&sbi->s_curr_pending); num_pend > 0;
	     num_pend = atomic_read(&sbi->s_curr_pending)) {
		wait_queue_head_t wq;

		printk(KERN_NOTICE "%s: !!Pending operations in flight. "
		       "This is a BUG. please report to osd-dev@open-osd.org\n",
		       __func__);
		init_waitqueue_head(&wq);
		wait_event_timeout(wq,
				  (atomic_read(&sbi->s_curr_pending) == 0),
				  msecs_to_jiffies(100));
	}

	_exofs_print_device("Unmounting", NULL, sbi->comps.ods[0],
			    sbi->one_comp.obj.partition);

	bdi_destroy(&sbi->bdi);
	exofs_free_sbi(sbi);
	sb->s_fs_info = NULL;
}

static int _read_and_match_data_map(struct exofs_sb_info *sbi, unsigned numdevs,
				    struct exofs_device_table *dt)
{
	u64 stripe_length;

	sbi->data_map.odm_num_comps   =
				le32_to_cpu(dt->dt_data_map.cb_num_comps);
	sbi->data_map.odm_stripe_unit =
				le64_to_cpu(dt->dt_data_map.cb_stripe_unit);
	sbi->data_map.odm_group_width =
				le32_to_cpu(dt->dt_data_map.cb_group_width);
	sbi->data_map.odm_group_depth =
				le32_to_cpu(dt->dt_data_map.cb_group_depth);
	sbi->data_map.odm_mirror_cnt  =
				le32_to_cpu(dt->dt_data_map.cb_mirror_cnt);
	sbi->data_map.odm_raid_algorithm  =
				le32_to_cpu(dt->dt_data_map.cb_raid_algorithm);

/* FIXME: Only raid0 for now. if not so, do not mount */
	if (sbi->data_map.odm_num_comps != numdevs) {
		EXOFS_ERR("odm_num_comps(%u) != numdevs(%u)\n",
			  sbi->data_map.odm_num_comps, numdevs);
		return -EINVAL;
	}
	if (sbi->data_map.odm_raid_algorithm != PNFS_OSD_RAID_0) {
		EXOFS_ERR("Only RAID_0 for now\n");
		return -EINVAL;
	}
	if (0 != (numdevs % (sbi->data_map.odm_mirror_cnt + 1))) {
		EXOFS_ERR("Data Map wrong, numdevs=%d mirrors=%d\n",
			  numdevs, sbi->data_map.odm_mirror_cnt);
		return -EINVAL;
	}

	if (0 != (sbi->data_map.odm_stripe_unit & ~PAGE_MASK)) {
		EXOFS_ERR("Stripe Unit(0x%llx)"
			  " must be Multples of PAGE_SIZE(0x%lx)\n",
			  _LLU(sbi->data_map.odm_stripe_unit), PAGE_SIZE);
		return -EINVAL;
	}

	sbi->layout.stripe_unit = sbi->data_map.odm_stripe_unit;
	sbi->layout.mirrors_p1 = sbi->data_map.odm_mirror_cnt + 1;

	if (sbi->data_map.odm_group_width) {
		sbi->layout.group_width = sbi->data_map.odm_group_width;
		sbi->layout.group_depth = sbi->data_map.odm_group_depth;
		if (!sbi->layout.group_depth) {
			EXOFS_ERR("group_depth == 0 && group_width != 0\n");
			return -EINVAL;
		}
		sbi->layout.group_count = sbi->data_map.odm_num_comps /
						sbi->layout.mirrors_p1 /
						sbi->data_map.odm_group_width;
	} else {
		if (sbi->data_map.odm_group_depth) {
			printk(KERN_NOTICE "Warning: group_depth ignored "
				"group_width == 0 && group_depth == %d\n",
				sbi->data_map.odm_group_depth);
			sbi->data_map.odm_group_depth = 0;
		}
		sbi->layout.group_width = sbi->data_map.odm_num_comps /
							sbi->layout.mirrors_p1;
		sbi->layout.group_depth = -1;
		sbi->layout.group_count = 1;
	}

	stripe_length = (u64)sbi->layout.group_width * sbi->layout.stripe_unit;
	if (stripe_length >= (1ULL << 32)) {
		EXOFS_ERR("Total Stripe length(0x%llx)"
			  " >= 32bit is not supported\n", _LLU(stripe_length));
		return -EINVAL;
	}

	EXOFS_DBGMSG("exofs: layout: "
		"num_comps=%u stripe_unit=0x%x group_width=%u "
		"group_depth=0x%llx mirrors_p1=%u raid_algorithm=%u\n",
		numdevs,
		sbi->layout.stripe_unit,
		sbi->layout.group_width,
		_LLU(sbi->layout.group_depth),
		sbi->layout.mirrors_p1,
		sbi->data_map.odm_raid_algorithm);
	return 0;
}

static unsigned __ra_pages(struct ore_layout *layout)
{
	const unsigned _MIN_RA = 32; /* min 128K read-ahead */
	unsigned ra_pages = layout->group_width * layout->stripe_unit /
				PAGE_SIZE;
	unsigned max_io_pages = exofs_max_io_pages(layout, ~0);

	ra_pages *= 2; /* two stripes */
	if (ra_pages < _MIN_RA)
		ra_pages = roundup(_MIN_RA, ra_pages / 2);

	if (ra_pages > max_io_pages)
		ra_pages = max_io_pages;

	return ra_pages;
}

/* @odi is valid only as long as @fscb_dev is valid */
static int exofs_devs_2_odi(struct exofs_dt_device_info *dt_dev,
			     struct osd_dev_info *odi)
{
	odi->systemid_len = le32_to_cpu(dt_dev->systemid_len);
	memcpy(odi->systemid, dt_dev->systemid, odi->systemid_len);

	odi->osdname_len = le32_to_cpu(dt_dev->osdname_len);
	odi->osdname = dt_dev->osdname;

	/* FIXME support long names. Will need a _put function */
	if (dt_dev->long_name_offset)
		return -EINVAL;

	/* Make sure osdname is printable!
	 * mkexofs should give us space for a null-terminator else the
	 * device-table is invalid.
	 */
	if (unlikely(odi->osdname_len >= sizeof(dt_dev->osdname)))
		odi->osdname_len = sizeof(dt_dev->osdname) - 1;
	dt_dev->osdname[odi->osdname_len] = 0;

	/* If it's all zeros something is bad we read past end-of-obj */
	return !(odi->systemid_len || odi->osdname_len);
}

static int exofs_read_lookup_dev_table(struct exofs_sb_info *sbi,
				       struct osd_dev *fscb_od,
				       unsigned table_count)
{
	struct ore_comp comp;
	struct exofs_device_table *dt;
	unsigned table_bytes = table_count * sizeof(dt->dt_dev_table[0]) +
					     sizeof(*dt);
	unsigned numdevs, i;
	int ret;

	dt = kmalloc(table_bytes, GFP_KERNEL);
	if (unlikely(!dt)) {
		EXOFS_ERR("ERROR: allocating %x bytes for device table\n",
			  table_bytes);
		return -ENOMEM;
	}

	sbi->comps.numdevs = 0;

	comp.obj.partition = sbi->one_comp.obj.partition;
	comp.obj.id = EXOFS_DEVTABLE_ID;
	exofs_make_credential(comp.cred, &comp.obj);

	ret = exofs_read_kern(fscb_od, comp.cred, &comp.obj, 0, dt,
			      table_bytes);
	if (unlikely(ret)) {
		EXOFS_ERR("ERROR: reading device table\n");
		goto out;
	}

	numdevs = le64_to_cpu(dt->dt_num_devices);
	if (unlikely(!numdevs)) {
		ret = -EINVAL;
		goto out;
	}
	WARN_ON(table_count != numdevs);

	ret = _read_and_match_data_map(sbi, numdevs, dt);
	if (unlikely(ret))
		goto out;

	if (likely(numdevs > 1)) {
		unsigned size = numdevs * sizeof(sbi->comps.ods[0]);

		/* Twice bigger table: See exofs_init_comps() and below
		 * comment
		 */
		sbi->comps.ods = kzalloc(size + size - 1, GFP_KERNEL);
		if (unlikely(!sbi->comps.ods)) {
			EXOFS_ERR("ERROR: faild allocating Device array[%d]\n",
				  numdevs);
			ret = -ENOMEM;
			goto out;
		}
	}

	for (i = 0; i < numdevs; i++) {
		struct exofs_fscb fscb;
		struct osd_dev_info odi;
		struct osd_dev *od;

		if (exofs_devs_2_odi(&dt->dt_dev_table[i], &odi)) {
			EXOFS_ERR("ERROR: Read all-zeros device entry\n");
			ret = -EINVAL;
			goto out;
		}

		printk(KERN_NOTICE "Add device[%d]: osd_name-%s\n",
		       i, odi.osdname);

		/* On all devices the device table is identical. The user can
		 * specify any one of the participating devices on the command
		 * line. We always keep them in device-table order.
		 */
		if (fscb_od && osduld_device_same(fscb_od, &odi)) {
			sbi->comps.ods[i] = fscb_od;
			++sbi->comps.numdevs;
			fscb_od = NULL;
			continue;
		}

		od = osduld_info_lookup(&odi);
		if (IS_ERR(od)) {
			ret = PTR_ERR(od);
			EXOFS_ERR("ERROR: device requested is not found "
				  "osd_name-%s =>%d\n", odi.osdname, ret);
			goto out;
		}

		sbi->comps.ods[i] = od;
		++sbi->comps.numdevs;

		/* Read the fscb of the other devices to make sure the FS
		 * partition is there.
		 */
		ret = exofs_read_kern(od, comp.cred, &comp.obj, 0, &fscb,
				      sizeof(fscb));
		if (unlikely(ret)) {
			EXOFS_ERR("ERROR: Malformed participating device "
				  "error reading fscb osd_name-%s\n",
				  odi.osdname);
			goto out;
		}

		/* TODO: verify other information is correct and FS-uuid
		 *	 matches. Benny what did you say about device table
		 *	 generation and old devices?
		 */
	}

out:
	kfree(dt);
	if (likely(!ret)) {
		unsigned numdevs = sbi->comps.numdevs;

		if (unlikely(fscb_od)) {
			EXOFS_ERR("ERROR: Bad device-table container device not present\n");
			osduld_put_device(fscb_od);
			return -EINVAL;
		}
		/* exofs round-robins the device table view according to inode
		 * number. We hold a: twice bigger table hence inodes can point
		 * to any device and have a sequential view of the table
		 * starting at this device. See exofs_init_comps()
		 */
		for (i = 0; i < numdevs - 1; ++i)
			sbi->comps.ods[i + numdevs] = sbi->comps.ods[i];
	}
	return ret;
}

/*
 * Read the superblock from the OSD and fill in the fields
 */
static int exofs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct exofs_mountopt *opts = data;
	struct exofs_sb_info *sbi;	/*extended info                  */
	struct osd_dev *od;		/* Master device                 */
	struct exofs_fscb fscb;		/*on-disk superblock info        */
	struct ore_comp comp;
	unsigned table_count;
	int ret;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	/* use mount options to fill superblock */
	if (opts->is_osdname) {
		struct osd_dev_info odi = {.systemid_len = 0};

		odi.osdname_len = strlen(opts->dev_name);
		odi.osdname = (u8 *)opts->dev_name;
		od = osduld_info_lookup(&odi);
		kfree(opts->dev_name);
		opts->dev_name = NULL;
	} else {
		od = osduld_path_lookup(opts->dev_name);
	}
	if (IS_ERR(od)) {
		ret = -EINVAL;
		goto free_sbi;
	}

	/* Default layout in case we do not have a device-table */
	sbi->layout.stripe_unit = PAGE_SIZE;
	sbi->layout.mirrors_p1 = 1;
	sbi->layout.group_width = 1;
	sbi->layout.group_depth = -1;
	sbi->layout.group_count = 1;
	sbi->s_timeout = opts->timeout;

	sbi->one_comp.obj.partition = opts->pid;
	sbi->one_comp.obj.id = 0;
	exofs_make_credential(sbi->one_comp.cred, &sbi->one_comp.obj);
	sbi->comps.numdevs = 1;
	sbi->comps.single_comp = EC_SINGLE_COMP;
	sbi->comps.comps = &sbi->one_comp;
	sbi->comps.ods = sbi->_min_one_dev;

	/* fill in some other data by hand */
	memset(sb->s_id, 0, sizeof(sb->s_id));
	strcpy(sb->s_id, "exofs");
	sb->s_blocksize = EXOFS_BLKSIZE;
	sb->s_blocksize_bits = EXOFS_BLKSHIFT;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	atomic_set(&sbi->s_curr_pending, 0);
	sb->s_bdev = NULL;
	sb->s_dev = 0;

	comp.obj.partition = sbi->one_comp.obj.partition;
	comp.obj.id = EXOFS_SUPER_ID;
	exofs_make_credential(comp.cred, &comp.obj);

	ret = exofs_read_kern(od, comp.cred, &comp.obj, 0, &fscb, sizeof(fscb));
	if (unlikely(ret))
		goto free_sbi;

	sb->s_magic = le16_to_cpu(fscb.s_magic);
	/* NOTE: we read below to be backward compatible with old versions */
	sbi->s_nextid = le64_to_cpu(fscb.s_nextid);
	sbi->s_numfiles = le32_to_cpu(fscb.s_numfiles);

	/* make sure what we read from the object store is correct */
	if (sb->s_magic != EXOFS_SUPER_MAGIC) {
		if (!silent)
			EXOFS_ERR("ERROR: Bad magic value\n");
		ret = -EINVAL;
		goto free_sbi;
	}
	if (le32_to_cpu(fscb.s_version) > EXOFS_FSCB_VER) {
		EXOFS_ERR("ERROR: Bad FSCB version expected-%d got-%d\n",
			  EXOFS_FSCB_VER, le32_to_cpu(fscb.s_version));
		ret = -EINVAL;
		goto free_sbi;
	}

	/* start generation numbers from a random point */
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);

	table_count = le64_to_cpu(fscb.s_dev_table_count);
	if (table_count) {
		ret = exofs_read_lookup_dev_table(sbi, od, table_count);
		if (unlikely(ret))
			goto free_sbi;
	} else {
		sbi->comps.ods[0] = od;
	}

	__sbi_read_stats(sbi);

	/* set up operation vectors */
	sbi->bdi.ra_pages = __ra_pages(&sbi->layout);
	sb->s_bdi = &sbi->bdi;
	sb->s_fs_info = sbi;
	sb->s_op = &exofs_sops;
	sb->s_export_op = &exofs_export_ops;
	root = exofs_iget(sb, EXOFS_ROOT_ID - EXOFS_OBJ_OFF);
	if (IS_ERR(root)) {
		EXOFS_ERR("ERROR: exofs_iget failed\n");
		ret = PTR_ERR(root);
		goto free_sbi;
	}
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		iput(root);
		EXOFS_ERR("ERROR: get root inode failed\n");
		ret = -ENOMEM;
		goto free_sbi;
	}

	if (!S_ISDIR(root->i_mode)) {
		dput(sb->s_root);
		sb->s_root = NULL;
		EXOFS_ERR("ERROR: corrupt root inode (mode = %hd)\n",
		       root->i_mode);
		ret = -EINVAL;
		goto free_sbi;
	}

	ret = bdi_setup_and_register(&sbi->bdi, "exofs", BDI_CAP_MAP_COPY);
	if (ret) {
		EXOFS_DBGMSG("Failed to bdi_setup_and_register\n");
		goto free_sbi;
	}

	_exofs_print_device("Mounting", opts->dev_name, sbi->comps.ods[0],
			    sbi->one_comp.obj.partition);
	return 0;

free_sbi:
	EXOFS_ERR("Unable to mount exofs on %s pid=0x%llx err=%d\n",
		  opts->dev_name, sbi->one_comp.obj.partition, ret);
	exofs_free_sbi(sbi);
	return ret;
}

/*
 * Set up the superblock (calls exofs_fill_super eventually)
 */
static struct dentry *exofs_mount(struct file_system_type *type,
			  int flags, const char *dev_name,
			  void *data)
{
	struct exofs_mountopt opts;
	int ret;

	ret = parse_options(data, &opts);
	if (ret)
		return ERR_PTR(ret);

	if (!opts.dev_name)
		opts.dev_name = dev_name;
	return mount_nodev(type, flags, &opts, exofs_fill_super);
}

/*
 * Return information about the file system state in the buffer.  This is used
 * by the 'df' command, for example.
 */
static int exofs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct ore_io_state *ios;
	struct osd_attr attrs[] = {
		ATTR_DEF(OSD_APAGE_PARTITION_QUOTAS,
			OSD_ATTR_PQ_CAPACITY_QUOTA, sizeof(__be64)),
		ATTR_DEF(OSD_APAGE_PARTITION_INFORMATION,
			OSD_ATTR_PI_USED_CAPACITY, sizeof(__be64)),
	};
	uint64_t capacity = ULLONG_MAX;
	uint64_t used = ULLONG_MAX;
	int ret;

	ret = ore_get_io_state(&sbi->layout, &sbi->comps, &ios);
	if (ret) {
		EXOFS_DBGMSG("ore_get_io_state failed.\n");
		return ret;
	}

	ios->in_attr = attrs;
	ios->in_attr_len = ARRAY_SIZE(attrs);

	ret = ore_read(ios);
	if (unlikely(ret))
		goto out;

	ret = extract_attr_from_ios(ios, &attrs[0]);
	if (likely(!ret)) {
		capacity = get_unaligned_be64(attrs[0].val_ptr);
		if (unlikely(!capacity))
			capacity = ULLONG_MAX;
	} else
		EXOFS_DBGMSG("exofs_statfs: get capacity failed.\n");

	ret = extract_attr_from_ios(ios, &attrs[1]);
	if (likely(!ret))
		used = get_unaligned_be64(attrs[1].val_ptr);
	else
		EXOFS_DBGMSG("exofs_statfs: get used-space failed.\n");

	/* fill in the stats buffer */
	buf->f_type = EXOFS_SUPER_MAGIC;
	buf->f_bsize = EXOFS_BLKSIZE;
	buf->f_blocks = capacity >> 9;
	buf->f_bfree = (capacity - used) >> 9;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = sbi->s_numfiles;
	buf->f_ffree = EXOFS_MAX_ID - sbi->s_numfiles;
	buf->f_namelen = EXOFS_NAME_LEN;

out:
	ore_put_io_state(ios);
	return ret;
}

static const struct super_operations exofs_sops = {
	.alloc_inode    = exofs_alloc_inode,
	.destroy_inode  = exofs_destroy_inode,
	.write_inode    = exofs_write_inode,
	.evict_inode    = exofs_evict_inode,
	.put_super      = exofs_put_super,
	.write_super    = exofs_write_super,
	.sync_fs	= exofs_sync_fs,
	.statfs         = exofs_statfs,
};

/******************************************************************************
 * EXPORT OPERATIONS
 *****************************************************************************/

struct dentry *exofs_get_parent(struct dentry *child)
{
	unsigned long ino = exofs_parent_ino(child);

	if (!ino)
		return ERR_PTR(-ESTALE);

	return d_obtain_alias(exofs_iget(child->d_inode->i_sb, ino));
}

static struct inode *exofs_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct inode *inode;

	inode = exofs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *exofs_fh_to_dentry(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    exofs_nfs_get_inode);
}

static struct dentry *exofs_fh_to_parent(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    exofs_nfs_get_inode);
}

static const struct export_operations exofs_export_ops = {
	.fh_to_dentry = exofs_fh_to_dentry,
	.fh_to_parent = exofs_fh_to_parent,
	.get_parent = exofs_get_parent,
};

/******************************************************************************
 * INSMOD/RMMOD
 *****************************************************************************/

/*
 * struct that describes this file system
 */
static struct file_system_type exofs_type = {
	.owner          = THIS_MODULE,
	.name           = "exofs",
	.mount          = exofs_mount,
	.kill_sb        = generic_shutdown_super,
};

static int __init init_exofs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		goto out;

	err = register_filesystem(&exofs_type);
	if (err)
		goto out_d;

	return 0;
out_d:
	destroy_inodecache();
out:
	return err;
}

static void __exit exit_exofs(void)
{
	unregister_filesystem(&exofs_type);
	destroy_inodecache();
}

MODULE_AUTHOR("Avishay Traeger <avishay@gmail.com>");
MODULE_DESCRIPTION("exofs");
MODULE_LICENSE("GPL");

module_init(init_exofs)
module_exit(exit_exofs)
