/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 *
 * Trivial changes by Alan Cox to remove EHASHCOLLISION for compatibility
 *
 * Trivial Changes:
 * Rights granted to Hans Reiser to redistribute under other terms providing
 * he accepts all liability including but not limited to patent, fitness
 * for purpose, and direct or indirect claims arising from failure to perform.
 *
 * NO WARRANTY
 */

#include <linux/time.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "reiserfs.h"
#include "acl.h"
#include "xattr.h"
#include <linux/quotaops.h>

#define INC_DIR_INODE_NLINK(i) if (i->i_nlink != 1) { inc_nlink(i); if (i->i_nlink >= REISERFS_LINK_MAX) set_nlink(i, 1); }
#define DEC_DIR_INODE_NLINK(i) if (i->i_nlink != 1) drop_nlink(i);

/*
 * directory item contains array of entry headers. This performs
 * binary search through that array
 */
static int bin_search_in_dir_item(struct reiserfs_dir_entry *de, loff_t off)
{
	struct item_head *ih = de->de_ih;
	struct reiserfs_de_head *deh = de->de_deh;
	int rbound, lbound, j;

	lbound = 0;
	rbound = ih_entry_count(ih) - 1;

	for (j = (rbound + lbound) / 2; lbound <= rbound;
	     j = (rbound + lbound) / 2) {
		if (off < deh_offset(deh + j)) {
			rbound = j - 1;
			continue;
		}
		if (off > deh_offset(deh + j)) {
			lbound = j + 1;
			continue;
		}
		/* this is not name found, but matched third key component */
		de->de_entry_num = j;
		return NAME_FOUND;
	}

	de->de_entry_num = lbound;
	return NAME_NOT_FOUND;
}

/*
 * comment?  maybe something like set de to point to what the path points to?
 */
static inline void set_de_item_location(struct reiserfs_dir_entry *de,
					struct treepath *path)
{
	de->de_bh = get_last_bh(path);
	de->de_ih = tp_item_head(path);
	de->de_deh = B_I_DEH(de->de_bh, de->de_ih);
	de->de_item_num = PATH_LAST_POSITION(path);
}

/*
 * de_bh, de_ih, de_deh (points to first element of array), de_item_num is set
 */
inline void set_de_name_and_namelen(struct reiserfs_dir_entry *de)
{
	struct reiserfs_de_head *deh = de->de_deh + de->de_entry_num;

	BUG_ON(de->de_entry_num >= ih_entry_count(de->de_ih));

	de->de_entrylen = entry_length(de->de_bh, de->de_ih, de->de_entry_num);
	de->de_namelen = de->de_entrylen - (de_with_sd(deh) ? SD_SIZE : 0);
	de->de_name = ih_item_body(de->de_bh, de->de_ih) + deh_location(deh);
	if (de->de_name[de->de_namelen - 1] == 0)
		de->de_namelen = strlen(de->de_name);
}

/* what entry points to */
static inline void set_de_object_key(struct reiserfs_dir_entry *de)
{
	BUG_ON(de->de_entry_num >= ih_entry_count(de->de_ih));
	de->de_dir_id = deh_dir_id(&de->de_deh[de->de_entry_num]);
	de->de_objectid = deh_objectid(&de->de_deh[de->de_entry_num]);
}

static inline void store_de_entry_key(struct reiserfs_dir_entry *de)
{
	struct reiserfs_de_head *deh = de->de_deh + de->de_entry_num;

	BUG_ON(de->de_entry_num >= ih_entry_count(de->de_ih));

	/* store key of the found entry */
	de->de_entry_key.version = KEY_FORMAT_3_5;
	de->de_entry_key.on_disk_key.k_dir_id =
	    le32_to_cpu(de->de_ih->ih_key.k_dir_id);
	de->de_entry_key.on_disk_key.k_objectid =
	    le32_to_cpu(de->de_ih->ih_key.k_objectid);
	set_cpu_key_k_offset(&de->de_entry_key, deh_offset(deh));
	set_cpu_key_k_type(&de->de_entry_key, TYPE_DIRENTRY);
}

/*
 * We assign a key to each directory item, and place multiple entries in a
 * single directory item.  A directory item has a key equal to the key of
 * the first directory entry in it.

 * This function first calls search_by_key, then, if item whose first entry
 * matches is not found it looks for the entry inside directory item found
 * by search_by_key. Fills the path to the entry, and to the entry position
 * in the item
 */
/* The function is NOT SCHEDULE-SAFE! */
int search_by_entry_key(struct super_block *sb, const struct cpu_key *key,
			struct treepath *path, struct reiserfs_dir_entry *de)
{
	int retval;

	retval = search_item(sb, key, path);
	switch (retval) {
	case ITEM_NOT_FOUND:
		if (!PATH_LAST_POSITION(path)) {
			reiserfs_error(sb, "vs-7000", "search_by_key "
				       "returned item position == 0");
			pathrelse(path);
			return IO_ERROR;
		}
		PATH_LAST_POSITION(path)--;

	case ITEM_FOUND:
		break;

	case IO_ERROR:
		return retval;

	default:
		pathrelse(path);
		reiserfs_error(sb, "vs-7002", "no path to here");
		return IO_ERROR;
	}

	set_de_item_location(de, path);

#ifdef CONFIG_REISERFS_CHECK
	if (!is_direntry_le_ih(de->de_ih) ||
	    COMP_SHORT_KEYS(&de->de_ih->ih_key, key)) {
		print_block(de->de_bh, 0, -1, -1);
		reiserfs_panic(sb, "vs-7005", "found item %h is not directory "
			       "item or does not belong to the same directory "
			       "as key %K", de->de_ih, key);
	}
#endif				/* CONFIG_REISERFS_CHECK */

	/*
	 * binary search in directory item by third component of the
	 * key. sets de->de_entry_num of de
	 */
	retval = bin_search_in_dir_item(de, cpu_key_k_offset(key));
	path->pos_in_item = de->de_entry_num;
	if (retval != NAME_NOT_FOUND) {
		/*
		 * ugly, but rename needs de_bh, de_deh, de_name,
		 * de_namelen, de_objectid set
		 */
		set_de_name_and_namelen(de);
		set_de_object_key(de);
	}
	return retval;
}

/* Keyed 32-bit hash function using TEA in a Davis-Meyer function */

/*
 * The third component is hashed, and you can choose from more than
 * one hash function.  Per directory hashes are not yet implemented
 * but are thought about. This function should be moved to hashes.c
 * Jedi, please do so.  -Hans
 */
