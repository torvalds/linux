/*
 *  linux/fs/ext3/namei.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *  Directory entry file type support and forward compatibility hooks
 *	for B-tree directories by Theodore Ts'o (tytso@mit.edu), 1998
 *  Hash Tree Directory indexing (c)
 *	Daniel Phillips, 2001
 *  Hash Tree Directory indexing porting
 *	Christopher Li, 2002
 *  Hash Tree Directory indexing cleanup
 *	Theodore Ts'o, 2002
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/jbd.h>
#include <linux/time.h>
#include <linux/ext3_fs.h>
#include <linux/ext3_jbd.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include <linux/smp_lock.h>

#include "namei.h"
#include "xattr.h"
#include "acl.h"

/*
 * define how far ahead to read directories while searching them.
 */
#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

static struct buffer_head *ext3_append(handle_t *handle,
					struct inode *inode,
					u32 *block, int *err)
{
	struct buffer_head *bh;

	*block = inode->i_size >> inode->i_sb->s_blocksize_bits;

	if ((bh = ext3_bread(handle, inode, *block, 1, err))) {
		inode->i_size += inode->i_sb->s_blocksize;
		EXT3_I(inode)->i_disksize = inode->i_size;
		ext3_journal_get_write_access(handle,bh);
	}
	return bh;
}

#ifndef assert
#define assert(test) J_ASSERT(test)
#endif

#ifndef swap
#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)
#endif

#ifdef DX_DEBUG
#define dxtrace(command) command
#else
#define dxtrace(command)
#endif

struct fake_dirent
{
	__le32 inode;
	__le16 rec_len;
	u8 name_len;
	u8 file_type;
};

struct dx_countlimit
{
	__le16 limit;
	__le16 count;
};

struct dx_entry
{
	__le32 hash;
	__le32 block;
};

/*
 * dx_root_info is laid out so that if it should somehow get overlaid by a
 * dirent the two low bits of the hash version will be zero.  Therefore, the
 * hash version mod 4 should never be 0.  Sincerely, the paranoia department.
 */

struct dx_root
{
	struct fake_dirent dot;
	char dot_name[4];
	struct fake_dirent dotdot;
	char dotdot_name[4];
	struct dx_root_info
	{
		__le32 reserved_zero;
		u8 hash_version;
		u8 info_length; /* 8 */
		u8 indirect_levels;
		u8 unused_flags;
	}
	info;
	struct dx_entry	entries[0];
};

struct dx_node
{
	struct fake_dirent fake;
	struct dx_entry	entries[0];
};


struct dx_frame
{
	struct buffer_head *bh;
	struct dx_entry *entries;
	struct dx_entry *at;
};

struct dx_map_entry
{
	u32 hash;
	u32 offs;
};

#ifdef CONFIG_EXT3_INDEX
static inline unsigned dx_get_block (struct dx_entry *entry);
static void dx_set_block (struct dx_entry *entry, unsigned value);
static inline unsigned dx_get_hash (struct dx_entry *entry);
static void dx_set_hash (struct dx_entry *entry, unsigned value);
static unsigned dx_get_count (struct dx_entry *entries);
static unsigned dx_get_limit (struct dx_entry *entries);
static void dx_set_count (struct dx_entry *entries, unsigned value);
static void dx_set_limit (struct dx_entry *entries, unsigned value);
static unsigned dx_root_limit (struct inode *dir, unsigned infosize);
static unsigned dx_node_limit (struct inode *dir);
static struct dx_frame *dx_probe(struct dentry *dentry,
				 struct inode *dir,
				 struct dx_hash_info *hinfo,
				 struct dx_frame *frame,
				 int *err);
static void dx_release (struct dx_frame *frames);
static int dx_make_map (struct ext3_dir_entry_2 *de, int size,
			struct dx_hash_info *hinfo, struct dx_map_entry map[]);
static void dx_sort_map(struct dx_map_entry *map, unsigned count);
static struct ext3_dir_entry_2 *dx_move_dirents (char *from, char *to,
		struct dx_map_entry *offsets, int count);
static struct ext3_dir_entry_2* dx_pack_dirents (char *base, int size);
static void dx_insert_block (struct dx_frame *frame, u32 hash, u32 block);
static int ext3_htree_next_block(struct inode *dir, __u32 hash,
				 struct dx_frame *frame,
				 struct dx_frame *frames,
				 __u32 *start_hash);
static struct buffer_head * ext3_dx_find_entry(struct dentry *dentry,
		       struct ext3_dir_entry_2 **res_dir, int *err);
static int ext3_dx_add_entry(handle_t *handle, struct dentry *dentry,
			     struct inode *inode);

/*
 * Future: use high four bits of block for coalesce-on-delete flags
 * Mask them off for now.
 */

static inline unsigned dx_get_block (struct dx_entry *entry)
{
	return le32_to_cpu(entry->block) & 0x00ffffff;
}

static inline void dx_set_block (struct dx_entry *entry, unsigned value)
{
	entry->block = cpu_to_le32(value);
}

static inline unsigned dx_get_hash (struct dx_entry *entry)
{
	return le32_to_cpu(entry->hash);
}

static inline void dx_set_hash (struct dx_entry *entry, unsigned value)
{
	entry->hash = cpu_to_le32(value);
}

static inline unsigned dx_get_count (struct dx_entry *entries)
{
	return le16_to_cpu(((struct dx_countlimit *) entries)->count);
}

static inline unsigned dx_get_limit (struct dx_entry *entries)
{
	return le16_to_cpu(((struct dx_countlimit *) entries)->limit);
}

static inline void dx_set_count (struct dx_entry *entries, unsigned value)
{
	((struct dx_countlimit *) entries)->count = cpu_to_le16(value);
}

static inline void dx_set_limit (struct dx_entry *entries, unsigned value)
{
	((struct dx_countlimit *) entries)->limit = cpu_to_le16(value);
}

static inline unsigned dx_root_limit (struct inode *dir, unsigned infosize)
{
	unsigned entry_space = dir->i_sb->s_blocksize - EXT3_DIR_REC_LEN(1) -
		EXT3_DIR_REC_LEN(2) - infosize;
	return 0? 20: entry_space / sizeof(struct dx_entry);
}

static inline unsigned dx_node_limit (struct inode *dir)
{
	unsigned entry_space = dir->i_sb->s_blocksize - EXT3_DIR_REC_LEN(0);
	return 0? 22: entry_space / sizeof(struct dx_entry);
}

/*
 * Debug
 */
#ifdef DX_DEBUG
static void dx_show_index (char * label, struct dx_entry *entries)
{
        int i, n = dx_get_count (entries);
        printk("%s index ", label);
        for (i = 0; i < n; i++)
        {
                printk("%x->%u ", i? dx_get_hash(entries + i): 0, dx_get_block(entries + i));
        }
        printk("\n");
}

struct stats
{
	unsigned names;
	unsigned space;
	unsigned bcount;
};

