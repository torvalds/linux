/*
 *	vfsv0 quota IO operations on file
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/dqblk_v2.h>
#include <linux/quotaio_v2.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/byteorder.h>

MODULE_AUTHOR("Jan Kara");
MODULE_DESCRIPTION("Quota format v2 support");
MODULE_LICENSE("GPL");

#define __QUOTA_V2_PARANOIA

typedef char *dqbuf_t;

#define GETIDINDEX(id, depth) (((id) >> ((V2_DQTREEDEPTH-(depth)-1)*8)) & 0xff)
#define GETENTRIES(buf) ((struct v2_disk_dqblk *)(((char *)buf)+sizeof(struct v2_disk_dqdbheader)))

/* Check whether given file is really vfsv0 quotafile */
static int v2_check_quota_file(struct super_block *sb, int type)
{
	struct v2_disk_dqheader dqhead;
	ssize_t size;
	static const uint quota_magics[] = V2_INITQMAGICS;
	static const uint quota_versions[] = V2_INITQVERSIONS;
 
	size = sb->s_op->quota_read(sb, type, (char *)&dqhead, sizeof(struct v2_disk_dqheader), 0);
	if (size != sizeof(struct v2_disk_dqheader)) {
		printk("quota_v2: failed read expected=%zd got=%zd\n",
			sizeof(struct v2_disk_dqheader), size);
		return 0;
	}
	if (le32_to_cpu(dqhead.dqh_magic) != quota_magics[type] ||
	    le32_to_cpu(dqhead.dqh_version) != quota_versions[type])
		return 0;
	return 1;
}

/* Read information header from quota file */
static int v2_read_file_info(struct super_block *sb, int type)
{
	struct v2_disk_dqinfo dinfo;
	struct mem_dqinfo *info = sb_dqopt(sb)->info+type;
	ssize_t size;

	size = sb->s_op->quota_read(sb, type, (char *)&dinfo,
	       sizeof(struct v2_disk_dqinfo), V2_DQINFOOFF);
	if (size != sizeof(struct v2_disk_dqinfo)) {
		printk(KERN_WARNING "Can't read info structure on device %s.\n",
			sb->s_id);
		return -1;
	}
	/* limits are stored as unsigned 32-bit data */
	info->dqi_maxblimit = 0xffffffff;
	info->dqi_maxilimit = 0xffffffff;
	info->dqi_bgrace = le32_to_cpu(dinfo.dqi_bgrace);
	info->dqi_igrace = le32_to_cpu(dinfo.dqi_igrace);
	info->dqi_flags = le32_to_cpu(dinfo.dqi_flags);
	info->u.v2_i.dqi_blocks = le32_to_cpu(dinfo.dqi_blocks);
	info->u.v2_i.dqi_free_blk = le32_to_cpu(dinfo.dqi_free_blk);
	info->u.v2_i.dqi_free_entry = le32_to_cpu(dinfo.dqi_free_entry);
	return 0;
}

/* Write information header to quota file */
static int v2_write_file_info(struct super_block *sb, int type)
{
	struct v2_disk_dqinfo dinfo;
	struct mem_dqinfo *info = sb_dqopt(sb)->info+type;
	ssize_t size;

	spin_lock(&dq_data_lock);
	info->dqi_flags &= ~DQF_INFO_DIRTY;
	dinfo.dqi_bgrace = cpu_to_le32(info->dqi_bgrace);
	dinfo.dqi_igrace = cpu_to_le32(info->dqi_igrace);
	dinfo.dqi_flags = cpu_to_le32(info->dqi_flags & DQF_MASK);
	spin_unlock(&dq_data_lock);
	dinfo.dqi_blocks = cpu_to_le32(info->u.v2_i.dqi_blocks);
	dinfo.dqi_free_blk = cpu_to_le32(info->u.v2_i.dqi_free_blk);
	dinfo.dqi_free_entry = cpu_to_le32(info->u.v2_i.dqi_free_entry);
	size = sb->s_op->quota_write(sb, type, (char *)&dinfo,
	       sizeof(struct v2_disk_dqinfo), V2_DQINFOOFF);
	if (size != sizeof(struct v2_disk_dqinfo)) {
		printk(KERN_WARNING "Can't write info structure on device %s.\n",
			sb->s_id);
		return -1;
	}
	return 0;
}