static __u32 get_third_component(struct super_block *s,
				 const char *name, int len)
{
	__u32 res;

	if (!len || (len == 1 && name[0] == '.'))
		return DOT_OFFSET;
	if (len == 2 && name[0] == '.' && name[1] == '.')
		return DOT_DOT_OFFSET;

	res = REISERFS_SB(s)->s_hash_function(name, len);

	/* take bits from 7-th to 30-th including both bounds */
	res = GET_HASH_VALUE(res);
	if (res == 0)
		/*
		 * needed to have no names before "." and ".." those have hash
		 * value == 0 and generation conters 1 and 2 accordingly
		 */
		res = 128;
	return res + MAX_GENERATION_NUMBER;
}

static int reiserfs_match(struct reiserfs_dir_entry *de,
			  const char *name, int namelen)
{
	int retval = NAME_NOT_FOUND;

	if ((namelen == de->de_namelen) &&
	    !memcmp(de->de_name, name, de->de_namelen))
		retval =
		    (de_visible(de->de_deh + de->de_entry_num) ? NAME_FOUND :
		     NAME_FOUND_INVISIBLE);

	return retval;
}

/* de's de_bh, de_ih, de_deh, de_item_num, de_entry_num are set already */

/* used when hash collisions exist */

static int linear_search_in_dir_item(struct cpu_key *key,
				     struct reiserfs_dir_entry *de,
				     const char *name, int namelen)
{
	struct reiserfs_de_head *deh = de->de_deh;
	int retval;
	int i;

	i = de->de_entry_num;

	if (i == ih_entry_count(de->de_ih) ||
	    GET_HASH_VALUE(deh_offset(deh + i)) !=
	    GET_HASH_VALUE(cpu_key_k_offset(key))) {
		i--;
	}

	RFALSE(de->de_deh != B_I_DEH(de->de_bh, de->de_ih),
	       "vs-7010: array of entry headers not found");

	deh += i;

	for (; i >= 0; i--, deh--) {
		/* hash value does not match, no need to check whole name */
		if (GET_HASH_VALUE(deh_offset(deh)) !=
		    GET_HASH_VALUE(cpu_key_k_offset(key))) {
			return NAME_NOT_FOUND;
		}

		/* mark that this generation number is used */
		if (de->de_gen_number_bit_string)
			set_bit(GET_GENERATION_NUMBER(deh_offset(deh)),
				de->de_gen_number_bit_string);

		/* calculate pointer to name and namelen */
		de->de_entry_num = i;
		set_de_name_and_namelen(de);

		/*
		 * de's de_name, de_namelen, de_recordlen are set.
		 * Fill the rest.
		 */
		if ((retval =
		     reiserfs_match(de, name, namelen)) != NAME_NOT_FOUND) {

			/* key of pointed object */
			set_de_object_key(de);

			store_de_entry_key(de);

			/* retval can be NAME_FOUND or NAME_FOUND_INVISIBLE */
			return retval;
		}
	}

	if (GET_GENERATION_NUMBER(le_ih_k_offset(de->de_ih)) == 0)
		/*
		 * we have reached left most entry in the node. In common we
		 * have to go to the left neighbor, but if generation counter
		 * is 0 already, we know for sure, that there is no name with
		 * the same hash value
		 */
		/*
		 * FIXME: this work correctly only because hash value can not
		 *  be 0. Btw, in case of Yura's hash it is probably possible,
		 * so, this is a bug
		 */
		return NAME_NOT_FOUND;

	RFALSE(de->de_item_num,
	       "vs-7015: two diritems of the same directory in one node?");

	return GOTO_PREVIOUS_ITEM;
}

/*
 * may return NAME_FOUND, NAME_FOUND_INVISIBLE, NAME_NOT_FOUND
 * FIXME: should add something like IOERROR
 */
static int reiserfs_find_entry(struct inode *dir, const char *name, int namelen,
			       struct treepath *path_to_entry,
			       struct reiserfs_dir_entry *de)
{
	struct cpu_key key_to_search;
	int retval;

	if (namelen > REISERFS_MAX_NAME(dir->i_sb->s_blocksize))
		return NAME_NOT_FOUND;

	/* we will search for this key in the tree */
	make_cpu_key(&key_to_search, dir,
		     get_third_component(dir->i_sb, name, namelen),
		     TYPE_DIRENTRY, 3);

	while (1) {
		retval =
		    search_by_entry_key(dir->i_sb, &key_to_search,
					path_to_entry, de);
		if (retval == IO_ERROR) {
			reiserfs_error(dir->i_sb, "zam-7001", "io error");
			return IO_ERROR;
		}

		/* compare names for all entries having given hash value */
		retval =
		    linear_search_in_dir_item(&key_to_search, de, name,
					      namelen);
		/*
		 * there is no need to scan directory anymore.
		 * Given entry found or does not exist
		 */
		if (retval != GOTO_PREVIOUS_ITEM) {
			path_to_entry->pos_in_item = de->de_entry_num;
			return retval;
		}

		/*
		 * there is left neighboring item of this directory
		 * and given entry can be there
		 */
		set_cpu_key_k_offset(&key_to_search,
				     le_ih_k_offset(de->de_ih) - 1);
		pathrelse(path_to_entry);

	}			/* while (1) */
}

static struct dentry *reiserfs_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	int retval;
	struct inode *inode = NULL;
	struct reiserfs_dir_entry de;
	INITIALIZE_PATH(path_to_entry);

	if (REISERFS_MAX_NAME(dir->i_sb->s_blocksize) < dentry->d_name.len)
		return ERR_PTR(-ENAMETOOLONG);

	reiserfs_write_lock(dir->i_sb);

	de.de_gen_number_bit_string = NULL;
	retval =
	    reiserfs_find_entry(dir, dentry->d_name.name, dentry->d_name.len,
				&path_to_entry, &de);
	pathrelse(&path_to_entry);
	if (retval == NAME_FOUND) {
		inode = reiserfs_iget(dir->i_sb,
				      (struct cpu_key *)&de.de_dir_id);
		if (!inode || IS_ERR(inode)) {
			reiserfs_write_unlock(dir->i_sb);
			return ERR_PTR(-EACCES);
		}

		/*
		 * Propagate the private flag so we know we're
		 * in the priv tree
		 */
		if (IS_PRIVATE(dir))
			inode->i_flags |= S_PRIVATE;
	}
	reiserfs_write_unlock(dir->i_sb);
	if (retval == IO_ERROR) {
		return ERR_PTR(-EIO);
	}

	return d_splice_alias(inode, dentry);
}

/*
 * looks up the dentry of the parent directory for child.
 * taken from ext2_get_parent
 */
struct dentry *reiserfs_get_parent(struct dentry *child)
{
	int retval;
	struct inode *inode = NULL;
	struct reiserfs_dir_entry de;
	INITIALIZE_PATH(path_to_entry);
	struct inode *dir = d_inode(child);