static struct stats dx_show_leaf(struct dx_hash_info *hinfo, struct ext3_dir_entry_2 *de,
				 int size, int show_names)
{
	unsigned names = 0, space = 0;
	char *base = (char *) de;
	struct dx_hash_info h = *hinfo;

	printk("names: ");
	while ((char *) de < base + size)
	{
		if (de->inode)
		{
			if (show_names)
			{
				int len = de->name_len;
				char *name = de->name;
				while (len--) printk("%c", *name++);
				ext3fs_dirhash(de->name, de->name_len, &h);
				printk(":%x.%u ", h.hash,
				       ((char *) de - base));
			}
			space += EXT3_DIR_REC_LEN(de->name_len);
			names++;
		}
		de = (struct ext3_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	}
	printk("(%i)\n", names);
	return (struct stats) { names, space, 1 };
}

struct stats dx_show_entries(struct dx_hash_info *hinfo, struct inode *dir,
			     struct dx_entry *entries, int levels)
{
	unsigned blocksize = dir->i_sb->s_blocksize;
	unsigned count = dx_get_count (entries), names = 0, space = 0, i;
	unsigned bcount = 0;
	struct buffer_head *bh;
	int err;
	printk("%i indexed blocks...\n", count);
	for (i = 0; i < count; i++, entries++)
	{
		u32 block = dx_get_block(entries), hash = i? dx_get_hash(entries): 0;
		u32 range = i < count - 1? (dx_get_hash(entries + 1) - hash): ~hash;
		struct stats stats;
		printk("%s%3u:%03u hash %8x/%8x ",levels?"":"   ", i, block, hash, range);
		if (!(bh = ext3_bread (NULL,dir, block, 0,&err))) continue;
		stats = levels?
		   dx_show_entries(hinfo, dir, ((struct dx_node *) bh->b_data)->entries, levels - 1):
		   dx_show_leaf(hinfo, (struct ext3_dir_entry_2 *) bh->b_data, blocksize, 0);
		names += stats.names;
		space += stats.space;
		bcount += stats.bcount;
		brelse (bh);
	}
	if (bcount)
		printk("%snames %u, fullness %u (%u%%)\n", levels?"":"   ",
			names, space/bcount,(space/bcount)*100/blocksize);
	return (struct stats) { names, space, bcount};
}
#endif /* DX_DEBUG */

/*
 * Probe for a directory leaf block to search.
 *
 * dx_probe can return ERR_BAD_DX_DIR, which means there was a format
 * error in the directory index, and the caller should fall back to
 * searching the directory normally.  The callers of dx_probe **MUST**
 * check for this error code, and make sure it never gets reflected
 * back to userspace.
 */
static struct dx_frame *
dx_probe(struct dentry *dentry, struct inode *dir,
	 struct dx_hash_info *hinfo, struct dx_frame *frame_in, int *err)
{
	unsigned count, indirect;
	struct dx_entry *at, *entries, *p, *q, *m;
	struct dx_root *root;
	struct buffer_head *bh;
	struct dx_frame *frame = frame_in;
	u32 hash;

	frame->bh = NULL;
	if (dentry)
		dir = dentry->d_parent->d_inode;
	if (!(bh = ext3_bread (NULL,dir, 0, 0, err)))
		goto fail;
	root = (struct dx_root *) bh->b_data;
	if (root->info.hash_version != DX_HASH_TEA &&
	    root->info.hash_version != DX_HASH_HALF_MD4 &&
	    root->info.hash_version != DX_HASH_LEGACY) {
		ext3_warning(dir->i_sb, __FUNCTION__,
			     "Unrecognised inode hash code %d",
			     root->info.hash_version);
		brelse(bh);
		*err = ERR_BAD_DX_DIR;
		goto fail;
	}
	hinfo->hash_version = root->info.hash_version;
	hinfo->seed = EXT3_SB(dir->i_sb)->s_hash_seed;
	if (dentry)
		ext3fs_dirhash(dentry->d_name.name, dentry->d_name.len, hinfo);
	hash = hinfo->hash;

	if (root->info.unused_flags & 1) {
		ext3_warning(dir->i_sb, __FUNCTION__,
			     "Unimplemented inode hash flags: %#06x",
			     root->info.unused_flags);
		brelse(bh);
		*err = ERR_BAD_DX_DIR;
		goto fail;
	}

	if ((indirect = root->info.indirect_levels) > 1) {
		ext3_warning(dir->i_sb, __FUNCTION__,
			     "Unimplemented inode hash depth: %#06x",
			     root->info.indirect_levels);
		brelse(bh);
		*err = ERR_BAD_DX_DIR;
		goto fail;
	}

	entries = (struct dx_entry *) (((char *)&root->info) +
				       root->info.info_length);
	assert(dx_get_limit(entries) == dx_root_limit(dir,
						      root->info.info_length));
	dxtrace (printk("Look up %x", hash));
	while (1)
	{
		count = dx_get_count(entries);
		assert (count && count <= dx_get_limit(entries));
		p = entries + 1;
		q = entries + count - 1;
		while (p <= q)
		{
			m = p + (q - p)/2;
			dxtrace(printk("."));
			if (dx_get_hash(m) > hash)
				q = m - 1;
			else
				p = m + 1;
		}

		if (0) // linear search cross check
		{
			unsigned n = count - 1;
			at = entries;
			while (n--)
			{
				dxtrace(printk(","));
				if (dx_get_hash(++at) > hash)
				{
					at--;
					break;
				}
			}
			assert (at == p - 1);
		}

		at = p - 1;
		dxtrace(printk(" %x->%u\n", at == entries? 0: dx_get_hash(at), dx_get_block(at)));
		frame->bh = bh;
		frame->entries = entries;
		frame->at = at;
		if (!indirect--) return frame;
		if (!(bh = ext3_bread (NULL,dir, dx_get_block(at), 0, err)))
			goto fail2;
		at = entries = ((struct dx_node *) bh->b_data)->entries;
		assert (dx_get_limit(entries) == dx_node_limit (dir));
		frame++;
	}
fail2:
	while (frame >= frame_in) {
		brelse(frame->bh);
		frame--;
	}
fail:
	return NULL;
}

static void dx_release (struct dx_frame *frames)
{
	if (frames[0].bh == NULL)
		return;

	if (((struct dx_root *) frames[0].bh->b_data)->info.indirect_levels)
		brelse(frames[1].bh);
	brelse(frames[0].bh);
}

/*
 * This function increments the frame pointer to search the next leaf
 * block, and reads in the necessary intervening nodes if the search
 * should be necessary.  Whether or not the search is necessary is
 * controlled by the hash parameter.  If the hash value is even, then
 * the search is only continued if the next block starts with that
 * hash value.  This is used if we are searching for a specific file.
 *
 * If the hash value is HASH_NB_ALWAYS, then always go to the next block.
 *
 * This function returns 1 if the caller should continue to search,
 * or 0 if it should not.  If there is an error reading one of the
 * index blocks, it will a negative error code.
 *
 * If start_hash is non-null, it will be filled in with the starting
 * hash of the next page.
 */
static int ext3_htree_next_block(struct inode *dir, __u32 hash,
				 struct dx_frame *frame,
				 struct dx_frame *frames,
				 __u32 *start_hash)
{
	struct dx_frame *p;
	struct buffer_head *bh;
	int err, num_frames = 0;
	__u32 bhash;

	p = frame;
	/*
	 * Find the next leaf page by incrementing the frame pointer.
	 * If we run out of entries in the interior node, loop around and
	 * increment pointer in the parent node.  When we break out of
	 * this loop, num_frames indicates the number of interior
	 * nodes need to be read.
	 */
	while (1) {
		if (++(p->at) < p->entries + dx_get_count(p->entries))
			break;
		if (p == frames)
			return 0;
		num_frames++;
		p--;
	}

	/*
	 * If the hash is 1, then continue only if the next page has a
	 * continuation hash of any value.  This is used for readdir
	 * handling.  Otherwise, check to see if the hash matches the
	 * desired contiuation hash.  If it doesn't, return since
	 * there's no point to read in the successive index pages.
	 */
	bhash = dx_get_hash(p->at);
	if (start_hash)
		*start_hash = bhash;
	if ((hash & 1) == 0) {
		if ((bhash & ~1) != hash)
			return 0;
	}
	/*
	 * If the hash is HASH_NB_ALWAYS, we always go to the next
	 * block so no check is necessary
	 */
	while (num_frames--) {
		if (!(bh = ext3_bread(NULL, dir, dx_get_block(p->at),
				      0, &err)))
			return err; /* Failure */
		p++;
		brelse (p->bh);
		p->bh = bh;
		p->at = p->entries = ((struct dx_node *) bh->b_data)->entries;
	}
	return 1;
}


/*
 * p is at least 6 bytes before the end of page
 */
static inline struct ext3_dir_entry_2 *ext3_next_entry(struct ext3_dir_entry_2 *p)
{
	return (struct ext3_dir_entry_2 *)((char*)p + le16_to_cpu(p->rec_len));
}

/*
 * This function fills a red-black tree with information from a
 * directory block.  It returns the number directory entries loaded
 * into the tree.  If there is an error it is returned in err.
 */
static int htree_dirblock_to_tree(struct file *dir_file,
				  struct inode *dir, int block,
				  struct dx_hash_info *hinfo,
				  __u32 start_hash, __u32 start_minor_hash)
{
	struct buffer_head *bh;
	struct ext3_dir_entry_2 *de, *top;
	int err, count = 0;

	dxtrace(printk("In htree dirblock_to_tree: block %d\n", block));
	if (!(bh = ext3_bread (NULL, dir, block, 0, &err)))
		return err;

	de = (struct ext3_dir_entry_2 *) bh->b_data;
	top = (struct ext3_dir_entry_2 *) ((char *) de +
					   dir->i_sb->s_blocksize -
					   EXT3_DIR_REC_LEN(0));
	for (; de < top; de = ext3_next_entry(de)) {
		if (!ext3_check_dir_entry("htree_dirblock_to_tree", dir, de, bh,
					(block<<EXT3_BLOCK_SIZE_BITS(dir->i_sb))
						+((char *)de - bh->b_data))) {
			/* On error, skip the f_pos to the next block. */
			dir_file->f_pos = (dir_file->f_pos |
					(dir->i_sb->s_blocksize - 1)) + 1;
			brelse (bh);
			return count;
		}
		ext3fs_dirhash(de->name, de->name_len, hinfo);
		if ((hinfo->hash < start_hash) ||
		    ((hinfo->hash == start_hash) &&
		     (hinfo->minor_hash < start_minor_hash)))
			continue;
		if (de->inode == 0)
			continue;
		if ((err = ext3_htree_store_dirent(dir_file,
				   hinfo->hash, hinfo->minor_hash, de)) != 0) {
			brelse(bh);
			return err;
		}
		count++;
	}
	brelse(bh);
	return count;
}


/*
 * This function fills a red-black tree with information from a
 * directory.  We start scanning the directory in hash order, starting
 * at start_hash and start_minor_hash.
 *
 * This function returns the number of entries inserted into the tree,
 * or a negative error code.
 */
int ext3_htree_fill_tree(struct file *dir_file, __u32 start_hash,
			 __u32 start_minor_hash, __u32 *next_hash)
{
	struct dx_hash_info hinfo;
	struct ext3_dir_entry_2 *de;
	struct dx_frame frames[2], *frame;
	struct inode *dir;
	int block, err;
	int count = 0;
	int ret;
	__u32 hashval;

	dxtrace(printk("In htree_fill_tree, start hash: %x:%x\n", start_hash,
		       start_minor_hash));
	dir = dir_file->f_path.dentry->d_inode;
	if (!(EXT3_I(dir)->i_flags & EXT3_INDEX_FL)) {
		hinfo.hash_version = EXT3_SB(dir->i_sb)->s_def_hash_version;
		hinfo.seed = EXT3_SB(dir->i_sb)->s_hash_seed;
		count = htree_dirblock_to_tree(dir_file, dir, 0, &hinfo,
					       start_hash, start_minor_hash);
		*next_hash = ~0;
		return count;
	}
	hinfo.hash = start_hash;
	hinfo.minor_hash = 0;
	frame = dx_probe(NULL, dir_file->f_path.dentry->d_inode, &hinfo, frames, &err);
	if (!frame)
		return err;

	/* Add '.' and '..' from the htree header */
	if (!start_hash && !start_minor_hash) {
		de = (struct ext3_dir_entry_2 *) frames[0].bh->b_data;
		if ((err = ext3_htree_store_dirent(dir_file, 0, 0, de)) != 0)
			goto errout;
		count++;
	}
	if (start_hash < 2 || (start_hash ==2 && start_minor_hash==0)) {
		de = (struct ext3_dir_entry_2 *) frames[0].bh->b_data;
		de = ext3_next_entry(de);
		if ((err = ext3_htree_store_dirent(dir_file, 2, 0, de)) != 0)
			goto errout;
		count++;
	}

	while (1) {
		block = dx_get_block(frame->at);
		ret = htree_dirblock_to_tree(dir_file, dir, block, &hinfo,
					     start_hash, start_minor_hash);
		if (ret < 0) {
			err = ret;
			goto errout;
		}
		count += ret;
		hashval = ~0;
		ret = ext3_htree_next_block(dir, HASH_NB_ALWAYS,
					    frame, frames, &hashval);
		*next_hash = hashval;
		if (ret < 0) {
			err = ret;
			goto errout;
		}
		/*
		 * Stop if:  (a) there are no more entries, or
		 * (b) we have inserted at least one entry and the
		 * next hash value is not a continuation
		 */
		if ((ret == 0) ||
		    (count && ((hashval & 1) == 0)))
			break;
	}
	dx_release(frames);
	dxtrace(printk("Fill tree: returned %d entries, next hash: %x\n",
		       count, *next_hash));
	return count;
errout:
	dx_release(frames);
	return (err);
}


/*
 * Directory block splitting, compacting
 */

static int dx_make_map (struct ext3_dir_entry_2 *de, int size,
			struct dx_hash_info *hinfo, struct dx_map_entry *map_tail)
{
	int count = 0;
	char *base = (char *) de;
	struct dx_hash_info h = *hinfo;

	while ((char *) de < base + size)
	{
		if (de->name_len && de->inode) {
			ext3fs_dirhash(de->name, de->name_len, &h);
			map_tail--;
			map_tail->hash = h.hash;
			map_tail->offs = (u32) ((char *) de - base);
			count++;
			cond_resched();
		}
		/* XXX: do we need to check rec_len == 0 case? -Chris */
		de = (struct ext3_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	}
	return count;
}

static void dx_sort_map (struct dx_map_entry *map, unsigned count)
{
        struct dx_map_entry *p, *q, *top = map + count - 1;
        int more;
        /* Combsort until bubble sort doesn't suck */
        while (count > 2)
	{
                count = count*10/13;
                if (count - 9 < 2) /* 9, 10 -> 11 */
                        count = 11;
                for (p = top, q = p - count; q >= map; p--, q--)
                        if (p->hash < q->hash)
                                swap(*p, *q);
        }
        /* Garden variety bubble sort */
        do {
                more = 0;
                q = top;
                while (q-- > map)
		{
                        if (q[1].hash >= q[0].hash)
				continue;
                        swap(*(q+1), *q);
                        more = 1;
		}
	} while(more);
}

static void dx_insert_block(struct dx_frame *frame, u32 hash, u32 block)
{
	struct dx_entry *entries = frame->entries;
	struct dx_entry *old = frame->at, *new = old + 1;
	int count = dx_get_count(entries);

	assert(count < dx_get_limit(entries));
	assert(old < entries + count);
	memmove(new + 1, new, (char *)(entries + count) - (char *)(new));
	dx_set_hash(new, hash);
	dx_set_block(new, block);
	dx_set_count(entries, count + 1);
}
#endif


static void ext3_update_dx_flag(struct inode *inode)
{
	if (!EXT3_HAS_COMPAT_FEATURE(inode->i_sb,
				     EXT3_FEATURE_COMPAT_DIR_INDEX))
		EXT3_I(inode)->i_flags &= ~EXT3_INDEX_FL;
}

/*
 * NOTE! unlike strncmp, ext3_match returns 1 for success, 0 for failure.
 *
 * `len <= EXT3_NAME_LEN' is guaranteed by caller.
 * `de != NULL' is guaranteed by caller.
 */
static inline int ext3_match (int len, const char * const name,
			      struct ext3_dir_entry_2 * de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 * Returns 0 if not found, -1 on failure, and 1 on success
 */
static inline int search_dirblock(struct buffer_head * bh,
				  struct inode *dir,
				  struct dentry *dentry,
				  unsigned long offset,
				  struct ext3_dir_entry_2 ** res_dir)
{
	struct ext3_dir_entry_2 * de;
	char * dlimit;
	int de_len;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;

	de = (struct ext3_dir_entry_2 *) bh->b_data;
	dlimit = bh->b_data + dir->i_sb->s_blocksize;
	while ((char *) de < dlimit) {
		/* this code is executed quadratically often */
		/* do minimal checking `by hand' */

		if ((char *) de + namelen <= dlimit &&
		    ext3_match (namelen, name, de)) {
			/* found a match - just to be sure, do a full check */
			if (!ext3_check_dir_entry("ext3_find_entry",
						  dir, de, bh, offset))
				return -1;
			*res_dir = de;
			return 1;
		}
		/* prevent looping on a bad block */
		de_len = le16_to_cpu(de->rec_len);
		if (de_len <= 0)
			return -1;
		offset += de_len;
		de = (struct ext3_dir_entry_2 *) ((char *) de + de_len);
	}
	return 0;
}


/*
 *	ext3_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * The returned buffer_head has ->b_count elevated.  The caller is expected
 * to brelse() it when appropriate.
 */
static struct buffer_head * ext3_find_entry (struct dentry *dentry,
					struct ext3_dir_entry_2 ** res_dir)
{
	struct super_block * sb;
	struct buffer_head * bh_use[NAMEI_RA_SIZE];
	struct buffer_head * bh, *ret = NULL;
	unsigned long start, block, b;
	int ra_max = 0;		/* Number of bh's in the readahead
				   buffer, bh_use[] */
	int ra_ptr = 0;		/* Current index into readahead
				   buffer */
	int num = 0;
	int nblocks, i, err;
	struct inode *dir = dentry->d_parent->d_inode;
	int namelen;
	const u8 *name;
	unsigned blocksize;

	*res_dir = NULL;
	sb = dir->i_sb;
	blocksize = sb->s_blocksize;
	namelen = dentry->d_name.len;
	name = dentry->d_name.name;
	if (namelen > EXT3_NAME_LEN)
		return NULL;
#ifdef CONFIG_EXT3_INDEX
	if (is_dx(dir)) {
		bh = ext3_dx_find_entry(dentry, res_dir, &err);
		/*
		 * On success, or if the error was file not found,
		 * return.  Otherwise, fall back to doing a search the
		 * old fashioned way.
		 */
		if (bh || (err != ERR_BAD_DX_DIR))
			return bh;
		dxtrace(printk("ext3_find_entry: dx failed, falling back\n"));
	}
#endif
	nblocks = dir->i_size >> EXT3_BLOCK_SIZE_BITS(sb);
	start = EXT3_I(dir)->i_dir_start_lookup;
	if (start >= nblocks)
		start = 0;
	block = start;
restart:
	do {
		/*
		 * We deal with the read-ahead logic here.
		 */
		if (ra_ptr >= ra_max) {
			/* Refill the readahead buffer */
			ra_ptr = 0;
			b = block;
			for (ra_max = 0; ra_max < NAMEI_RA_SIZE; ra_max++) {
				/*
				 * Terminate if we reach the end of the
				 * directory and must wrap, or if our
				 * search has finished at this block.
				 */
				if (b >= nblocks || (num && block == start)) {
					bh_use[ra_max] = NULL;
					break;
				}
				num++;
				bh = ext3_getblk(NULL, dir, b++, 0, &err);
				bh_use[ra_max] = bh;
				if (bh)
					ll_rw_block(READ_META, 1, &bh);
			}
		}
		if ((bh = bh_use[ra_ptr++]) == NULL)
			goto next;
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			/* read error, skip block & hope for the best */
			ext3_error(sb, __FUNCTION__, "reading directory #%lu "
				   "offset %lu", dir->i_ino, block);
			brelse(bh);
			goto next;
		}
		i = search_dirblock(bh, dir, dentry,
			    block << EXT3_BLOCK_SIZE_BITS(sb), res_dir);
		if (i == 1) {
			EXT3_I(dir)->i_dir_start_lookup = block;
			ret = bh;
			goto cleanup_and_exit;
		} else {
			brelse(bh);
			if (i < 0)
				goto cleanup_and_exit;
		}
	next:
		if (++block >= nblocks)
			block = 0;
	} while (block != start);

	/*
	 * If the directory has grown while we were searching, then
	 * search the last part of the directory before giving up.
	 */
	block = nblocks;
	nblocks = dir->i_size >> EXT3_BLOCK_SIZE_BITS(sb);
	if (block < nblocks) {
		start = 0;
		goto restart;
	}

cleanup_and_exit:
	/* Clean up the read-ahead blocks */
	for (; ra_ptr < ra_max; ra_ptr++)
		brelse (bh_use[ra_ptr]);
	return ret;
}

#ifdef CONFIG_EXT3_INDEX
static struct buffer_head * ext3_dx_find_entry(struct dentry *dentry,
		       struct ext3_dir_entry_2 **res_dir, int *err)
{
	struct super_block * sb;
	struct dx_hash_info	hinfo;
	u32 hash;
	struct dx_frame frames[2], *frame;
	struct ext3_dir_entry_2 *de, *top;
	struct buffer_head *bh;
	unsigned long block;
	int retval;
	int namelen = dentry->d_name.len;
	const u8 *name = dentry->d_name.name;
	struct inode *dir = dentry->d_parent->d_inode;

	sb = dir->i_sb;
	/* NFS may look up ".." - look at dx_root directory block */
	if (namelen > 2 || name[0] != '.'||(name[1] != '.' && name[1] != '\0')){
		if (!(frame = dx_probe(dentry, NULL, &hinfo, frames, err)))
			return NULL;
	} else {
		frame = frames;
		frame->bh = NULL;			/* for dx_release() */
		frame->at = (struct dx_entry *)frames;	/* hack for zero entry*/
		dx_set_block(frame->at, 0);		/* dx_root block is 0 */
	}
	hash = hinfo.hash;
	do {
		block = dx_get_block(frame->at);
		if (!(bh = ext3_bread (NULL,dir, block, 0, err)))
			goto errout;
		de = (struct ext3_dir_entry_2 *) bh->b_data;
		top = (struct ext3_dir_entry_2 *) ((char *) de + sb->s_blocksize -
				       EXT3_DIR_REC_LEN(0));
		for (; de < top; de = ext3_next_entry(de))
		if (ext3_match (namelen, name, de)) {
			if (!ext3_check_dir_entry("ext3_find_entry",
						  dir, de, bh,
				  (block<<EXT3_BLOCK_SIZE_BITS(sb))
					  +((char *)de - bh->b_data))) {
				brelse (bh);
				goto errout;
			}
			*res_dir = de;
			dx_release (frames);
			return bh;
		}
		brelse (bh);
		/* Check to see if we should continue to search */
		retval = ext3_htree_next_block(dir, hash, frame,
					       frames, NULL);
		if (retval < 0) {
			ext3_warning(sb, __FUNCTION__,
			     "error reading index page in directory #%lu",
			     dir->i_ino);
			*err = retval;
			goto errout;
		}
	} while (retval == 1);

	*err = -ENOENT;
errout:
	dxtrace(printk("%s not found\n", name));
	dx_release (frames);
	return NULL;
}
#endif

static struct dentry *ext3_lookup(struct inode * dir, struct dentry *dentry, struct nameidata *nd)
{
	struct inode * inode;
	struct ext3_dir_entry_2 * de;
	struct buffer_head * bh;

	if (dentry->d_name.len > EXT3_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	bh = ext3_find_entry(dentry, &de);
	inode = NULL;
	if (bh) {
		unsigned long ino = le32_to_cpu(de->inode);
		brelse (bh);
		if (!ext3_valid_inum(dir->i_sb, ino)) {
			ext3_error(dir->i_sb, "ext3_lookup",
				   "bad inode number: %lu", ino);
			inode = NULL;
		} else
			inode = iget(dir->i_sb, ino);

		if (!inode)
			return ERR_PTR(-EACCES);
	}
	return d_splice_alias(inode, dentry);
}


struct dentry *ext3_get_parent(struct dentry *child)
{
	unsigned long ino;
	struct dentry *parent;
	struct inode *inode;
	struct dentry dotdot;
	struct ext3_dir_entry_2 * de;
	struct buffer_head *bh;

	dotdot.d_name.name = "..";
	dotdot.d_name.len = 2;
	dotdot.d_parent = child; /* confusing, isn't it! */

	bh = ext3_find_entry(&dotdot, &de);
	inode = NULL;
	if (!bh)
		return ERR_PTR(-ENOENT);
	ino = le32_to_cpu(de->inode);
	brelse(bh);

	if (!ext3_valid_inum(child->d_inode->i_sb, ino)) {
		ext3_error(child->d_inode->i_sb, "ext3_get_parent",
			   "bad inode number: %lu", ino);
		inode = NULL;
	} else
		inode = iget(child->d_inode->i_sb, ino);

	if (!inode)
		return ERR_PTR(-EACCES);

	parent = d_alloc_anon(inode);
	if (!parent) {
		iput(inode);
		parent = ERR_PTR(-ENOMEM);
	}
	return parent;
}

#define S_SHIFT 12
static unsigned char ext3_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= EXT3_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= EXT3_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= EXT3_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= EXT3_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= EXT3_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= EXT3_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= EXT3_FT_SYMLINK,
};

static inline void ext3_set_de_type(struct super_block *sb,
				struct ext3_dir_entry_2 *de,
				umode_t mode) {
	if (EXT3_HAS_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_FILETYPE))
		de->file_type = ext3_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
}

#ifdef CONFIG_EXT3_INDEX
static struct ext3_dir_entry_2 *
dx_move_dirents(char *from, char *to, struct dx_map_entry *map, int count)
{
	unsigned rec_len = 0;

	while (count--) {
		struct ext3_dir_entry_2 *de = (struct ext3_dir_entry_2 *) (from + map->offs);
		rec_len = EXT3_DIR_REC_LEN(de->name_len);
		memcpy (to, de, rec_len);
		((struct ext3_dir_entry_2 *) to)->rec_len =
				cpu_to_le16(rec_len);
		de->inode = 0;
		map++;
		to += rec_len;
	}
	return (struct ext3_dir_entry_2 *) (to - rec_len);
}

static struct ext3_dir_entry_2* dx_pack_dirents(char *base, int size)
{
	struct ext3_dir_entry_2 *next, *to, *prev, *de = (struct ext3_dir_entry_2 *) base;
	unsigned rec_len = 0;

	prev = to = de;
	while ((char*)de < base + size) {
		next = (struct ext3_dir_entry_2 *) ((char *) de +
						    le16_to_cpu(de->rec_len));
		if (de->inode && de->name_len) {
			rec_len = EXT3_DIR_REC_LEN(de->name_len);
			if (de > to)
				memmove(to, de, rec_len);
			to->rec_len = cpu_to_le16(rec_len);
			prev = to;
			to = (struct ext3_dir_entry_2 *) (((char *) to) + rec_len);
		}
		de = next;
	}
	return prev;
}

static struct ext3_dir_entry_2 *do_split(handle_t *handle, struct inode *dir,
			struct buffer_head **bh,struct dx_frame *frame,
			struct dx_hash_info *hinfo, int *error)
{
	unsigned blocksize = dir->i_sb->s_blocksize;
	unsigned count, continued;
	struct buffer_head *bh2;
	u32 newblock;
	u32 hash2;
	struct dx_map_entry *map;
	char *data1 = (*bh)->b_data, *data2;
	unsigned split;
	struct ext3_dir_entry_2 *de = NULL, *de2;
	int	err;

	bh2 = ext3_append (handle, dir, &newblock, error);
	if (!(bh2)) {
		brelse(*bh);
		*bh = NULL;
		goto errout;
	}

	BUFFER_TRACE(*bh, "get_write_access");
	err = ext3_journal_get_write_access(handle, *bh);
	if (err) {
	journal_error:
		brelse(*bh);
		brelse(bh2);
		*bh = NULL;
		ext3_std_error(dir->i_sb, err);
		goto errout;
	}
	BUFFER_TRACE(frame->bh, "get_write_access");
	err = ext3_journal_get_write_access(handle, frame->bh);
	if (err)
		goto journal_error;

	data2 = bh2->b_data;

	/* create map in the end of data2 block */
	map = (struct dx_map_entry *) (data2 + blocksize);
	count = dx_make_map ((struct ext3_dir_entry_2 *) data1,
			     blocksize, hinfo, map);
	map -= count;
	split = count/2; // need to adjust to actual middle
	dx_sort_map (map, count);
	hash2 = map[split].hash;
	continued = hash2 == map[split - 1].hash;
	dxtrace(printk("Split block %i at %x, %i/%i\n",
		dx_get_block(frame->at), hash2, split, count-split));

	/* Fancy dance to stay within two buffers */
	de2 = dx_move_dirents(data1, data2, map + split, count - split);
	de = dx_pack_dirents(data1,blocksize);
	de->rec_len = cpu_to_le16(data1 + blocksize - (char *) de);
	de2->rec_len = cpu_to_le16(data2 + blocksize - (char *) de2);
	dxtrace(dx_show_leaf (hinfo, (struct ext3_dir_entry_2 *) data1, blocksize, 1));
	dxtrace(dx_show_leaf (hinfo, (struct ext3_dir_entry_2 *) data2, blocksize, 1));

	/* Which block gets the new entry? */
	if (hinfo->hash >= hash2)
	{
		swap(*bh, bh2);
		de = de2;
	}
	dx_insert_block (frame, hash2 + continued, newblock);
	err = ext3_journal_dirty_metadata (handle, bh2);
	if (err)
		goto journal_error;
	err = ext3_journal_dirty_metadata (handle, frame->bh);
	if (err)
		goto journal_error;
	brelse (bh2);
	dxtrace(dx_show_index ("frame", frame->entries));
errout:
	return de;
}
#endif


/*
 * Add a new entry into a directory (leaf) block.  If de is non-NULL,
 * it points to a directory entry which is guaranteed to be large
 * enough for new directory entry.  If de is NULL, then
 * add_dirent_to_buf will attempt search the directory block for
 * space.  It will return -ENOSPC if no space is available, and -EIO
 * and -EEXIST if directory entry already exists.
 *
 * NOTE!  bh is NOT released in the case where ENOSPC is returned.  In
 * all other cases bh is released.
 */
static int add_dirent_to_buf(handle_t *handle, struct dentry *dentry,
			     struct inode *inode, struct ext3_dir_entry_2 *de,
			     struct buffer_head * bh)
{
	struct inode	*dir = dentry->d_parent->d_inode;
	const char	*name = dentry->d_name.name;
	int		namelen = dentry->d_name.len;
	unsigned long	offset = 0;
	unsigned short	reclen;
	int		nlen, rlen, err;
	char		*top;

	reclen = EXT3_DIR_REC_LEN(namelen);
	if (!de) {
		de = (struct ext3_dir_entry_2 *)bh->b_data;
		top = bh->b_data + dir->i_sb->s_blocksize - reclen;
		while ((char *) de <= top) {
			if (!ext3_check_dir_entry("ext3_add_entry", dir, de,
						  bh, offset)) {
				brelse (bh);
				return -EIO;
			}
			if (ext3_match (namelen, name, de)) {
				brelse (bh);
				return -EEXIST;
			}
			nlen = EXT3_DIR_REC_LEN(de->name_len);
			rlen = le16_to_cpu(de->rec_len);
			if ((de->inode? rlen - nlen: rlen) >= reclen)
				break;
			de = (struct ext3_dir_entry_2 *)((char *)de + rlen);
			offset += rlen;
		}
		if ((char *) de > top)
			return -ENOSPC;
	}
	BUFFER_TRACE(bh, "get_write_access");
	err = ext3_journal_get_write_access(handle, bh);
	if (err) {
		ext3_std_error(dir->i_sb, err);
		brelse(bh);
		return err;
	}

	/* By now the buffer is marked for journaling */
	nlen = EXT3_DIR_REC_LEN(de->name_len);
	rlen = le16_to_cpu(de->rec_len);
	if (de->inode) {
		struct ext3_dir_entry_2 *de1 = (struct ext3_dir_entry_2 *)((char *)de + nlen);
		de1->rec_len = cpu_to_le16(rlen - nlen);
		de->rec_len = cpu_to_le16(nlen);
		de = de1;
	}
	de->file_type = EXT3_FT_UNKNOWN;
	if (inode) {
		de->inode = cpu_to_le32(inode->i_ino);
		ext3_set_de_type(dir->i_sb, de, inode->i_mode);
	} else
		de->inode = 0;
	de->name_len = namelen;
	memcpy (de->name, name, namelen);
	/*
	 * XXX shouldn't update any times until successful
	 * completion of syscall, but too many callers depend
	 * on this.
	 *
	 * XXX similarly, too many callers depend on
	 * ext3_new_inode() setting the times, but error
	 * recovery deletes the inode, so the worst that can
	 * happen is that the times are slightly out of date
	 * and/or different from the directory change time.
	 */
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	ext3_update_dx_flag(dir);
	dir->i_version++;
	ext3_mark_inode_dirty(handle, dir);
	BUFFER_TRACE(bh, "call ext3_journal_dirty_metadata");
	err = ext3_journal_dirty_metadata(handle, bh);
	if (err)
		ext3_std_error(dir->i_sb, err);
	brelse(bh);
	return 0;
}

#ifdef CONFIG_EXT3_INDEX
/*
 * This converts a one block unindexed directory to a 3 block indexed
 * directory, and adds the dentry to the indexed directory.
 */
static int make_indexed_dir(handle_t *handle, struct dentry *dentry,
			    struct inode *inode, struct buffer_head *bh)
{
	struct inode	*dir = dentry->d_parent->d_inode;
	const char	*name = dentry->d_name.name;
	int		namelen = dentry->d_name.len;
	struct buffer_head *bh2;
	struct dx_root	*root;
	struct dx_frame	frames[2], *frame;
	struct dx_entry *entries;
	struct ext3_dir_entry_2	*de, *de2;
	char		*data1, *top;
	unsigned	len;
	int		retval;
	unsigned	blocksize;
	struct dx_hash_info hinfo;
	u32		block;
	struct fake_dirent *fde;

	blocksize =  dir->i_sb->s_blocksize;
	dxtrace(printk("Creating index\n"));
	retval = ext3_journal_get_write_access(handle, bh);
	if (retval) {
		ext3_std_error(dir->i_sb, retval);
		brelse(bh);
		return retval;
	}
	root = (struct dx_root *) bh->b_data;

	bh2 = ext3_append (handle, dir, &block, &retval);
	if (!(bh2)) {
		brelse(bh);
		return retval;
	}
	EXT3_I(dir)->i_flags |= EXT3_INDEX_FL;
	data1 = bh2->b_data;

	/* The 0th block becomes the root, move the dirents out */
	fde = &root->dotdot;
	de = (struct ext3_dir_entry_2 *)((char *)fde + le16_to_cpu(fde->rec_len));
	len = ((char *) root) + blocksize - (char *) de;
	memcpy (data1, de, len);
	de = (struct ext3_dir_entry_2 *) data1;
	top = data1 + len;
	while ((char *)(de2=(void*)de+le16_to_cpu(de->rec_len)) < top)
		de = de2;
	de->rec_len = cpu_to_le16(data1 + blocksize - (char *) de);
	/* Initialize the root; the dot dirents already exist */
	de = (struct ext3_dir_entry_2 *) (&root->dotdot);
	de->rec_len = cpu_to_le16(blocksize - EXT3_DIR_REC_LEN(2));
	memset (&root->info, 0, sizeof(root->info));
	root->info.info_length = sizeof(root->info);
	root->info.hash_version = EXT3_SB(dir->i_sb)->s_def_hash_version;
	entries = root->entries;
	dx_set_block (entries, 1);
	dx_set_count (entries, 1);
	dx_set_limit (entries, dx_root_limit(dir, sizeof(root->info)));

	/* Initialize as for dx_probe */
	hinfo.hash_version = root->info.hash_version;
	hinfo.seed = EXT3_SB(dir->i_sb)->s_hash_seed;
	ext3fs_dirhash(name, namelen, &hinfo);
	frame = frames;
	frame->entries = entries;
	frame->at = entries;
	frame->bh = bh;
	bh = bh2;
	de = do_split(handle,dir, &bh, frame, &hinfo, &retval);
	dx_release (frames);
	if (!(de))
		return retval;

	return add_dirent_to_buf(handle, dentry, inode, de, bh);
}
#endif

/*
 *	ext3_add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as ext3_find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static int ext3_add_entry (handle_t *handle, struct dentry *dentry,
	struct inode *inode)
{
	struct inode *dir = dentry->d_parent->d_inode;
	unsigned long offset;
	struct buffer_head * bh;
	struct ext3_dir_entry_2 *de;
	struct super_block * sb;
	int	retval;
#ifdef CONFIG_EXT3_INDEX
	int	dx_fallback=0;
#endif
	unsigned blocksize;
	u32 block, blocks;

	sb = dir->i_sb;
	blocksize = sb->s_blocksize;
	if (!dentry->d_name.len)
		return -EINVAL;
#ifdef CONFIG_EXT3_INDEX
	if (is_dx(dir)) {
		retval = ext3_dx_add_entry(handle, dentry, inode);
		if (!retval || (retval != ERR_BAD_DX_DIR))
			return retval;
		EXT3_I(dir)->i_flags &= ~EXT3_INDEX_FL;
		dx_fallback++;
		ext3_mark_inode_dirty(handle, dir);
	}
#endif
	blocks = dir->i_size >> sb->s_blocksize_bits;
	for (block = 0, offset = 0; block < blocks; block++) {
		bh = ext3_bread(handle, dir, block, 0, &retval);
		if(!bh)
			return retval;
		retval = add_dirent_to_buf(handle, dentry, inode, NULL, bh);
		if (retval != -ENOSPC)
			return retval;

#ifdef CONFIG_EXT3_INDEX
		if (blocks == 1 && !dx_fallback &&
		    EXT3_HAS_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_DIR_INDEX))
			return make_indexed_dir(handle, dentry, inode, bh);
#endif
		brelse(bh);
	}
	bh = ext3_append(handle, dir, &block, &retval);
	if (!bh)
		return retval;
	de = (struct ext3_dir_entry_2 *) bh->b_data;
	de->inode = 0;
	de->rec_len = cpu_to_le16(blocksize);
	return add_dirent_to_buf(handle, dentry, inode, de, bh);
}

#ifdef CONFIG_EXT3_INDEX
/*
 * Returns 0 for success, or a negative error value
 */
static int ext3_dx_add_entry(handle_t *handle, struct dentry *dentry,
			     struct inode *inode)
{
	struct dx_frame frames[2], *frame;
	struct dx_entry *entries, *at;
	struct dx_hash_info hinfo;
	struct buffer_head * bh;
	struct inode *dir = dentry->d_parent->d_inode;
	struct super_block * sb = dir->i_sb;
	struct ext3_dir_entry_2 *de;
	int err;

	frame = dx_probe(dentry, NULL, &hinfo, frames, &err);
	if (!frame)
		return err;
	entries = frame->entries;
	at = frame->at;

	if (!(bh = ext3_bread(handle,dir, dx_get_block(frame->at), 0, &err)))
		goto cleanup;

	BUFFER_TRACE(bh, "get_write_access");
	err = ext3_journal_get_write_access(handle, bh);
	if (err)
		goto journal_error;

	err = add_dirent_to_buf(handle, dentry, inode, NULL, bh);
	if (err != -ENOSPC) {
		bh = NULL;
		goto cleanup;
	}

	/* Block full, should compress but for now just split */
	dxtrace(printk("using %u of %u node entries\n",
		       dx_get_count(entries), dx_get_limit(entries)));
	/* Need to split index? */
	if (dx_get_count(entries) == dx_get_limit(entries)) {
		u32 newblock;
		unsigned icount = dx_get_count(entries);
		int levels = frame - frames;
		struct dx_entry *entries2;
		struct dx_node *node2;
		struct buffer_head *bh2;

		if (levels && (dx_get_count(frames->entries) ==
			       dx_get_limit(frames->entries))) {
			ext3_warning(sb, __FUNCTION__,
				     "Directory index full!");
			err = -ENOSPC;
			goto cleanup;
		}
		bh2 = ext3_append (handle, dir, &newblock, &err);
		if (!(bh2))
			goto cleanup;
		node2 = (struct dx_node *)(bh2->b_data);
		entries2 = node2->entries;
		node2->fake.rec_len = cpu_to_le16(sb->s_blocksize);
		node2->fake.inode = 0;
		BUFFER_TRACE(frame->bh, "get_write_access");
		err = ext3_journal_get_write_access(handle, frame->bh);
		if (err)
			goto journal_error;
		if (levels) {
			unsigned icount1 = icount/2, icount2 = icount - icount1;
			unsigned hash2 = dx_get_hash(entries + icount1);
			dxtrace(printk("Split index %i/%i\n", icount1, icount2));

			BUFFER_TRACE(frame->bh, "get_write_access"); /* index root */
			err = ext3_journal_get_write_access(handle,
							     frames[0].bh);
			if (err)
				goto journal_error;

			memcpy ((char *) entries2, (char *) (entries + icount1),
				icount2 * sizeof(struct dx_entry));
			dx_set_count (entries, icount1);
			dx_set_count (entries2, icount2);
			dx_set_limit (entries2, dx_node_limit(dir));

			/* Which index block gets the new entry? */
			if (at - entries >= icount1) {
				frame->at = at = at - entries - icount1 + entries2;
				frame->entries = entries = entries2;
				swap(frame->bh, bh2);
			}
			dx_insert_block (frames + 0, hash2, newblock);
			dxtrace(dx_show_index ("node", frames[1].entries));
			dxtrace(dx_show_index ("node",
			       ((struct dx_node *) bh2->b_data)->entries));
			err = ext3_journal_dirty_metadata(handle, bh2);
			if (err)
				goto journal_error;
			brelse (bh2);
		} else {
			dxtrace(printk("Creating second level index...\n"));
			memcpy((char *) entries2, (char *) entries,
			       icount * sizeof(struct dx_entry));
			dx_set_limit(entries2, dx_node_limit(dir));

			/* Set up root */
			dx_set_count(entries, 1);
			dx_set_block(entries + 0, newblock);
			((struct dx_root *) frames[0].bh->b_data)->info.indirect_levels = 1;

			/* Add new access path frame */
			frame = frames + 1;
			frame->at = at = at - entries + entries2;
			frame->entries = entries = entries2;
			frame->bh = bh2;
			err = ext3_journal_get_write_access(handle,
							     frame->bh);
			if (err)
				goto journal_error;
		}
		ext3_journal_dirty_metadata(handle, frames[0].bh);
	}
	de = do_split(handle, dir, &bh, frame, &hinfo, &err);
	if (!de)
		goto cleanup;
	err = add_dirent_to_buf(handle, dentry, inode, de, bh);
	bh = NULL;
	goto cleanup;

journal_error:
	ext3_std_error(dir->i_sb, err);
cleanup:
	if (bh)
		brelse(bh);
	dx_release(frames);
	return err;
}
#endif

/*
 * ext3_delete_entry deletes a directory entry by merging it with the
 * previous entry
 */
static int ext3_delete_entry (handle_t *handle,
			      struct inode * dir,
			      struct ext3_dir_entry_2 * de_del,
			      struct buffer_head * bh)
{
	struct ext3_dir_entry_2 * de, * pde;
	int i;

	i = 0;
	pde = NULL;
	de = (struct ext3_dir_entry_2 *) bh->b_data;
	while (i < bh->b_size) {
		if (!ext3_check_dir_entry("ext3_delete_entry", dir, de, bh, i))
			return -EIO;
		if (de == de_del)  {
			BUFFER_TRACE(bh, "get_write_access");
			ext3_journal_get_write_access(handle, bh);
			if (pde)
				pde->rec_len =
					cpu_to_le16(le16_to_cpu(pde->rec_len) +
						    le16_to_cpu(de->rec_len));
			else
				de->inode = 0;
			dir->i_version++;
			BUFFER_TRACE(bh, "call ext3_journal_dirty_metadata");
			ext3_journal_dirty_metadata(handle, bh);
			return 0;
		}
		i += le16_to_cpu(de->rec_len);
		pde = de;
		de = (struct ext3_dir_entry_2 *)
			((char *) de + le16_to_cpu(de->rec_len));
	}
	return -ENOENT;
}

/*
 * ext3_mark_inode_dirty is somewhat expensive, so unlike ext2 we
 * do not perform it in these functions.  We perform it at the call site,
 * if it is needed.
 */
static inline void ext3_inc_count(handle_t *handle, struct inode *inode)
{
	inc_nlink(inode);
}

static inline void ext3_dec_count(handle_t *handle, struct inode *inode)
{
	drop_nlink(inode);
}

static int ext3_add_nondir(handle_t *handle,
		struct dentry *dentry, struct inode *inode)
{
	int err = ext3_add_entry(handle, dentry, inode);
	if (!err) {
		ext3_mark_inode_dirty(handle, inode);
		d_instantiate(dentry, inode);
		return 0;
	}
	ext3_dec_count(handle, inode);
	iput(inode);
	return err;
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate().
 */
static int ext3_create (struct inode * dir, struct dentry * dentry, int mode,
		struct nameidata *nd)
{
	handle_t *handle;
	struct inode * inode;
	int err, retries = 0;

retry:
	handle = ext3_journal_start(dir, EXT3_DATA_TRANS_BLOCKS(dir->i_sb) +
					EXT3_INDEX_EXTRA_TRANS_BLOCKS + 3 +
					2*EXT3_QUOTA_INIT_BLOCKS(dir->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	inode = ext3_new_inode (handle, dir, mode);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &ext3_file_inode_operations;
		inode->i_fop = &ext3_file_operations;
		ext3_set_aops(inode);
		err = ext3_add_nondir(handle, dentry, inode);
	}
	ext3_journal_stop(handle);
	if (err == -ENOSPC && ext3_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext3_mknod (struct inode * dir, struct dentry *dentry,
			int mode, dev_t rdev)
{
	handle_t *handle;
	struct inode *inode;
	int err, retries = 0;

	if (!new_valid_dev(rdev))
		return -EINVAL;

retry:
	handle = ext3_journal_start(dir, EXT3_DATA_TRANS_BLOCKS(dir->i_sb) +
					EXT3_INDEX_EXTRA_TRANS_BLOCKS + 3 +
					2*EXT3_QUOTA_INIT_BLOCKS(dir->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	inode = ext3_new_inode (handle, dir, mode);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		init_special_inode(inode, inode->i_mode, rdev);
#ifdef CONFIG_EXT3_FS_XATTR
		inode->i_op = &ext3_special_inode_operations;
#endif
		err = ext3_add_nondir(handle, dentry, inode);
	}
	ext3_journal_stop(handle);
	if (err == -ENOSPC && ext3_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext3_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	handle_t *handle;
	struct inode * inode;
	struct buffer_head * dir_block;
	struct ext3_dir_entry_2 * de;
	int err, retries = 0;

	if (dir->i_nlink >= EXT3_LINK_MAX)
		return -EMLINK;

retry:
	handle = ext3_journal_start(dir, EXT3_DATA_TRANS_BLOCKS(dir->i_sb) +
					EXT3_INDEX_EXTRA_TRANS_BLOCKS + 3 +
					2*EXT3_QUOTA_INIT_BLOCKS(dir->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	inode = ext3_new_inode (handle, dir, S_IFDIR | mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_stop;

	inode->i_op = &ext3_dir_inode_operations;
	inode->i_fop = &ext3_dir_operations;
	inode->i_size = EXT3_I(inode)->i_disksize = inode->i_sb->s_blocksize;
	dir_block = ext3_bread (handle, inode, 0, 1, &err);
	if (!dir_block) {
		drop_nlink(inode); /* is this nlink == 0? */
		ext3_mark_inode_dirty(handle, inode);
		iput (inode);
		goto out_stop;
	}
	BUFFER_TRACE(dir_block, "get_write_access");
	ext3_journal_get_write_access(handle, dir_block);
	de = (struct ext3_dir_entry_2 *) dir_block->b_data;
	de->inode = cpu_to_le32(inode->i_ino);
	de->name_len = 1;
	de->rec_len = cpu_to_le16(EXT3_DIR_REC_LEN(de->name_len));
	strcpy (de->name, ".");
	ext3_set_de_type(dir->i_sb, de, S_IFDIR);
	de = (struct ext3_dir_entry_2 *)
			((char *) de + le16_to_cpu(de->rec_len));
	de->inode = cpu_to_le32(dir->i_ino);
	de->rec_len = cpu_to_le16(inode->i_sb->s_blocksize-EXT3_DIR_REC_LEN(1));
	de->name_len = 2;
	strcpy (de->name, "..");
	ext3_set_de_type(dir->i_sb, de, S_IFDIR);
	inode->i_nlink = 2;
	BUFFER_TRACE(dir_block, "call ext3_journal_dirty_metadata");
	ext3_journal_dirty_metadata(handle, dir_block);
	brelse (dir_block);
	ext3_mark_inode_dirty(handle, inode);
	err = ext3_add_entry (handle, dentry, inode);
	if (err) {
		inode->i_nlink = 0;
		ext3_mark_inode_dirty(handle, inode);
		iput (inode);
		goto out_stop;
	}
	inc_nlink(dir);
	ext3_update_dx_flag(dir);
	ext3_mark_inode_dirty(handle, dir);
	d_instantiate(dentry, inode);
out_stop:
	ext3_journal_stop(handle);
	if (err == -ENOSPC && ext3_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir (struct inode * inode)
{
	unsigned long offset;
	struct buffer_head * bh;
	struct ext3_dir_entry_2 * de, * de1;
	struct super_block * sb;
	int err = 0;

	sb = inode->i_sb;
	if (inode->i_size < EXT3_DIR_REC_LEN(1) + EXT3_DIR_REC_LEN(2) ||
	    !(bh = ext3_bread (NULL, inode, 0, 0, &err))) {
		if (err)
			ext3_error(inode->i_sb, __FUNCTION__,
				   "error %d reading directory #%lu offset 0",
				   err, inode->i_ino);
		else
			ext3_warning(inode->i_sb, __FUNCTION__,
				     "bad directory (dir #%lu) - no data block",
				     inode->i_ino);
		return 1;
	}
	de = (struct ext3_dir_entry_2 *) bh->b_data;
	de1 = (struct ext3_dir_entry_2 *)
			((char *) de + le16_to_cpu(de->rec_len));
	if (le32_to_cpu(de->inode) != inode->i_ino ||
			!le32_to_cpu(de1->inode) ||
			strcmp (".", de->name) ||
			strcmp ("..", de1->name)) {
		ext3_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no `.' or `..'",
			      inode->i_ino);
		brelse (bh);
		return 1;
	}
	offset = le16_to_cpu(de->rec_len) + le16_to_cpu(de1->rec_len);
	de = (struct ext3_dir_entry_2 *)
			((char *) de1 + le16_to_cpu(de1->rec_len));
	while (offset < inode->i_size ) {
		if (!bh ||
			(void *) de >= (void *) (bh->b_data+sb->s_blocksize)) {
			err = 0;
			brelse (bh);
			bh = ext3_bread (NULL, inode,
				offset >> EXT3_BLOCK_SIZE_BITS(sb), 0, &err);
			if (!bh) {
				if (err)
					ext3_error(sb, __FUNCTION__,
						   "error %d reading directory"
						   " #%lu offset %lu",
						   err, inode->i_ino, offset);
				offset += sb->s_blocksize;
				continue;
			}
			de = (struct ext3_dir_entry_2 *) bh->b_data;
		}
		if (!ext3_check_dir_entry("empty_dir", inode, de, bh, offset)) {
			de = (struct ext3_dir_entry_2 *)(bh->b_data +
							 sb->s_blocksize);
			offset = (offset | (sb->s_blocksize - 1)) + 1;
			continue;
		}
		if (le32_to_cpu(de->inode)) {
			brelse (bh);
			return 0;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ext3_dir_entry_2 *)
				((char *) de + le16_to_cpu(de->rec_len));
	}
	brelse (bh);
	return 1;
}

/* ext3_orphan_add() links an unlinked or truncated inode into a list of
 * such inodes, starting at the superblock, in case we crash before the
 * file is closed/deleted, or in case the inode truncate spans multiple
 * transactions and the last transaction is not recovered after a crash.
 *
 * At filesystem recovery time, we walk this list deleting unlinked
 * inodes and truncating linked inodes in ext3_orphan_cleanup().
 */
int ext3_orphan_add(handle_t *handle, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct ext3_iloc iloc;
	int err = 0, rc;

	lock_super(sb);
	if (!list_empty(&EXT3_I(inode)->i_orphan))
		goto out_unlock;

	/* Orphan handling is only valid for files with data blocks
	 * being truncated, or files being unlinked. */

	/* @@@ FIXME: Observation from aviro:
	 * I think I can trigger J_ASSERT in ext3_orphan_add().  We block
	 * here (on lock_super()), so race with ext3_link() which might bump
	 * ->i_nlink. For, say it, character device. Not a regular file,
	 * not a directory, not a symlink and ->i_nlink > 0.
	 */
	J_ASSERT ((S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
		S_ISLNK(inode->i_mode)) || inode->i_nlink == 0);

	BUFFER_TRACE(EXT3_SB(sb)->s_sbh, "get_write_access");
	err = ext3_journal_get_write_access(handle, EXT3_SB(sb)->s_sbh);
	if (err)
		goto out_unlock;

	err = ext3_reserve_inode_write(handle, inode, &iloc);
	if (err)
		goto out_unlock;

	/* Insert this inode at the head of the on-disk orphan list... */
	NEXT_ORPHAN(inode) = le32_to_cpu(EXT3_SB(sb)->s_es->s_last_orphan);
	EXT3_SB(sb)->s_es->s_last_orphan = cpu_to_le32(inode->i_ino);
	err = ext3_journal_dirty_metadata(handle, EXT3_SB(sb)->s_sbh);
	rc = ext3_mark_iloc_dirty(handle, inode, &iloc);
	if (!err)
		err = rc;

	/* Only add to the head of the in-memory list if all the
	 * previous operations succeeded.  If the orphan_add is going to
	 * fail (possibly taking the journal offline), we can't risk
	 * leaving the inode on the orphan list: stray orphan-list
	 * entries can cause panics at unmount time.
	 *
	 * This is safe: on error we're going to ignore the orphan list
	 * anyway on the next recovery. */
	if (!err)
		list_add(&EXT3_I(inode)->i_orphan, &EXT3_SB(sb)->s_orphan);

	jbd_debug(4, "superblock will point to %lu\n", inode->i_ino);
	jbd_debug(4, "orphan inode %lu will point to %d\n",
			inode->i_ino, NEXT_ORPHAN(inode));
out_unlock:
	unlock_super(sb);
	ext3_std_error(inode->i_sb, err);
	return err;
}

/*
 * ext3_orphan_del() removes an unlinked or truncated inode from the list
 * of such inodes stored on disk, because it is finally being cleaned up.
 */
int ext3_orphan_del(handle_t *handle, struct inode *inode)
{
	struct list_head *prev;
	struct ext3_inode_info *ei = EXT3_I(inode);
	struct ext3_sb_info *sbi;
	unsigned long ino_next;
	struct ext3_iloc iloc;
	int err = 0;

	lock_super(inode->i_sb);
	if (list_empty(&ei->i_orphan)) {
		unlock_super(inode->i_sb);
		return 0;
	}

	ino_next = NEXT_ORPHAN(inode);
	prev = ei->i_orphan.prev;
	sbi = EXT3_SB(inode->i_sb);

	jbd_debug(4, "remove inode %lu from orphan list\n", inode->i_ino);

	list_del_init(&ei->i_orphan);

	/* If we're on an error path, we may not have a valid
	 * transaction handle with which to update the orphan list on
	 * disk, but we still need to remove the inode from the linked
	 * list in memory. */
	if (!handle)
		goto out;

	err = ext3_reserve_inode_write(handle, inode, &iloc);
	if (err)
		goto out_err;

	if (prev == &sbi->s_orphan) {
		jbd_debug(4, "superblock will point to %lu\n", ino_next);
		BUFFER_TRACE(sbi->s_sbh, "get_write_access");
		err = ext3_journal_get_write_access(handle, sbi->s_sbh);
		if (err)
			goto out_brelse;
		sbi->s_es->s_last_orphan = cpu_to_le32(ino_next);
		err = ext3_journal_dirty_metadata(handle, sbi->s_sbh);
	} else {
		struct ext3_iloc iloc2;
		struct inode *i_prev =
			&list_entry(prev, struct ext3_inode_info, i_orphan)->vfs_inode;

		jbd_debug(4, "orphan inode %lu will point to %lu\n",
			  i_prev->i_ino, ino_next);
		err = ext3_reserve_inode_write(handle, i_prev, &iloc2);
		if (err)
			goto out_brelse;
		NEXT_ORPHAN(i_prev) = ino_next;
		err = ext3_mark_iloc_dirty(handle, i_prev, &iloc2);
	}
	if (err)
		goto out_brelse;
	NEXT_ORPHAN(inode) = 0;
	err = ext3_mark_iloc_dirty(handle, inode, &iloc);

out_err:
	ext3_std_error(inode->i_sb, err);
out:
	unlock_super(inode->i_sb);
	return err;

out_brelse:
	brelse(iloc.bh);
	goto out_err;
}

static int ext3_rmdir (struct inode * dir, struct dentry *dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct ext3_dir_entry_2 * de;
	handle_t *handle;

	/* Initialize quotas before so that eventual writes go in
	 * separate transaction */
	DQUOT_INIT(dentry->d_inode);
	handle = ext3_journal_start(dir, EXT3_DELETE_TRANS_BLOCKS(dir->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	retval = -ENOENT;
	bh = ext3_find_entry (dentry, &de);
	if (!bh)
		goto end_rmdir;

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	inode = dentry->d_inode;

	retval = -EIO;
	if (le32_to_cpu(de->inode) != inode->i_ino)
		goto end_rmdir;

	retval = -ENOTEMPTY;
	if (!empty_dir (inode))
		goto end_rmdir;

	retval = ext3_delete_entry(handle, dir, de, bh);
	if (retval)
		goto end_rmdir;
	if (inode->i_nlink != 2)
		ext3_warning (inode->i_sb, "ext3_rmdir",
			      "empty directory has nlink!=2 (%d)",
			      inode->i_nlink);
	inode->i_version++;
	clear_nlink(inode);
	/* There's no need to set i_disksize: the fact that i_nlink is
	 * zero will ensure that the right thing happens during any
	 * recovery. */
	inode->i_size = 0;
	ext3_orphan_add(handle, inode);
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	ext3_mark_inode_dirty(handle, inode);
	drop_nlink(dir);
	ext3_update_dx_flag(dir);
	ext3_mark_inode_dirty(handle, dir);

end_rmdir:
	ext3_journal_stop(handle);
	brelse (bh);
	return retval;
}

static int ext3_unlink(struct inode * dir, struct dentry *dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct ext3_dir_entry_2 * de;
	handle_t *handle;

	/* Initialize quotas before so that eventual writes go
	 * in separate transaction */
	DQUOT_INIT(dentry->d_inode);
	handle = ext3_journal_start(dir, EXT3_DELETE_TRANS_BLOCKS(dir->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	retval = -ENOENT;
	bh = ext3_find_entry (dentry, &de);
	if (!bh)
		goto end_unlink;

	inode = dentry->d_inode;

	retval = -EIO;
	if (le32_to_cpu(de->inode) != inode->i_ino)
		goto end_unlink;

	if (!inode->i_nlink) {
		ext3_warning (inode->i_sb, "ext3_unlink",
			      "Deleting nonexistent file (%lu), %d",
			      inode->i_ino, inode->i_nlink);
		inode->i_nlink = 1;
	}
	retval = ext3_delete_entry(handle, dir, de, bh);
	if (retval)
		goto end_unlink;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	ext3_update_dx_flag(dir);
	ext3_mark_inode_dirty(handle, dir);
	drop_nlink(inode);
	if (!inode->i_nlink)
		ext3_orphan_add(handle, inode);
	inode->i_ctime = dir->i_ctime;
	ext3_mark_inode_dirty(handle, inode);
	retval = 0;

end_unlink:
	ext3_journal_stop(handle);
	brelse (bh);
	return retval;
}

static int ext3_symlink (struct inode * dir,
		struct dentry *dentry, const char * symname)
{
	handle_t *handle;
	struct inode * inode;
	int l, err, retries = 0;

	l = strlen(symname)+1;
	if (l > dir->i_sb->s_blocksize)
		return -ENAMETOOLONG;

retry:
	handle = ext3_journal_start(dir, EXT3_DATA_TRANS_BLOCKS(dir->i_sb) +
					EXT3_INDEX_EXTRA_TRANS_BLOCKS + 5 +
					2*EXT3_QUOTA_INIT_BLOCKS(dir->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	inode = ext3_new_inode (handle, dir, S_IFLNK|S_IRWXUGO);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_stop;

	if (l > sizeof (EXT3_I(inode)->i_data)) {
		inode->i_op = &ext3_symlink_inode_operations;
		ext3_set_aops(inode);
		/*
		 * page_symlink() calls into ext3_prepare/commit_write.
		 * We have a transaction open.  All is sweetness.  It also sets
		 * i_size in generic_commit_write().
		 */
		err = __page_symlink(inode, symname, l,
				mapping_gfp_mask(inode->i_mapping) & ~__GFP_FS);
		if (err) {
			ext3_dec_count(handle, inode);
			ext3_mark_inode_dirty(handle, inode);
			iput (inode);
			goto out_stop;
		}
	} else {
		inode->i_op = &ext3_fast_symlink_inode_operations;
		memcpy((char*)&EXT3_I(inode)->i_data,symname,l);
		inode->i_size = l-1;
	}
	EXT3_I(inode)->i_disksize = inode->i_size;
	err = ext3_add_nondir(handle, dentry, inode);
out_stop:
	ext3_journal_stop(handle);
	if (err == -ENOSPC && ext3_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext3_link (struct dentry * old_dentry,
		struct inode * dir, struct dentry *dentry)
{
	handle_t *handle;
	struct inode *inode = old_dentry->d_inode;
	int err, retries = 0;

	if (inode->i_nlink >= EXT3_LINK_MAX)
		return -EMLINK;

retry:
	handle = ext3_journal_start(dir, EXT3_DATA_TRANS_BLOCKS(dir->i_sb) +
					EXT3_INDEX_EXTRA_TRANS_BLOCKS);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		handle->h_sync = 1;

	inode->i_ctime = CURRENT_TIME_SEC;
	ext3_inc_count(handle, inode);
	atomic_inc(&inode->i_count);

	err = ext3_add_nondir(handle, dentry, inode);
	ext3_journal_stop(handle);
	if (err == -ENOSPC && ext3_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

#define PARENT_INO(buffer) \
	((struct ext3_dir_entry_2 *) ((char *) buffer + \
	le16_to_cpu(((struct ext3_dir_entry_2 *) buffer)->rec_len)))->inode

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int ext3_rename (struct inode * old_dir, struct dentry *old_dentry,
			   struct inode * new_dir,struct dentry *new_dentry)
{
	handle_t *handle;
	struct inode * old_inode, * new_inode;
	struct buffer_head * old_bh, * new_bh, * dir_bh;
	struct ext3_dir_entry_2 * old_de, * new_de;
	int retval;

	old_bh = new_bh = dir_bh = NULL;

	/* Initialize quotas before so that eventual writes go
	 * in separate transaction */
	if (new_dentry->d_inode)
		DQUOT_INIT(new_dentry->d_inode);
	handle = ext3_journal_start(old_dir, 2 *
					EXT3_DATA_TRANS_BLOCKS(old_dir->i_sb) +
					EXT3_INDEX_EXTRA_TRANS_BLOCKS + 2);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir))
		handle->h_sync = 1;

	old_bh = ext3_find_entry (old_dentry, &old_de);
	/*
	 *  Check for inode number is _not_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	old_inode = old_dentry->d_inode;
	retval = -ENOENT;
	if (!old_bh || le32_to_cpu(old_de->inode) != old_inode->i_ino)
		goto end_rename;

	new_inode = new_dentry->d_inode;
	new_bh = ext3_find_entry (new_dentry, &new_de);
	if (new_bh) {
		if (!new_inode) {
			brelse (new_bh);
			new_bh = NULL;
		}
	}
	if (S_ISDIR(old_inode->i_mode)) {
		if (new_inode) {
			retval = -ENOTEMPTY;
			if (!empty_dir (new_inode))
				goto end_rename;
		}
		retval = -EIO;
		dir_bh = ext3_bread (handle, old_inode, 0, 0, &retval);
		if (!dir_bh)
			goto end_rename;
		if (le32_to_cpu(PARENT_INO(dir_bh->b_data)) != old_dir->i_ino)
			goto end_rename;
		retval = -EMLINK;
		if (!new_inode && new_dir!=old_dir &&
				new_dir->i_nlink >= EXT3_LINK_MAX)
			goto end_rename;
	}
	if (!new_bh) {
		retval = ext3_add_entry (handle, new_dentry, old_inode);
		if (retval)
			goto end_rename;
	} else {
		BUFFER_TRACE(new_bh, "get write access");
		ext3_journal_get_write_access(handle, new_bh);
		new_de->inode = cpu_to_le32(old_inode->i_ino);
		if (EXT3_HAS_INCOMPAT_FEATURE(new_dir->i_sb,
					      EXT3_FEATURE_INCOMPAT_FILETYPE))
			new_de->file_type = old_de->file_type;
		new_dir->i_version++;
		BUFFER_TRACE(new_bh, "call ext3_journal_dirty_metadata");
		ext3_journal_dirty_metadata(handle, new_bh);
		brelse(new_bh);
		new_bh = NULL;
	}

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
	 * rename.
	 */
	old_inode->i_ctime = CURRENT_TIME_SEC;
	ext3_mark_inode_dirty(handle, old_inode);

	/*
	 * ok, that's it
	 */
	if (le32_to_cpu(old_de->inode) != old_inode->i_ino ||
	    old_de->name_len != old_dentry->d_name.len ||
	    strncmp(old_de->name, old_dentry->d_name.name, old_de->name_len) ||
	    (retval = ext3_delete_entry(handle, old_dir,
					old_de, old_bh)) == -ENOENT) {
		/* old_de could have moved from under us during htree split, so
		 * make sure that we are deleting the right entry.  We might
		 * also be pointing to a stale entry in the unused part of
		 * old_bh so just checking inum and the name isn't enough. */
		struct buffer_head *old_bh2;
		struct ext3_dir_entry_2 *old_de2;

		old_bh2 = ext3_find_entry(old_dentry, &old_de2);
		if (old_bh2) {
			retval = ext3_delete_entry(handle, old_dir,
						   old_de2, old_bh2);
			brelse(old_bh2);
		}
	}
	if (retval) {
		ext3_warning(old_dir->i_sb, "ext3_rename",
				"Deleting old file (%lu), %d, error=%d",
				old_dir->i_ino, old_dir->i_nlink, retval);
	}

	if (new_inode) {
		drop_nlink(new_inode);
		new_inode->i_ctime = CURRENT_TIME_SEC;
	}
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME_SEC;
	ext3_update_dx_flag(old_dir);
	if (dir_bh) {
		BUFFER_TRACE(dir_bh, "get_write_access");
		ext3_journal_get_write_access(handle, dir_bh);
		PARENT_INO(dir_bh->b_data) = cpu_to_le32(new_dir->i_ino);
		BUFFER_TRACE(dir_bh, "call ext3_journal_dirty_metadata");
		ext3_journal_dirty_metadata(handle, dir_bh);
		drop_nlink(old_dir);
		if (new_inode) {
			drop_nlink(new_inode);
		} else {
			inc_nlink(new_dir);
			ext3_update_dx_flag(new_dir);
			ext3_mark_inode_dirty(handle, new_dir);
		}
	}
	ext3_mark_inode_dirty(handle, old_dir);
	if (new_inode) {
		ext3_mark_inode_dirty(handle, new_inode);
		if (!new_inode->i_nlink)
			ext3_orphan_add(handle, new_inode);
	}
	retval = 0;

end_rename:
	brelse (dir_bh);
	brelse (old_bh);
	brelse (new_bh);
	ext3_journal_stop(handle);
	return retval;
}

/*
 * directories can handle most operations...
 */
struct inode_operations ext3_dir_inode_operations = {
	.create		= ext3_create,
	.lookup		= ext3_lookup,
	.link		= ext3_link,
	.unlink		= ext3_unlink,
	.symlink	= ext3_symlink,
	.mkdir		= ext3_mkdir,
	.rmdir		= ext3_rmdir,
	.mknod		= ext3_mknod,
	.rename		= ext3_rename,
	.setattr	= ext3_setattr,
#ifdef CONFIG_EXT3_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext3_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.permission	= ext3_permission,
};

struct inode_operations ext3_special_inode_operations = {
	.setattr	= ext3_setattr,
#ifdef CONFIG_EXT3_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext3_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.permission	= ext3_permission,
};