static void disk2memdqb(struct mem_dqblk *m, struct v2_disk_dqblk *d)
{
	m->dqb_ihardlimit = le32_to_cpu(d->dqb_ihardlimit);
	m->dqb_isoftlimit = le32_to_cpu(d->dqb_isoftlimit);
	m->dqb_curinodes = le32_to_cpu(d->dqb_curinodes);
	m->dqb_itime = le64_to_cpu(d->dqb_itime);
	m->dqb_bhardlimit = le32_to_cpu(d->dqb_bhardlimit);
	m->dqb_bsoftlimit = le32_to_cpu(d->dqb_bsoftlimit);
	m->dqb_curspace = le64_to_cpu(d->dqb_curspace);
	m->dqb_btime = le64_to_cpu(d->dqb_btime);
}

static void mem2diskdqb(struct v2_disk_dqblk *d, struct mem_dqblk *m, qid_t id)
{
	d->dqb_ihardlimit = cpu_to_le32(m->dqb_ihardlimit);
	d->dqb_isoftlimit = cpu_to_le32(m->dqb_isoftlimit);
	d->dqb_curinodes = cpu_to_le32(m->dqb_curinodes);
	d->dqb_itime = cpu_to_le64(m->dqb_itime);
	d->dqb_bhardlimit = cpu_to_le32(m->dqb_bhardlimit);
	d->dqb_bsoftlimit = cpu_to_le32(m->dqb_bsoftlimit);
	d->dqb_curspace = cpu_to_le64(m->dqb_curspace);
	d->dqb_btime = cpu_to_le64(m->dqb_btime);
	d->dqb_id = cpu_to_le32(id);
}

static dqbuf_t getdqbuf(void)
{
	dqbuf_t buf = kmalloc(V2_DQBLKSIZE, GFP_NOFS);
	if (!buf)
		printk(KERN_WARNING "VFS: Not enough memory for quota buffers.\n");
	return buf;
}

static inline void freedqbuf(dqbuf_t buf)
{
	kfree(buf);
}

static inline ssize_t read_blk(struct super_block *sb, int type, uint blk, dqbuf_t buf)
{
	memset(buf, 0, V2_DQBLKSIZE);
	return sb->s_op->quota_read(sb, type, (char *)buf,
	       V2_DQBLKSIZE, blk << V2_DQBLKSIZE_BITS);
}

static inline ssize_t write_blk(struct super_block *sb, int type, uint blk, dqbuf_t buf)
{
	return sb->s_op->quota_write(sb, type, (char *)buf,
	       V2_DQBLKSIZE, blk << V2_DQBLKSIZE_BITS);
}