	if (dir->i_nlink == 0) {
		return ERR_PTR(-ENOENT);
	}
	de.de_gen_number_bit_string = NULL;

	reiserfs_write_lock(dir->i_sb);
	retval = reiserfs_find_entry(dir, "..", 2, &path_to_entry, &de);
	pathrelse(&path_to_entry);
	if (retval != NAME_FOUND) {
		reiserfs_write_unlock(dir->i_sb);
		return ERR_PTR(-ENOENT);
	}
	inode = reiserfs_iget(dir->i_sb, (struct cpu_key *)&de.de_dir_id);
	reiserfs_write_unlock(dir->i_sb);

	return d_obtain_alias(inode);
}

/* add entry to the directory (entry can be hidden).

insert definition of when hidden directories are used here -Hans

 Does not mark dir   inode dirty, do it after successesfull call to it */

static int reiserfs_add_entry(struct reiserfs_transaction_handle *th,
			      struct inode *dir, const char *name, int namelen,
			      struct inode *inode, int visible)
{
	struct cpu_key entry_key;
	struct reiserfs_de_head *deh;
	INITIALIZE_PATH(path);
	struct reiserfs_dir_entry de;
	DECLARE_BITMAP(bit_string, MAX_GENERATION_NUMBER + 1);
	int gen_number;

	/*
	 * 48 bytes now and we avoid kmalloc if we
	 * create file with short name
	 */
	char small_buf[32 + DEH_SIZE];

	char *buffer;
	int buflen, paste_size;
	int retval;

	BUG_ON(!th->t_trans_id);

	/* cannot allow items to be added into a busy deleted directory */
	if (!namelen)
		return -EINVAL;

	if (namelen > REISERFS_MAX_NAME(dir->i_sb->s_blocksize))
		return -ENAMETOOLONG;

	/* each entry has unique key. compose it */
	make_cpu_key(&entry_key, dir,
		     get_third_component(dir->i_sb, name, namelen),
		     TYPE_DIRENTRY, 3);

	/* get memory for composing the entry */
	buflen = DEH_SIZE + ROUND_UP(namelen);
	if (buflen > sizeof(small_buf)) {
		buffer = kmalloc(buflen, GFP_NOFS);
		if (!buffer)
			return -ENOMEM;
	} else
		buffer = small_buf;

	paste_size =
	    (get_inode_sd_version(dir) ==
	     STAT_DATA_V1) ? (DEH_SIZE + namelen) : buflen;

	/*
	 * fill buffer : directory entry head, name[, dir objectid | ,
	 * stat data | ,stat data, dir objectid ]
	 */
	deh = (struct reiserfs_de_head *)buffer;
	deh->deh_location = 0;	/* JDM Endian safe if 0 */
	put_deh_offset(deh, cpu_key_k_offset(&entry_key));
	deh->deh_state = 0;	/* JDM Endian safe if 0 */
	/* put key (ino analog) to de */

	/* safe: k_dir_id is le */
	deh->deh_dir_id = INODE_PKEY(inode)->k_dir_id;
	/* safe: k_objectid is le */
	deh->deh_objectid = INODE_PKEY(inode)->k_objectid;

	/* copy name */
	memcpy((char *)(deh + 1), name, namelen);
	/* padd by 0s to the 4 byte boundary */
	padd_item((char *)(deh + 1), ROUND_UP(namelen), namelen);

	/*
	 * entry is ready to be pasted into tree, set 'visibility'
	 * and 'stat data in entry' attributes
	 */
	mark_de_without_sd(deh);
	visible ? mark_de_visible(deh) : mark_de_hidden(deh);

	/* find the proper place for the new entry */
	memset(bit_string, 0, sizeof(bit_string));
	de.de_gen_number_bit_string = bit_string;
	retval = reiserfs_find_entry(dir, name, namelen, &path, &de);
	if (retval != NAME_NOT_FOUND) {
		if (buffer != small_buf)
			kfree(buffer);
		pathrelse(&path);

		if (retval == IO_ERROR) {
			return -EIO;
		}

		if (retval != NAME_FOUND) {
			reiserfs_error(dir->i_sb, "zam-7002",
				       "reiserfs_find_entry() returned "
				       "unexpected value (%d)", retval);
		}

		return -EEXIST;
	}

	gen_number =
	    find_first_zero_bit(bit_string,
				MAX_GENERATION_NUMBER + 1);
	if (gen_number > MAX_GENERATION_NUMBER) {
		/* there is no free generation number */
		reiserfs_warning(dir->i_sb, "reiserfs-7010",
				 "Congratulations! we have got hash function "
				 "screwed up");
		if (buffer != small_buf)
			kfree(buffer);
		pathrelse(&path);
		return -EBUSY;
	}
	/* adjust offset of directory enrty */
	put_deh_offset(deh, SET_GENERATION_NUMBER(deh_offset(deh), gen_number));
	set_cpu_key_k_offset(&entry_key, deh_offset(deh));

	/* update max-hash-collisions counter in reiserfs_sb_info */
	PROC_INFO_MAX(th->t_super, max_hash_collisions, gen_number);

	/* we need to re-search for the insertion point */
	if (gen_number != 0) {
		if (search_by_entry_key(dir->i_sb, &entry_key, &path, &de) !=
		    NAME_NOT_FOUND) {
			reiserfs_warning(dir->i_sb, "vs-7032",
					 "entry with this key (%K) already "
					 "exists", &entry_key);

			if (buffer != small_buf)
				kfree(buffer);
			pathrelse(&path);
			return -EBUSY;
		}
	}

	/* perform the insertion of the entry that we have prepared */
	retval =
	    reiserfs_paste_into_item(th, &path, &entry_key, dir, buffer,
				     paste_size);
	if (buffer != small_buf)
		kfree(buffer);
	if (retval) {
		reiserfs_check_path(&path);
		return retval;
	}

	dir->i_size += paste_size;
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	if (!S_ISDIR(inode->i_mode) && visible)
		/* reiserfs_mkdir or reiserfs_rename will do that by itself */
		reiserfs_update_sd(th, dir);

	reiserfs_check_path(&path);
	return 0;
}

/*
 * quota utility function, call if you've had to abort after calling
 * new_inode_init, and have not called reiserfs_new_inode yet.
 * This should only be called on inodes that do not have stat data
 * inserted into the tree yet.
 */
static int drop_new_inode(struct inode *inode)
{
	dquot_drop(inode);
	make_bad_inode(inode);
	inode->i_flags |= S_NOQUOTA;
	iput(inode);
	return 0;
}

/*
 * utility function that does setup for reiserfs_new_inode.
 * dquot_initialize needs lots of credits so it's better to have it
 * outside of a transaction, so we had to pull some bits of
 * reiserfs_new_inode out into this func.
 */
static int new_inode_init(struct inode *inode, struct inode *dir, umode_t mode)
{
	/*
	 * Make inode invalid - just in case we are going to drop it before
	 * the initialization happens
	 */
	INODE_PKEY(inode)->k_objectid = 0;

	/*
	 * the quota init calls have to know who to charge the quota to, so
	 * we have to set uid and gid here
	 */
	inode_init_owner(inode, dir, mode);
	dquot_initialize(inode);
	return 0;
}

static int reiserfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			   bool excl)
{
	int retval;
	struct inode *inode;
	/*
	 * We need blocks for transaction + (user+group)*(quotas
	 * for new inode + update of quota for directory owner)
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 2 +
	    2 * (REISERFS_QUOTA_INIT_BLOCKS(dir->i_sb) +
		 REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb));
	struct reiserfs_transaction_handle th;
	struct reiserfs_security_handle security;

	dquot_initialize(dir);

	if (!(inode = new_inode(dir->i_sb))) {
		return -ENOMEM;
	}
	new_inode_init(inode, dir, mode);

	jbegin_count += reiserfs_cache_default_acl(dir);
	retval = reiserfs_security_init(dir, inode, &dentry->d_name, &security);
	if (retval < 0) {
		drop_new_inode(inode);
		return retval;
	}
	jbegin_count += retval;
	reiserfs_write_lock(dir->i_sb);

	retval = journal_begin(&th, dir->i_sb, jbegin_count);
	if (retval) {
		drop_new_inode(inode);
		goto out_failed;
	}

	retval =
	    reiserfs_new_inode(&th, dir, mode, NULL, 0 /*i_size */ , dentry,
			       inode, &security);
	if (retval)
		goto out_failed;

	inode->i_op = &reiserfs_file_inode_operations;
	inode->i_fop = &reiserfs_file_operations;
	inode->i_mapping->a_ops = &reiserfs_address_space_operations;

	retval =
	    reiserfs_add_entry(&th, dir, dentry->d_name.name,
			       dentry->d_name.len, inode, 1 /*visible */ );
	if (retval) {
		int err;
		drop_nlink(inode);
		reiserfs_update_sd(&th, inode);
		err = journal_end(&th);
		if (err)
			retval = err;
		unlock_new_inode(inode);
		iput(inode);
		goto out_failed;
	}
	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(dir);

	unlock_new_inode(inode);
	d_instantiate(dentry, inode);
	retval = journal_end(&th);

out_failed:
	reiserfs_write_unlock(dir->i_sb);
	return retval;
}

static int reiserfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
			  dev_t rdev)
{
	int retval;
	struct inode *inode;
	struct reiserfs_transaction_handle th;
	struct reiserfs_security_handle security;
	/*
	 * We need blocks for transaction + (user+group)*(quotas
	 * for new inode + update of quota for directory owner)
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 3 +
	    2 * (REISERFS_QUOTA_INIT_BLOCKS(dir->i_sb) +
		 REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb));

	if (!new_valid_dev(rdev))
		return -EINVAL;

	dquot_initialize(dir);

	if (!(inode = new_inode(dir->i_sb))) {
		return -ENOMEM;
	}
	new_inode_init(inode, dir, mode);

	jbegin_count += reiserfs_cache_default_acl(dir);
	retval = reiserfs_security_init(dir, inode, &dentry->d_name, &security);
	if (retval < 0) {
		drop_new_inode(inode);
		return retval;
	}
	jbegin_count += retval;
	reiserfs_write_lock(dir->i_sb);

	retval = journal_begin(&th, dir->i_sb, jbegin_count);
	if (retval) {
		drop_new_inode(inode);
		goto out_failed;
	}

	retval =
	    reiserfs_new_inode(&th, dir, mode, NULL, 0 /*i_size */ , dentry,
			       inode, &security);
	if (retval) {
		goto out_failed;
	}

	inode->i_op = &reiserfs_special_inode_operations;
	init_special_inode(inode, inode->i_mode, rdev);

	/* FIXME: needed for block and char devices only */
	reiserfs_update_sd(&th, inode);

	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(dir);

	retval =
	    reiserfs_add_entry(&th, dir, dentry->d_name.name,
			       dentry->d_name.len, inode, 1 /*visible */ );
	if (retval) {
		int err;
		drop_nlink(inode);
		reiserfs_update_sd(&th, inode);
		err = journal_end(&th);
		if (err)
			retval = err;
		unlock_new_inode(inode);
		iput(inode);
		goto out_failed;
	}

	unlock_new_inode(inode);
	d_instantiate(dentry, inode);
	retval = journal_end(&th);

out_failed:
	reiserfs_write_unlock(dir->i_sb);
	return retval;
}

static int reiserfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int retval;
	struct inode *inode;
	struct reiserfs_transaction_handle th;
	struct reiserfs_security_handle security;
	/*
	 * We need blocks for transaction + (user+group)*(quotas
	 * for new inode + update of quota for directory owner)
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 3 +
	    2 * (REISERFS_QUOTA_INIT_BLOCKS(dir->i_sb) +
		 REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb));

	dquot_initialize(dir);

#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	/*
	 * set flag that new packing locality created and new blocks
	 * for the content of that directory are not displaced yet
	 */
	REISERFS_I(dir)->new_packing_locality = 1;
#endif
	mode = S_IFDIR | mode;
	if (!(inode = new_inode(dir->i_sb))) {
		return -ENOMEM;
	}
	new_inode_init(inode, dir, mode);

	jbegin_count += reiserfs_cache_default_acl(dir);
	retval = reiserfs_security_init(dir, inode, &dentry->d_name, &security);
	if (retval < 0) {
		drop_new_inode(inode);
		return retval;
	}
	jbegin_count += retval;
	reiserfs_write_lock(dir->i_sb);

	retval = journal_begin(&th, dir->i_sb, jbegin_count);
	if (retval) {
		drop_new_inode(inode);
		goto out_failed;
	}

	/*
	 * inc the link count now, so another writer doesn't overflow
	 * it while we sleep later on.
	 */
	INC_DIR_INODE_NLINK(dir)

	    retval = reiserfs_new_inode(&th, dir, mode, NULL /*symlink */ ,
					old_format_only(dir->i_sb) ?
					EMPTY_DIR_SIZE_V1 : EMPTY_DIR_SIZE,
					dentry, inode, &security);
	if (retval) {
		DEC_DIR_INODE_NLINK(dir)
		goto out_failed;
	}

	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(dir);

	inode->i_op = &reiserfs_dir_inode_operations;
	inode->i_fop = &reiserfs_dir_operations;

	/* note, _this_ add_entry will not update dir's stat data */
	retval =
	    reiserfs_add_entry(&th, dir, dentry->d_name.name,
			       dentry->d_name.len, inode, 1 /*visible */ );
	if (retval) {
		int err;
		clear_nlink(inode);
		DEC_DIR_INODE_NLINK(dir);
		reiserfs_update_sd(&th, inode);
		err = journal_end(&th);
		if (err)
			retval = err;
		unlock_new_inode(inode);
		iput(inode);
		goto out_failed;
	}
	/* the above add_entry did not update dir's stat data */
	reiserfs_update_sd(&th, dir);

	unlock_new_inode(inode);
	d_instantiate(dentry, inode);
	retval = journal_end(&th);