/* Remove empty block from list and return it */
static int get_free_dqblk(struct super_block *sb, int type)
{
	dqbuf_t buf = getdqbuf();
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct v2_disk_dqdbheader *dh = (struct v2_disk_dqdbheader *)buf;
	int ret, blk;

	if (!buf)
		return -ENOMEM;
	if (info->u.v2_i.dqi_free_blk) {
		blk = info->u.v2_i.dqi_free_blk;
		if ((ret = read_blk(sb, type, blk, buf)) < 0)
			goto out_buf;
		info->u.v2_i.dqi_free_blk = le32_to_cpu(dh->dqdh_next_free);
	}
	else {
		memset(buf, 0, V2_DQBLKSIZE);
		/* Assure block allocation... */
		if ((ret = write_blk(sb, type, info->u.v2_i.dqi_blocks, buf)) < 0)
			goto out_buf;
		blk = info->u.v2_i.dqi_blocks++;
	}
	mark_info_dirty(sb, type);
	ret = blk;
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Insert empty block to the list */
static int put_free_dqblk(struct super_block *sb, int type, dqbuf_t buf, uint blk)
{
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct v2_disk_dqdbheader *dh = (struct v2_disk_dqdbheader *)buf;
	int err;

	dh->dqdh_next_free = cpu_to_le32(info->u.v2_i.dqi_free_blk);
	dh->dqdh_prev_free = cpu_to_le32(0);
	dh->dqdh_entries = cpu_to_le16(0);
	info->u.v2_i.dqi_free_blk = blk;
	mark_info_dirty(sb, type);
	/* Some strange block. We had better leave it... */
	if ((err = write_blk(sb, type, blk, buf)) < 0)
		return err;
	return 0;
}

/* Remove given block from the list of blocks with free entries */
static int remove_free_dqentry(struct super_block *sb, int type, dqbuf_t buf, uint blk)
{
	dqbuf_t tmpbuf = getdqbuf();
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct v2_disk_dqdbheader *dh = (struct v2_disk_dqdbheader *)buf;
	uint nextblk = le32_to_cpu(dh->dqdh_next_free), prevblk = le32_to_cpu(dh->dqdh_prev_free);
	int err;

	if (!tmpbuf)
		return -ENOMEM;
	if (nextblk) {
		if ((err = read_blk(sb, type, nextblk, tmpbuf)) < 0)
			goto out_buf;
		((struct v2_disk_dqdbheader *)tmpbuf)->dqdh_prev_free = dh->dqdh_prev_free;
		if ((err = write_blk(sb, type, nextblk, tmpbuf)) < 0)
			goto out_buf;
	}
	if (prevblk) {
		if ((err = read_blk(sb, type, prevblk, tmpbuf)) < 0)
			goto out_buf;
		((struct v2_disk_dqdbheader *)tmpbuf)->dqdh_next_free = dh->dqdh_next_free;
		if ((err = write_blk(sb, type, prevblk, tmpbuf)) < 0)
			goto out_buf;
	}
	else {
		info->u.v2_i.dqi_free_entry = nextblk;
		mark_info_dirty(sb, type);
	}
	freedqbuf(tmpbuf);
	dh->dqdh_next_free = dh->dqdh_prev_free = cpu_to_le32(0);
	/* No matter whether write succeeds block is out of list */
	if (write_blk(sb, type, blk, buf) < 0)
		printk(KERN_ERR "VFS: Can't write block (%u) with free entries.\n", blk);
	return 0;
out_buf:
	freedqbuf(tmpbuf);
	return err;
}

/* Insert given block to the beginning of list with free entries */
static int insert_free_dqentry(struct super_block *sb, int type, dqbuf_t buf, uint blk)
{
	dqbuf_t tmpbuf = getdqbuf();
	struct mem_dqinfo *info = sb_dqinfo(sb, type);
	struct v2_disk_dqdbheader *dh = (struct v2_disk_dqdbheader *)buf;
	int err;

	if (!tmpbuf)
		return -ENOMEM;
	dh->dqdh_next_free = cpu_to_le32(info->u.v2_i.dqi_free_entry);
	dh->dqdh_prev_free = cpu_to_le32(0);
	if ((err = write_blk(sb, type, blk, buf)) < 0)
		goto out_buf;
	if (info->u.v2_i.dqi_free_entry) {
		if ((err = read_blk(sb, type, info->u.v2_i.dqi_free_entry, tmpbuf)) < 0)
			goto out_buf;
		((struct v2_disk_dqdbheader *)tmpbuf)->dqdh_prev_free = cpu_to_le32(blk);
		if ((err = write_blk(sb, type, info->u.v2_i.dqi_free_entry, tmpbuf)) < 0)
			goto out_buf;
	}
	freedqbuf(tmpbuf);
	info->u.v2_i.dqi_free_entry = blk;
	mark_info_dirty(sb, type);
	return 0;
out_buf:
	freedqbuf(tmpbuf);
	return err;
}

/* Find space for dquot */
static uint find_free_dqentry(struct dquot *dquot, int *err)
{
	struct super_block *sb = dquot->dq_sb;
	struct mem_dqinfo *info = sb_dqopt(sb)->info+dquot->dq_type;
	uint blk, i;
	struct v2_disk_dqdbheader *dh;
	struct v2_disk_dqblk *ddquot;
	struct v2_disk_dqblk fakedquot;
	dqbuf_t buf;

	*err = 0;
	if (!(buf = getdqbuf())) {
		*err = -ENOMEM;
		return 0;
	}
	dh = (struct v2_disk_dqdbheader *)buf;
	ddquot = GETENTRIES(buf);
	if (info->u.v2_i.dqi_free_entry) {
		blk = info->u.v2_i.dqi_free_entry;
		if ((*err = read_blk(sb, dquot->dq_type, blk, buf)) < 0)
			goto out_buf;
	}
	else {
		blk = get_free_dqblk(sb, dquot->dq_type);
		if ((int)blk < 0) {
			*err = blk;
			freedqbuf(buf);
			return 0;
		}
		memset(buf, 0, V2_DQBLKSIZE);
		/* This is enough as block is already zeroed and entry list is empty... */
		info->u.v2_i.dqi_free_entry = blk;
		mark_info_dirty(sb, dquot->dq_type);
	}
	if (le16_to_cpu(dh->dqdh_entries)+1 >= V2_DQSTRINBLK)	/* Block will be full? */
		if ((*err = remove_free_dqentry(sb, dquot->dq_type, buf, blk)) < 0) {
			printk(KERN_ERR "VFS: find_free_dqentry(): Can't remove block (%u) from entry free list.\n", blk);
			goto out_buf;
		}
	dh->dqdh_entries = cpu_to_le16(le16_to_cpu(dh->dqdh_entries)+1);
	memset(&fakedquot, 0, sizeof(struct v2_disk_dqblk));
	/* Find free structure in block */
	for (i = 0; i < V2_DQSTRINBLK && memcmp(&fakedquot, ddquot+i, sizeof(struct v2_disk_dqblk)); i++);
#ifdef __QUOTA_V2_PARANOIA
	if (i == V2_DQSTRINBLK) {
		printk(KERN_ERR "VFS: find_free_dqentry(): Data block full but it shouldn't.\n");
		*err = -EIO;
		goto out_buf;
	}
#endif
	if ((*err = write_blk(sb, dquot->dq_type, blk, buf)) < 0) {
		printk(KERN_ERR "VFS: find_free_dqentry(): Can't write quota data block %u.\n", blk);
		goto out_buf;
	}
	dquot->dq_off = (blk<<V2_DQBLKSIZE_BITS)+sizeof(struct v2_disk_dqdbheader)+i*sizeof(struct v2_disk_dqblk);
	freedqbuf(buf);
	return blk;
out_buf:
	freedqbuf(buf);
	return 0;
}

/* Insert reference to structure into the trie */
static int do_insert_tree(struct dquot *dquot, uint *treeblk, int depth)
{
	struct super_block *sb = dquot->dq_sb;
	dqbuf_t buf;
	int ret = 0, newson = 0, newact = 0;
	__le32 *ref;
	uint newblk;

	if (!(buf = getdqbuf()))
		return -ENOMEM;
	if (!*treeblk) {
		ret = get_free_dqblk(sb, dquot->dq_type);
		if (ret < 0)
			goto out_buf;
		*treeblk = ret;
		memset(buf, 0, V2_DQBLKSIZE);
		newact = 1;
	}
	else {
		if ((ret = read_blk(sb, dquot->dq_type, *treeblk, buf)) < 0) {
			printk(KERN_ERR "VFS: Can't read tree quota block %u.\n", *treeblk);
			goto out_buf;
		}
	}
	ref = (__le32 *)buf;
	newblk = le32_to_cpu(ref[GETIDINDEX(dquot->dq_id, depth)]);
	if (!newblk)
		newson = 1;
	if (depth == V2_DQTREEDEPTH-1) {
#ifdef __QUOTA_V2_PARANOIA
		if (newblk) {
			printk(KERN_ERR "VFS: Inserting already present quota entry (block %u).\n", le32_to_cpu(ref[GETIDINDEX(dquot->dq_id, depth)]));
			ret = -EIO;
			goto out_buf;
		}
#endif
		newblk = find_free_dqentry(dquot, &ret);
	}
	else
		ret = do_insert_tree(dquot, &newblk, depth+1);
	if (newson && ret >= 0) {
		ref[GETIDINDEX(dquot->dq_id, depth)] = cpu_to_le32(newblk);
		ret = write_blk(sb, dquot->dq_type, *treeblk, buf);
	}
	else if (newact && ret < 0)
		put_free_dqblk(sb, dquot->dq_type, buf, *treeblk);
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Wrapper for inserting quota structure into tree */
static inline int dq_insert_tree(struct dquot *dquot)
{
	int tmp = V2_DQTREEOFF;
	return do_insert_tree(dquot, &tmp, 0);
}

/*
 *	We don't have to be afraid of deadlocks as we never have quotas on quota files...
 */
static int v2_write_dquot(struct dquot *dquot)
{
	int type = dquot->dq_type;
	ssize_t ret;
	struct v2_disk_dqblk ddquot, empty;

	/* dq_off is guarded by dqio_mutex */
	if (!dquot->dq_off)
		if ((ret = dq_insert_tree(dquot)) < 0) {
			printk(KERN_ERR "VFS: Error %zd occurred while creating quota.\n", ret);
			return ret;
		}
	spin_lock(&dq_data_lock);
	mem2diskdqb(&ddquot, &dquot->dq_dqb, dquot->dq_id);
	/* Argh... We may need to write structure full of zeroes but that would be
	 * treated as an empty place by the rest of the code. Format change would
	 * be definitely cleaner but the problems probably are not worth it */
	memset(&empty, 0, sizeof(struct v2_disk_dqblk));
	if (!memcmp(&empty, &ddquot, sizeof(struct v2_disk_dqblk)))
		ddquot.dqb_itime = cpu_to_le64(1);
	spin_unlock(&dq_data_lock);
	ret = dquot->dq_sb->s_op->quota_write(dquot->dq_sb, type,
	      (char *)&ddquot, sizeof(struct v2_disk_dqblk), dquot->dq_off);
	if (ret != sizeof(struct v2_disk_dqblk)) {
		printk(KERN_WARNING "VFS: dquota write failed on dev %s\n", dquot->dq_sb->s_id);
		if (ret >= 0)
			ret = -ENOSPC;
	}
	else
		ret = 0;
	dqstats.writes++;

	return ret;
}

/* Free dquot entry in data block */
static int free_dqentry(struct dquot *dquot, uint blk)
{
	struct super_block *sb = dquot->dq_sb;
	int type = dquot->dq_type;
	struct v2_disk_dqdbheader *dh;
	dqbuf_t buf = getdqbuf();
	int ret = 0;

	if (!buf)
		return -ENOMEM;
	if (dquot->dq_off >> V2_DQBLKSIZE_BITS != blk) {
		printk(KERN_ERR "VFS: Quota structure has offset to other "
		  "block (%u) than it should (%u).\n", blk,
		  (uint)(dquot->dq_off >> V2_DQBLKSIZE_BITS));
		goto out_buf;
	}
	if ((ret = read_blk(sb, type, blk, buf)) < 0) {
		printk(KERN_ERR "VFS: Can't read quota data block %u\n", blk);
		goto out_buf;
	}
	dh = (struct v2_disk_dqdbheader *)buf;
	dh->dqdh_entries = cpu_to_le16(le16_to_cpu(dh->dqdh_entries)-1);
	if (!le16_to_cpu(dh->dqdh_entries)) {	/* Block got free? */
		if ((ret = remove_free_dqentry(sb, type, buf, blk)) < 0 ||
		    (ret = put_free_dqblk(sb, type, buf, blk)) < 0) {
			printk(KERN_ERR "VFS: Can't move quota data block (%u) "
			  "to free list.\n", blk);
			goto out_buf;
		}
	}
	else {
		memset(buf+(dquot->dq_off & ((1 << V2_DQBLKSIZE_BITS)-1)), 0,
		  sizeof(struct v2_disk_dqblk));
		if (le16_to_cpu(dh->dqdh_entries) == V2_DQSTRINBLK-1) {
			/* Insert will write block itself */
			if ((ret = insert_free_dqentry(sb, type, buf, blk)) < 0) {
				printk(KERN_ERR "VFS: Can't insert quota data block (%u) to free entry list.\n", blk);
				goto out_buf;
			}
		}
		else
			if ((ret = write_blk(sb, type, blk, buf)) < 0) {
				printk(KERN_ERR "VFS: Can't write quota data "
				  "block %u\n", blk);
				goto out_buf;
			}
	}
	dquot->dq_off = 0;	/* Quota is now unattached */
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Remove reference to dquot from tree */
static int remove_tree(struct dquot *dquot, uint *blk, int depth)
{
	struct super_block *sb = dquot->dq_sb;
	int type = dquot->dq_type;
	dqbuf_t buf = getdqbuf();
	int ret = 0;
	uint newblk;
	__le32 *ref = (__le32 *)buf;
	
	if (!buf)
		return -ENOMEM;
	if ((ret = read_blk(sb, type, *blk, buf)) < 0) {
		printk(KERN_ERR "VFS: Can't read quota data block %u\n", *blk);
		goto out_buf;
	}
	newblk = le32_to_cpu(ref[GETIDINDEX(dquot->dq_id, depth)]);
	if (depth == V2_DQTREEDEPTH-1) {
		ret = free_dqentry(dquot, newblk);
		newblk = 0;
	}
	else
		ret = remove_tree(dquot, &newblk, depth+1);
	if (ret >= 0 && !newblk) {
		int i;
		ref[GETIDINDEX(dquot->dq_id, depth)] = cpu_to_le32(0);
		for (i = 0; i < V2_DQBLKSIZE && !buf[i]; i++);	/* Block got empty? */
		/* Don't put the root block into the free block list */
		if (i == V2_DQBLKSIZE && *blk != V2_DQTREEOFF) {
			put_free_dqblk(sb, type, buf, *blk);
			*blk = 0;
		}
		else
			if ((ret = write_blk(sb, type, *blk, buf)) < 0)
				printk(KERN_ERR "VFS: Can't write quota tree "
				  "block %u.\n", *blk);
	}
out_buf:
	freedqbuf(buf);
	return ret;	
}

/* Delete dquot from tree */
static int v2_delete_dquot(struct dquot *dquot)
{
	uint tmp = V2_DQTREEOFF;

	if (!dquot->dq_off)	/* Even not allocated? */
		return 0;
	return remove_tree(dquot, &tmp, 0);
}

/* Find entry in block */
static loff_t find_block_dqentry(struct dquot *dquot, uint blk)
{
	dqbuf_t buf = getdqbuf();
	loff_t ret = 0;
	int i;
	struct v2_disk_dqblk *ddquot = GETENTRIES(buf);

	if (!buf)
		return -ENOMEM;
	if ((ret = read_blk(dquot->dq_sb, dquot->dq_type, blk, buf)) < 0) {
		printk(KERN_ERR "VFS: Can't read quota tree block %u.\n", blk);
		goto out_buf;
	}
	if (dquot->dq_id)
		for (i = 0; i < V2_DQSTRINBLK &&
		     le32_to_cpu(ddquot[i].dqb_id) != dquot->dq_id; i++);
	else {	/* ID 0 as a bit more complicated searching... */
		struct v2_disk_dqblk fakedquot;

		memset(&fakedquot, 0, sizeof(struct v2_disk_dqblk));
		for (i = 0; i < V2_DQSTRINBLK; i++)
			if (!le32_to_cpu(ddquot[i].dqb_id) &&
			    memcmp(&fakedquot, ddquot+i, sizeof(struct v2_disk_dqblk)))
				break;
	}
	if (i == V2_DQSTRINBLK) {
		printk(KERN_ERR "VFS: Quota for id %u referenced "
		  "but not present.\n", dquot->dq_id);
		ret = -EIO;
		goto out_buf;
	}
	else
		ret = (blk << V2_DQBLKSIZE_BITS) + sizeof(struct
		  v2_disk_dqdbheader) + i * sizeof(struct v2_disk_dqblk);
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Find entry for given id in the tree */
static loff_t find_tree_dqentry(struct dquot *dquot, uint blk, int depth)
{
	dqbuf_t buf = getdqbuf();
	loff_t ret = 0;
	__le32 *ref = (__le32 *)buf;

	if (!buf)
		return -ENOMEM;
	if ((ret = read_blk(dquot->dq_sb, dquot->dq_type, blk, buf)) < 0) {
		printk(KERN_ERR "VFS: Can't read quota tree block %u.\n", blk);
		goto out_buf;
	}
	ret = 0;
	blk = le32_to_cpu(ref[GETIDINDEX(dquot->dq_id, depth)]);
	if (!blk)	/* No reference? */
		goto out_buf;
	if (depth < V2_DQTREEDEPTH-1)
		ret = find_tree_dqentry(dquot, blk, depth+1);
	else
		ret = find_block_dqentry(dquot, blk);
out_buf:
	freedqbuf(buf);
	return ret;
}

/* Find entry for given id in the tree - wrapper function */
static inline loff_t find_dqentry(struct dquot *dquot)
{
	return find_tree_dqentry(dquot, V2_DQTREEOFF, 0);
}

static int v2_read_dquot(struct dquot *dquot)
{
	int type = dquot->dq_type;
	loff_t offset;
	struct v2_disk_dqblk ddquot, empty;
	int ret = 0;

#ifdef __QUOTA_V2_PARANOIA
	/* Invalidated quota? */
	if (!dquot->dq_sb || !sb_dqopt(dquot->dq_sb)->files[type]) {
		printk(KERN_ERR "VFS: Quota invalidated while reading!\n");
		return -EIO;
	}
#endif
	offset = find_dqentry(dquot);
	if (offset <= 0) {	/* Entry not present? */
		if (offset < 0)
			printk(KERN_ERR "VFS: Can't read quota "
			  "structure for id %u.\n", dquot->dq_id);
		dquot->dq_off = 0;
		set_bit(DQ_FAKE_B, &dquot->dq_flags);
		memset(&dquot->dq_dqb, 0, sizeof(struct mem_dqblk));
		ret = offset;
	}
	else {
		dquot->dq_off = offset;
		if ((ret = dquot->dq_sb->s_op->quota_read(dquot->dq_sb, type,
		    (char *)&ddquot, sizeof(struct v2_disk_dqblk), offset))
		    != sizeof(struct v2_disk_dqblk)) {
			if (ret >= 0)
				ret = -EIO;
			printk(KERN_ERR "VFS: Error while reading quota "
			  "structure for id %u.\n", dquot->dq_id);
			memset(&ddquot, 0, sizeof(struct v2_disk_dqblk));
		}
		else {
			ret = 0;
			/* We need to escape back all-zero structure */
			memset(&empty, 0, sizeof(struct v2_disk_dqblk));
			empty.dqb_itime = cpu_to_le64(1);
			if (!memcmp(&empty, &ddquot, sizeof(struct v2_disk_dqblk)))
				ddquot.dqb_itime = 0;
		}
		disk2memdqb(&dquot->dq_dqb, &ddquot);
		if (!dquot->dq_dqb.dqb_bhardlimit &&
			!dquot->dq_dqb.dqb_bsoftlimit &&
			!dquot->dq_dqb.dqb_ihardlimit &&
			!dquot->dq_dqb.dqb_isoftlimit)
			set_bit(DQ_FAKE_B, &dquot->dq_flags);
	}
	dqstats.reads++;

	return ret;
}

/* Check whether dquot should not be deleted. We know we are
 * the only one operating on dquot (thanks to dq_lock) */
static int v2_release_dquot(struct dquot *dquot)
{
	if (test_bit(DQ_FAKE_B, &dquot->dq_flags) && !(dquot->dq_dqb.dqb_curinodes | dquot->dq_dqb.dqb_curspace))
		return v2_delete_dquot(dquot);
	return 0;
}

static struct quota_format_ops v2_format_ops = {
	.check_quota_file	= v2_check_quota_file,
	.read_file_info		= v2_read_file_info,
	.write_file_info	= v2_write_file_info,
	.free_file_info		= NULL,
	.read_dqblk		= v2_read_dquot,
	.commit_dqblk		= v2_write_dquot,
	.release_dqblk		= v2_release_dquot,
};

static struct quota_format_type v2_quota_format = {
	.qf_fmt_id	= QFMT_VFS_V0,
	.qf_ops		= &v2_format_ops,
	.qf_owner	= THIS_MODULE
};

static int __init init_v2_quota_format(void)
{
	return register_quota_format(&v2_quota_format);
}

static void __exit exit_v2_quota_format(void)
{
	unregister_quota_format(&v2_quota_format);
}

module_init(init_v2_quota_format);
module_exit(exit_v2_quota_format);