out_failed:
	reiserfs_write_unlock(dir->i_sb);
	return retval;
}

static inline int reiserfs_empty_dir(struct inode *inode)
{
	/*
	 * we can cheat because an old format dir cannot have
	 * EMPTY_DIR_SIZE, and a new format dir cannot have
	 * EMPTY_DIR_SIZE_V1.  So, if the inode is either size,
	 * regardless of disk format version, the directory is empty.
	 */
	if (inode->i_size != EMPTY_DIR_SIZE &&
	    inode->i_size != EMPTY_DIR_SIZE_V1) {
		return 0;
	}
	return 1;
}

static int reiserfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int retval, err;
	struct inode *inode;
	struct reiserfs_transaction_handle th;
	int jbegin_count;
	INITIALIZE_PATH(path);
	struct reiserfs_dir_entry de;

	/*
	 * we will be doing 2 balancings and update 2 stat data, we
	 * change quotas of the owner of the directory and of the owner
	 * of the parent directory.  The quota structure is possibly
	 * deleted only on last iput => outside of this transaction
	 */
	jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 2 + 2 +
	    4 * REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb);

	dquot_initialize(dir);

	reiserfs_write_lock(dir->i_sb);
	retval = journal_begin(&th, dir->i_sb, jbegin_count);
	if (retval)
		goto out_rmdir;

	de.de_gen_number_bit_string = NULL;
	if ((retval =
	     reiserfs_find_entry(dir, dentry->d_name.name, dentry->d_name.len,
				 &path, &de)) == NAME_NOT_FOUND) {
		retval = -ENOENT;
		goto end_rmdir;
	} else if (retval == IO_ERROR) {
		retval = -EIO;
		goto end_rmdir;
	}

	inode = d_inode(dentry);

	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(dir);

	if (de.de_objectid != inode->i_ino) {
		/*
		 * FIXME: compare key of an object and a key found in the entry
		 */
		retval = -EIO;
		goto end_rmdir;
	}
	if (!reiserfs_empty_dir(inode)) {
		retval = -ENOTEMPTY;
		goto end_rmdir;
	}

	/* cut entry from dir directory */
	retval = reiserfs_cut_from_item(&th, &path, &de.de_entry_key,
					dir, NULL,	/* page */
					0 /*new file size - not used here */ );
	if (retval < 0)
		goto end_rmdir;

	if (inode->i_nlink != 2 && inode->i_nlink != 1)
		reiserfs_error(inode->i_sb, "reiserfs-7040",
			       "empty directory has nlink != 2 (%d)",
			       inode->i_nlink);

	clear_nlink(inode);
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	reiserfs_update_sd(&th, inode);

	DEC_DIR_INODE_NLINK(dir)
	    dir->i_size -= (DEH_SIZE + de.de_entrylen);
	reiserfs_update_sd(&th, dir);

	/* prevent empty directory from getting lost */
	add_save_link(&th, inode, 0 /* not truncate */ );

	retval = journal_end(&th);
	reiserfs_check_path(&path);
out_rmdir:
	reiserfs_write_unlock(dir->i_sb);
	return retval;

end_rmdir:
	/*
	 * we must release path, because we did not call
	 * reiserfs_cut_from_item, or reiserfs_cut_from_item does not
	 * release path if operation was not complete
	 */
	pathrelse(&path);
	err = journal_end(&th);
	reiserfs_write_unlock(dir->i_sb);
	return err ? err : retval;
}

static int reiserfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int retval, err;
	struct inode *inode;
	struct reiserfs_dir_entry de;
	INITIALIZE_PATH(path);
	struct reiserfs_transaction_handle th;
	int jbegin_count;
	unsigned long savelink;

	dquot_initialize(dir);

	inode = d_inode(dentry);

	/*
	 * in this transaction we can be doing at max two balancings and
	 * update two stat datas, we change quotas of the owner of the
	 * directory and of the owner of the parent directory. The quota
	 * structure is possibly deleted only on iput => outside of
	 * this transaction
	 */
	jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 2 + 2 +
	    4 * REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb);

	reiserfs_write_lock(dir->i_sb);
	retval = journal_begin(&th, dir->i_sb, jbegin_count);
	if (retval)
		goto out_unlink;

	de.de_gen_number_bit_string = NULL;
	if ((retval =
	     reiserfs_find_entry(dir, dentry->d_name.name, dentry->d_name.len,
				 &path, &de)) == NAME_NOT_FOUND) {
		retval = -ENOENT;
		goto end_unlink;
	} else if (retval == IO_ERROR) {
		retval = -EIO;
		goto end_unlink;
	}

	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(dir);

	if (de.de_objectid != inode->i_ino) {
		/*
		 * FIXME: compare key of an object and a key found in the entry
		 */
		retval = -EIO;
		goto end_unlink;
	}

	if (!inode->i_nlink) {
		reiserfs_warning(inode->i_sb, "reiserfs-7042",
				 "deleting nonexistent file (%lu), %d",
				 inode->i_ino, inode->i_nlink);
		set_nlink(inode, 1);
	}

	drop_nlink(inode);

	/*
	 * we schedule before doing the add_save_link call, save the link
	 * count so we don't race
	 */
	savelink = inode->i_nlink;

	retval =
	    reiserfs_cut_from_item(&th, &path, &de.de_entry_key, dir, NULL,
				   0);
	if (retval < 0) {
		inc_nlink(inode);
		goto end_unlink;
	}
	inode->i_ctime = CURRENT_TIME_SEC;
	reiserfs_update_sd(&th, inode);

	dir->i_size -= (de.de_entrylen + DEH_SIZE);
	dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
	reiserfs_update_sd(&th, dir);

	if (!savelink)
		/* prevent file from getting lost */
		add_save_link(&th, inode, 0 /* not truncate */ );

	retval = journal_end(&th);
	reiserfs_check_path(&path);
	reiserfs_write_unlock(dir->i_sb);
	return retval;

end_unlink:
	pathrelse(&path);
	err = journal_end(&th);
	reiserfs_check_path(&path);
	if (err)
		retval = err;
out_unlink:
	reiserfs_write_unlock(dir->i_sb);
	return retval;
}

static int reiserfs_symlink(struct inode *parent_dir,
			    struct dentry *dentry, const char *symname)
{
	int retval;
	struct inode *inode;
	char *name;
	int item_len;
	struct reiserfs_transaction_handle th;
	struct reiserfs_security_handle security;
	int mode = S_IFLNK | S_IRWXUGO;
	/*
	 * We need blocks for transaction + (user+group)*(quotas for
	 * new inode + update of quota for directory owner)
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 3 +
	    2 * (REISERFS_QUOTA_INIT_BLOCKS(parent_dir->i_sb) +
		 REISERFS_QUOTA_TRANS_BLOCKS(parent_dir->i_sb));

	dquot_initialize(parent_dir);

	if (!(inode = new_inode(parent_dir->i_sb))) {
		return -ENOMEM;
	}
	new_inode_init(inode, parent_dir, mode);

	retval = reiserfs_security_init(parent_dir, inode, &dentry->d_name,
					&security);
	if (retval < 0) {
		drop_new_inode(inode);
		return retval;
	}
	jbegin_count += retval;

	reiserfs_write_lock(parent_dir->i_sb);
	item_len = ROUND_UP(strlen(symname));
	if (item_len > MAX_DIRECT_ITEM_LEN(parent_dir->i_sb->s_blocksize)) {
		retval = -ENAMETOOLONG;
		drop_new_inode(inode);
		goto out_failed;
	}

	name = kmalloc(item_len, GFP_NOFS);
	if (!name) {
		drop_new_inode(inode);
		retval = -ENOMEM;
		goto out_failed;
	}
	memcpy(name, symname, strlen(symname));
	padd_item(name, item_len, strlen(symname));

	retval = journal_begin(&th, parent_dir->i_sb, jbegin_count);
	if (retval) {
		drop_new_inode(inode);
		kfree(name);
		goto out_failed;
	}

	retval =
	    reiserfs_new_inode(&th, parent_dir, mode, name, strlen(symname),
			       dentry, inode, &security);
	kfree(name);
	if (retval) {		/* reiserfs_new_inode iputs for us */
		goto out_failed;
	}

	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(parent_dir);

	inode->i_op = &reiserfs_symlink_inode_operations;
	inode->i_mapping->a_ops = &reiserfs_address_space_operations;

	retval = reiserfs_add_entry(&th, parent_dir, dentry->d_name.name,
				    dentry->d_name.len, inode, 1 /*visible */ );
	if (retval) {
		int err;
		drop_nlink(inode);
		reiserfs_update_sd(&th, inode);
		err = journal_end(&th);
		if (err)
			retval = err;
		unlock_new_inode(inode);
		iput(inode);
		goto out_failed;
	}

	unlock_new_inode(inode);
	d_instantiate(dentry, inode);
	retval = journal_end(&th);
out_failed:
	reiserfs_write_unlock(parent_dir->i_sb);
	return retval;
}

static int reiserfs_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *dentry)
{
	int retval;
	struct inode *inode = d_inode(old_dentry);
	struct reiserfs_transaction_handle th;
	/*
	 * We need blocks for transaction + update of quotas for
	 * the owners of the directory
	 */
	int jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 3 +
	    2 * REISERFS_QUOTA_TRANS_BLOCKS(dir->i_sb);

	dquot_initialize(dir);

	reiserfs_write_lock(dir->i_sb);
	if (inode->i_nlink >= REISERFS_LINK_MAX) {
		/* FIXME: sd_nlink is 32 bit for new files */
		reiserfs_write_unlock(dir->i_sb);
		return -EMLINK;
	}

	/* inc before scheduling so reiserfs_unlink knows we are here */
	inc_nlink(inode);

	retval = journal_begin(&th, dir->i_sb, jbegin_count);
	if (retval) {
		drop_nlink(inode);
		reiserfs_write_unlock(dir->i_sb);
		return retval;
	}

	/* create new entry */
	retval =
	    reiserfs_add_entry(&th, dir, dentry->d_name.name,
			       dentry->d_name.len, inode, 1 /*visible */ );

	reiserfs_update_inode_transaction(inode);
	reiserfs_update_inode_transaction(dir);

	if (retval) {
		int err;
		drop_nlink(inode);
		err = journal_end(&th);
		reiserfs_write_unlock(dir->i_sb);
		return err ? err : retval;
	}

	inode->i_ctime = CURRENT_TIME_SEC;
	reiserfs_update_sd(&th, inode);

	ihold(inode);
	d_instantiate(dentry, inode);
	retval = journal_end(&th);
	reiserfs_write_unlock(dir->i_sb);
	return retval;
}

/* de contains information pointing to an entry which */
static int de_still_valid(const char *name, int len,
			  struct reiserfs_dir_entry *de)
{
	struct reiserfs_dir_entry tmp = *de;

	/* recalculate pointer to name and name length */
	set_de_name_and_namelen(&tmp);
	/* FIXME: could check more */
	if (tmp.de_namelen != len || memcmp(name, de->de_name, len))
		return 0;
	return 1;
}

static int entry_points_to_object(const char *name, int len,
				  struct reiserfs_dir_entry *de,
				  struct inode *inode)
{
	if (!de_still_valid(name, len, de))
		return 0;

	if (inode) {
		if (!de_visible(de->de_deh + de->de_entry_num))
			reiserfs_panic(inode->i_sb, "vs-7042",
				       "entry must be visible");
		return (de->de_objectid == inode->i_ino) ? 1 : 0;
	}

	/* this must be added hidden entry */
	if (de_visible(de->de_deh + de->de_entry_num))
		reiserfs_panic(NULL, "vs-7043", "entry must be visible");

	return 1;
}

/* sets key of objectid the entry has to point to */
static void set_ino_in_dir_entry(struct reiserfs_dir_entry *de,
				 struct reiserfs_key *key)
{
	/* JDM These operations are endian safe - both are le */
	de->de_deh[de->de_entry_num].deh_dir_id = key->k_dir_id;
	de->de_deh[de->de_entry_num].deh_objectid = key->k_objectid;
}

/*
 * process, that is going to call fix_nodes/do_balance must hold only
 * one path. If it holds 2 or more, it can get into endless waiting in
 * get_empty_nodes or its clones
 */
static int reiserfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry)
{
	int retval;
	INITIALIZE_PATH(old_entry_path);
	INITIALIZE_PATH(new_entry_path);
	INITIALIZE_PATH(dot_dot_entry_path);
	struct item_head new_entry_ih, old_entry_ih, dot_dot_ih;
	struct reiserfs_dir_entry old_de, new_de, dot_dot_de;
	struct inode *old_inode, *new_dentry_inode;
	struct reiserfs_transaction_handle th;
	int jbegin_count;
	umode_t old_inode_mode;
	unsigned long savelink = 1;
	struct timespec ctime;

	/*
	 * three balancings: (1) old name removal, (2) new name insertion
	 * and (3) maybe "save" link insertion
	 * stat data updates: (1) old directory,
	 * (2) new directory and (3) maybe old object stat data (when it is
	 * directory) and (4) maybe stat data of object to which new entry
	 * pointed initially and (5) maybe block containing ".." of
	 * renamed directory
	 * quota updates: two parent directories
	 */
	jbegin_count =
	    JOURNAL_PER_BALANCE_CNT * 3 + 5 +
	    4 * REISERFS_QUOTA_TRANS_BLOCKS(old_dir->i_sb);

	dquot_initialize(old_dir);
	dquot_initialize(new_dir);

	old_inode = d_inode(old_dentry);
	new_dentry_inode = d_inode(new_dentry);

	/*
	 * make sure that oldname still exists and points to an object we
	 * are going to rename
	 */
	old_de.de_gen_number_bit_string = NULL;
	reiserfs_write_lock(old_dir->i_sb);
	retval =
	    reiserfs_find_entry(old_dir, old_dentry->d_name.name,
				old_dentry->d_name.len, &old_entry_path,
				&old_de);
	pathrelse(&old_entry_path);
	if (retval == IO_ERROR) {
		reiserfs_write_unlock(old_dir->i_sb);
		return -EIO;
	}

	if (retval != NAME_FOUND || old_de.de_objectid != old_inode->i_ino) {
		reiserfs_write_unlock(old_dir->i_sb);
		return -ENOENT;
	}

	old_inode_mode = old_inode->i_mode;
	if (S_ISDIR(old_inode_mode)) {
		/*
		 * make sure that directory being renamed has correct ".."
		 * and that its new parent directory has not too many links
		 * already
		 */
		if (new_dentry_inode) {
			if (!reiserfs_empty_dir(new_dentry_inode)) {
				reiserfs_write_unlock(old_dir->i_sb);
				return -ENOTEMPTY;
			}
		}

		/*
		 * directory is renamed, its parent directory will be changed,
		 * so find ".." entry
		 */
		dot_dot_de.de_gen_number_bit_string = NULL;
		retval =
		    reiserfs_find_entry(old_inode, "..", 2, &dot_dot_entry_path,
					&dot_dot_de);
		pathrelse(&dot_dot_entry_path);
		if (retval != NAME_FOUND) {
			reiserfs_write_unlock(old_dir->i_sb);
			return -EIO;
		}

		/* inode number of .. must equal old_dir->i_ino */
		if (dot_dot_de.de_objectid != old_dir->i_ino) {
			reiserfs_write_unlock(old_dir->i_sb);
			return -EIO;
		}
	}

	retval = journal_begin(&th, old_dir->i_sb, jbegin_count);
	if (retval) {
		reiserfs_write_unlock(old_dir->i_sb);
		return retval;
	}

	/* add new entry (or find the existing one) */
	retval =
	    reiserfs_add_entry(&th, new_dir, new_dentry->d_name.name,
			       new_dentry->d_name.len, old_inode, 0);
	if (retval == -EEXIST) {
		if (!new_dentry_inode) {
			reiserfs_panic(old_dir->i_sb, "vs-7050",
				       "new entry is found, new inode == 0");
		}
	} else if (retval) {
		int err = journal_end(&th);
		reiserfs_write_unlock(old_dir->i_sb);
		return err ? err : retval;
	}

	reiserfs_update_inode_transaction(old_dir);
	reiserfs_update_inode_transaction(new_dir);

	/*
	 * this makes it so an fsync on an open fd for the old name will
	 * commit the rename operation
	 */
	reiserfs_update_inode_transaction(old_inode);

	if (new_dentry_inode)
		reiserfs_update_inode_transaction(new_dentry_inode);

	while (1) {
		/*
		 * look for old name using corresponding entry key
		 * (found by reiserfs_find_entry)
		 */
		if ((retval =
		     search_by_entry_key(new_dir->i_sb, &old_de.de_entry_key,
					 &old_entry_path,
					 &old_de)) != NAME_FOUND) {
			pathrelse(&old_entry_path);
			journal_end(&th);
			reiserfs_write_unlock(old_dir->i_sb);
			return -EIO;
		}

		copy_item_head(&old_entry_ih, tp_item_head(&old_entry_path));

		reiserfs_prepare_for_journal(old_inode->i_sb, old_de.de_bh, 1);

		/* look for new name by reiserfs_find_entry */
		new_de.de_gen_number_bit_string = NULL;
		retval =
		    reiserfs_find_entry(new_dir, new_dentry->d_name.name,
					new_dentry->d_name.len, &new_entry_path,
					&new_de);
		/*
		 * reiserfs_add_entry should not return IO_ERROR,
		 * because it is called with essentially same parameters from
		 * reiserfs_add_entry above, and we'll catch any i/o errors
		 * before we get here.
		 */
		if (retval != NAME_FOUND_INVISIBLE && retval != NAME_FOUND) {
			pathrelse(&new_entry_path);
			pathrelse(&old_entry_path);
			journal_end(&th);
			reiserfs_write_unlock(old_dir->i_sb);
			return -EIO;
		}

		copy_item_head(&new_entry_ih, tp_item_head(&new_entry_path));

		reiserfs_prepare_for_journal(old_inode->i_sb, new_de.de_bh, 1);

		if (S_ISDIR(old_inode->i_mode)) {
			if ((retval =
			     search_by_entry_key(new_dir->i_sb,
						 &dot_dot_de.de_entry_key,
						 &dot_dot_entry_path,
						 &dot_dot_de)) != NAME_FOUND) {
				pathrelse(&dot_dot_entry_path);
				pathrelse(&new_entry_path);
				pathrelse(&old_entry_path);
				journal_end(&th);
				reiserfs_write_unlock(old_dir->i_sb);
				return -EIO;
			}
			copy_item_head(&dot_dot_ih,
				       tp_item_head(&dot_dot_entry_path));
			/* node containing ".." gets into transaction */
			reiserfs_prepare_for_journal(old_inode->i_sb,
						     dot_dot_de.de_bh, 1);
		}
		/*
		 * we should check seals here, not do
		 * this stuff, yes? Then, having
		 * gathered everything into RAM we
		 * should lock the buffers, yes?  -Hans
		 */
		/*
		 * probably.  our rename needs to hold more
		 * than one path at once.  The seals would
		 * have to be written to deal with multi-path
		 * issues -chris
		 */
		/*
		 * sanity checking before doing the rename - avoid races many
		 * of the above checks could have scheduled.  We have to be
		 * sure our items haven't been shifted by another process.
		 */
		if (item_moved(&new_entry_ih, &new_entry_path) ||
		    !entry_points_to_object(new_dentry->d_name.name,
					    new_dentry->d_name.len,
					    &new_de, new_dentry_inode) ||
		    item_moved(&old_entry_ih, &old_entry_path) ||
		    !entry_points_to_object(old_dentry->d_name.name,
					    old_dentry->d_name.len,
					    &old_de, old_inode)) {
			reiserfs_restore_prepared_buffer(old_inode->i_sb,
							 new_de.de_bh);
			reiserfs_restore_prepared_buffer(old_inode->i_sb,
							 old_de.de_bh);
			if (S_ISDIR(old_inode_mode))
				reiserfs_restore_prepared_buffer(old_inode->
								 i_sb,
								 dot_dot_de.
								 de_bh);
			continue;
		}
		if (S_ISDIR(old_inode_mode)) {
			if (item_moved(&dot_dot_ih, &dot_dot_entry_path) ||
			    !entry_points_to_object("..", 2, &dot_dot_de,
						    old_dir)) {
				reiserfs_restore_prepared_buffer(old_inode->
								 i_sb,
								 old_de.de_bh);
				reiserfs_restore_prepared_buffer(old_inode->
								 i_sb,
								 new_de.de_bh);
				reiserfs_restore_prepared_buffer(old_inode->
								 i_sb,
								 dot_dot_de.
								 de_bh);
				continue;
			}
		}

		RFALSE(S_ISDIR(old_inode_mode) &&
		       !buffer_journal_prepared(dot_dot_de.de_bh), "");

		break;
	}

	/*
	 * ok, all the changes can be done in one fell swoop when we
	 * have claimed all the buffers needed.
	 */

	mark_de_visible(new_de.de_deh + new_de.de_entry_num);
	set_ino_in_dir_entry(&new_de, INODE_PKEY(old_inode));
	journal_mark_dirty(&th, new_de.de_bh);

	mark_de_hidden(old_de.de_deh + old_de.de_entry_num);
	journal_mark_dirty(&th, old_de.de_bh);
	ctime = CURRENT_TIME_SEC;
	old_dir->i_ctime = old_dir->i_mtime = ctime;
	new_dir->i_ctime = new_dir->i_mtime = ctime;
	/*
	 * thanks to Alex Adriaanse <alex_a@caltech.edu> for patch
	 * which adds ctime update of renamed object
	 */
	old_inode->i_ctime = ctime;

	if (new_dentry_inode) {
		/* adjust link number of the victim */
		if (S_ISDIR(new_dentry_inode->i_mode)) {
			clear_nlink(new_dentry_inode);
		} else {
			drop_nlink(new_dentry_inode);
		}
		new_dentry_inode->i_ctime = ctime;
		savelink = new_dentry_inode->i_nlink;
	}

	if (S_ISDIR(old_inode_mode)) {
		/* adjust ".." of renamed directory */
		set_ino_in_dir_entry(&dot_dot_de, INODE_PKEY(new_dir));
		journal_mark_dirty(&th, dot_dot_de.de_bh);

		/*
		 * there (in new_dir) was no directory, so it got new link
		 * (".."  of renamed directory)
		 */
		if (!new_dentry_inode)
			INC_DIR_INODE_NLINK(new_dir);

		/* old directory lost one link - ".. " of renamed directory */
		DEC_DIR_INODE_NLINK(old_dir);
	}
	/*
	 * looks like in 2.3.99pre3 brelse is atomic.
	 * so we can use pathrelse
	 */
	pathrelse(&new_entry_path);
	pathrelse(&dot_dot_entry_path);

	/*
	 * FIXME: this reiserfs_cut_from_item's return value may screw up
	 * anybody, but it will panic if will not be able to find the
	 * entry. This needs one more clean up
	 */
	if (reiserfs_cut_from_item
	    (&th, &old_entry_path, &old_de.de_entry_key, old_dir, NULL,
	     0) < 0)
		reiserfs_error(old_dir->i_sb, "vs-7060",
			       "couldn't not cut old name. Fsck later?");

	old_dir->i_size -= DEH_SIZE + old_de.de_entrylen;

	reiserfs_update_sd(&th, old_dir);
	reiserfs_update_sd(&th, new_dir);
	reiserfs_update_sd(&th, old_inode);

	if (new_dentry_inode) {
		if (savelink == 0)
			add_save_link(&th, new_dentry_inode,
				      0 /* not truncate */ );
		reiserfs_update_sd(&th, new_dentry_inode);
	}

	retval = journal_end(&th);
	reiserfs_write_unlock(old_dir->i_sb);
	return retval;
}

/* directories can handle most operations...  */
const struct inode_operations reiserfs_dir_inode_operations = {
	.create = reiserfs_create,
	.lookup = reiserfs_lookup,
	.link = reiserfs_link,
	.unlink = reiserfs_unlink,
	.symlink = reiserfs_symlink,
	.mkdir = reiserfs_mkdir,
	.rmdir = reiserfs_rmdir,
	.mknod = reiserfs_mknod,
	.rename = reiserfs_rename,
	.setattr = reiserfs_setattr,
	.setxattr = reiserfs_setxattr,
	.getxattr = reiserfs_getxattr,
	.listxattr = reiserfs_listxattr,
	.removexattr = reiserfs_removexattr,
	.permission = reiserfs_permission,
	.get_acl = reiserfs_get_acl,
	.set_acl = reiserfs_set_acl,
};

/*
 * symlink operations.. same as page_symlink_inode_operations, with xattr
 * stuff added
 */
const struct inode_operations reiserfs_symlink_inode_operations = {
	.readlink = generic_readlink,
	.follow_link = page_follow_link_light,
	.put_link = page_put_link,
	.setattr = reiserfs_setattr,
	.setxattr = reiserfs_setxattr,
	.getxattr = reiserfs_getxattr,
	.listxattr = reiserfs_listxattr,
	.removexattr = reiserfs_removexattr,
	.permission = reiserfs_permission,
};

/*
 * special file operations.. just xattr/acl stuff
 */
const struct inode_operations reiserfs_special_inode_operations = {
	.setattr = reiserfs_setattr,
	.setxattr = reiserfs_setxattr,
	.getxattr = reiserfs_getxattr,
	.listxattr = reiserfs_listxattr,
	.removexattr = reiserfs_removexattr,
	.permission = reiserfs_permission,
	.get_acl = reiserfs_get_acl,
	.set_acl = reiserfs_set_acl,
};
