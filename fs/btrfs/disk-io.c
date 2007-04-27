#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/radix-tree.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"

struct dev_lookup {
	u64 block_start;
	u64 num_blocks;
	u64 device_id;
	struct block_device *bdev;
};

int btrfs_insert_dev_radix(struct btrfs_root *root,
			   struct block_device *bdev,
			   u64 device_id,
			   u64 block_start,
			   u64 num_blocks)
{
	struct dev_lookup *lookup;
	int ret;

	lookup = kmalloc(sizeof(*lookup), GFP_NOFS);
	if (!lookup)
		return -ENOMEM;
	lookup->block_start = block_start;
	lookup->num_blocks = num_blocks;
	lookup->bdev = bdev;
	lookup->device_id = device_id;

	ret = radix_tree_insert(&root->fs_info->dev_radix, block_start +
				num_blocks - 1, lookup);
	return ret;
}

u64 bh_blocknr(struct buffer_head *bh)
{
	int blkbits = bh->b_page->mapping->host->i_blkbits;
	u64 blocknr = bh->b_page->index << (PAGE_CACHE_SHIFT - blkbits);
	unsigned long offset;

	if (PageHighMem(bh->b_page))
		offset = (unsigned long)bh->b_data;
	else
		offset = bh->b_data - (char *)page_address(bh->b_page);
	blocknr += offset >> (PAGE_CACHE_SHIFT - blkbits);
	return blocknr;
}

static int check_tree_block(struct btrfs_root *root, struct buffer_head *buf)
{
	struct btrfs_node *node = btrfs_buffer_node(buf);
	if (bh_blocknr(buf) != btrfs_header_blocknr(&node->header)) {
		printk(KERN_CRIT "bh_blocknr(buf) is %Lu, header is %Lu\n",
		       bh_blocknr(buf), btrfs_header_blocknr(&node->header));
		BUG();
	}
	return 0;
}

struct buffer_head *btrfs_find_tree_block(struct btrfs_root *root, u64 blocknr)
{
	struct address_space *mapping = root->fs_info->btree_inode->i_mapping;
	int blockbits = root->fs_info->sb->s_blocksize_bits;
	unsigned long index = blocknr >> (PAGE_CACHE_SHIFT - blockbits);
	struct page *page;
	struct buffer_head *bh;
	struct buffer_head *head;
	struct buffer_head *ret = NULL;


	page = find_lock_page(mapping, index);
	if (!page)
		return NULL;

	if (!page_has_buffers(page))
		goto out_unlock;

	head = page_buffers(page);
	bh = head;
	do {
		if (buffer_mapped(bh) && bh_blocknr(bh) == blocknr) {
			ret = bh;
			get_bh(bh);
			goto out_unlock;
		}
		bh = bh->b_this_page;
	} while (bh != head);
out_unlock:
	unlock_page(page);
	page_cache_release(page);
	return ret;
}

int btrfs_map_bh_to_logical(struct btrfs_root *root, struct buffer_head *bh,
			     u64 logical)
{
	struct dev_lookup *lookup[2];

	int ret;

	if (logical == 0) {
		bh->b_bdev = NULL;
		bh->b_blocknr = 0;
		set_buffer_mapped(bh);
		return 0;
	}
	root = root->fs_info->dev_root;
	ret = radix_tree_gang_lookup(&root->fs_info->dev_radix,
				     (void **)lookup,
				     (unsigned long)logical,
				     ARRAY_SIZE(lookup));
	if (ret == 0 || lookup[0]->block_start > logical ||
	    lookup[0]->block_start + lookup[0]->num_blocks <= logical) {
		ret = -ENOENT;
		goto out;
	}
	bh->b_bdev = lookup[0]->bdev;
	bh->b_blocknr = logical - lookup[0]->block_start;
	set_buffer_mapped(bh);
	ret = 0;
out:
	return ret;
}

struct buffer_head *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 blocknr)
{
	struct address_space *mapping = root->fs_info->btree_inode->i_mapping;
	int blockbits = root->fs_info->sb->s_blocksize_bits;
	unsigned long index = blocknr >> (PAGE_CACHE_SHIFT - blockbits);
	struct page *page;
	struct buffer_head *bh;
	struct buffer_head *head;
	struct buffer_head *ret = NULL;
	int err;
	u64 first_block = index << (PAGE_CACHE_SHIFT - blockbits);

	page = grab_cache_page(mapping, index);
	if (!page)
		return NULL;

	if (!page_has_buffers(page))
		create_empty_buffers(page, root->fs_info->sb->s_blocksize, 0);
	head = page_buffers(page);
	bh = head;
	do {
		if (!buffer_mapped(bh)) {
			err = btrfs_map_bh_to_logical(root, bh, first_block);
			BUG_ON(err);
		}
		if (bh_blocknr(bh) == blocknr) {
			ret = bh;
			get_bh(bh);
			goto out_unlock;
		}
		bh = bh->b_this_page;
		first_block++;
	} while (bh != head);
out_unlock:
	unlock_page(page);
	if (ret)
		touch_buffer(ret);
	page_cache_release(page);
	return ret;
}

static int btree_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	int err;
	struct btrfs_root *root = BTRFS_I(bh->b_page->mapping->host)->root;
	err = btrfs_map_bh_to_logical(root, bh, iblock);
	return err;
}

int btrfs_csum_data(struct btrfs_root * root, char *data, size_t len,
		    char *result)
{
	struct scatterlist sg;
	struct crypto_hash *tfm = root->fs_info->hash_tfm;
	struct hash_desc desc;
	int ret;

	desc.tfm = tfm;
	desc.flags = 0;
	sg_init_one(&sg, data, len);
	spin_lock(&root->fs_info->hash_lock);
	ret = crypto_hash_digest(&desc, &sg, 1, result);
	spin_unlock(&root->fs_info->hash_lock);
	if (ret) {
		printk("sha256 digest failed\n");
	}
	return ret;
}
static int csum_tree_block(struct btrfs_root *root, struct buffer_head *bh,
			   int verify)
{
	char result[BTRFS_CSUM_SIZE];
	int ret;
	struct btrfs_node *node;

	ret = btrfs_csum_data(root, bh->b_data + BTRFS_CSUM_SIZE,
			      bh->b_size - BTRFS_CSUM_SIZE, result);
	if (ret)
		return ret;
	if (verify) {
		if (memcmp(bh->b_data, result, BTRFS_CSUM_SIZE)) {
			printk("checksum verify failed on %Lu\n",
			       bh_blocknr(bh));
			return 1;
		}
	} else {
		node = btrfs_buffer_node(bh);
		memcpy(node->header.csum, result, BTRFS_CSUM_SIZE);
	}
	return 0;
}

static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct buffer_head *bh;
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	struct buffer_head *head;
	if (!page_has_buffers(page)) {
		create_empty_buffers(page, root->fs_info->sb->s_blocksize,
					(1 << BH_Dirty)|(1 << BH_Uptodate));
	}
	head = page_buffers(page);
	bh = head;
	do {
		if (buffer_dirty(bh))
			csum_tree_block(root, bh, 0);
		bh = bh->b_this_page;
	} while (bh != head);
	return block_write_full_page(page, btree_get_block, wbc);
}

static int btree_readpage(struct file * file, struct page * page)
{
	return block_read_full_page(page, btree_get_block);
}

static struct address_space_operations btree_aops = {
	.readpage	= btree_readpage,
	.writepage	= btree_writepage,
	.sync_page	= block_sync_page,
};

struct buffer_head *read_tree_block(struct btrfs_root *root, u64 blocknr)
{
	struct buffer_head *bh = NULL;

	bh = btrfs_find_create_tree_block(root, blocknr);
	if (!bh)
		return bh;
	if (buffer_uptodate(bh))
		goto uptodate;
	lock_buffer(bh);
	if (!buffer_uptodate(bh)) {
		get_bh(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(READ, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			goto fail;
		csum_tree_block(root, bh, 1);
	} else {
		unlock_buffer(bh);
	}
uptodate:
	if (check_tree_block(root, bh))
		BUG();
	return bh;
fail:
	brelse(bh);
	return NULL;
}

int dirty_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf)
{
	WARN_ON(atomic_read(&buf->b_count) == 0);
	mark_buffer_dirty(buf);
	return 0;
}

int clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf)
{
	WARN_ON(atomic_read(&buf->b_count) == 0);
	clear_buffer_dirty(buf);
	return 0;
}

static int __setup_root(int blocksize,
			struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid)
{
	root->node = NULL;
	root->inode = NULL;
	root->commit_root = NULL;
	root->blocksize = blocksize;
	root->ref_cows = 0;
	root->fs_info = fs_info;
	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_inode = 0;
	root->last_inode_alloc = 0;
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	root->root_key.objectid = objectid;
	return 0;
}

static int find_and_setup_root(int blocksize,
			       struct btrfs_root *tree_root,
			       struct btrfs_fs_info *fs_info,
			       u64 objectid,
			       struct btrfs_root *root)
{
	int ret;

	__setup_root(blocksize, root, fs_info, objectid);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	BUG_ON(ret);

	root->node = read_tree_block(root,
				     btrfs_root_blocknr(&root->root_item));
	BUG_ON(!root->node);
	return 0;
}

struct btrfs_root *btrfs_read_fs_root(struct btrfs_fs_info *fs_info,
				      struct btrfs_key *location)
{
	struct btrfs_root *root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path;
	struct btrfs_leaf *l;
	u64 highest_inode;
	int ret = 0;

printk("read_fs_root looking for %Lu %Lu %u\n", location->objectid, location->offset, location->flags);
	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)location->objectid);
	if (root) {
printk("found %p in cache\n", root);
		return root;
	}
	root = kmalloc(sizeof(*root), GFP_NOFS);
	if (!root) {
printk("failed1\n");
		return ERR_PTR(-ENOMEM);
	}
	if (location->offset == (u64)-1) {
		ret = find_and_setup_root(fs_info->sb->s_blocksize,
					  fs_info->tree_root, fs_info,
					  location->objectid, root);
		if (ret) {
printk("failed2\n");
			kfree(root);
			return ERR_PTR(ret);
		}
		goto insert;
	}

	__setup_root(fs_info->sb->s_blocksize, root, fs_info,
		     location->objectid);

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(NULL, tree_root, location, path, 0, 0);
	if (ret != 0) {
printk("internal search_slot gives us %d\n", ret);
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}
	l = btrfs_buffer_leaf(path->nodes[0]);
	memcpy(&root->root_item,
	       btrfs_item_ptr(l, path->slots[0], struct btrfs_root_item),
	       sizeof(root->root_item));
	memcpy(&root->root_key, location, sizeof(*location));
	ret = 0;
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	if (ret) {
		kfree(root);
		return ERR_PTR(ret);
	}
	root->node = read_tree_block(root,
				     btrfs_root_blocknr(&root->root_item));
	BUG_ON(!root->node);
insert:
printk("inserting %p\n", root);
	root->ref_cows = 1;
	ret = radix_tree_insert(&fs_info->fs_roots_radix,
				(unsigned long)root->root_key.objectid,
				root);
	if (ret) {
printk("radix_tree_insert gives us %d\n", ret);
		brelse(root->node);
		kfree(root);
		return ERR_PTR(ret);
	}
	ret = btrfs_find_highest_inode(root, &highest_inode);
	if (ret == 0) {
		root->highest_inode = highest_inode;
		root->last_inode_alloc = highest_inode;
printk("highest inode is %Lu\n", highest_inode);
	}
printk("all worked\n");
	return root;
}

static int btrfs_open_disk(struct btrfs_root *root, u64 device_id,
			   u64 block_start, u64 num_blocks,
			   char *filename, int name_len)
{
	char *null_filename;
	struct block_device *bdev;
	int ret;

	null_filename = kmalloc(name_len + 1, GFP_NOFS);
	if (!null_filename)
		return -ENOMEM;
	memcpy(null_filename, filename, name_len);
	null_filename[name_len] = '\0';

	bdev = open_bdev_excl(null_filename, O_RDWR, root->fs_info->sb);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto out;
	}
	set_blocksize(bdev, root->fs_info->sb->s_blocksize);
	ret = btrfs_insert_dev_radix(root, bdev, device_id,
				     block_start, num_blocks);
	BUG_ON(ret);
	ret = 0;
out:
	kfree(null_filename);
	return ret;
}

static int read_device_info(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *leaf;
	struct btrfs_device_item *dev_item;
	int nritems;
	int slot;

	root = root->fs_info->dev_root;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	key.objectid = 0;
	key.offset = 0;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DEV_ITEM_KEY);

	mutex_lock(&root->fs_info->fs_mutex);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	nritems = btrfs_header_nritems(&leaf->header);
	while(1) {
		slot = path->slots[0];
		if (slot >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret)
				break;
			leaf = btrfs_buffer_leaf(path->nodes[0]);
			nritems = btrfs_header_nritems(&leaf->header);
			slot = path->slots[0];
		}
		btrfs_disk_key_to_cpu(&key, &leaf->items[slot].key);
		if (btrfs_key_type(&key) != BTRFS_DEV_ITEM_KEY) {
			path->slots[0]++;
			continue;
		}
		dev_item = btrfs_item_ptr(leaf, slot, struct btrfs_device_item);
printk("found key %Lu %Lu\n", key.objectid, key.offset);
		if (btrfs_device_id(dev_item) !=
		    btrfs_super_device_id(root->fs_info->disk_super)) {
			ret = btrfs_open_disk(root, btrfs_device_id(dev_item),
					      key.objectid, key.offset,
					      (char *)(dev_item + 1),
					      btrfs_device_pathlen(dev_item));
			BUG_ON(ret);
		}
		path->slots[0]++;
	}
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

struct btrfs_root *open_ctree(struct super_block *sb)
{
	struct btrfs_root *extent_root = kmalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *dev_root = kmalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *tree_root = kmalloc(sizeof(struct btrfs_root),
					       GFP_NOFS);
	struct btrfs_fs_info *fs_info = kmalloc(sizeof(*fs_info),
						GFP_NOFS);
	int ret;
	struct btrfs_super_block *disk_super;
	struct dev_lookup *dev_lookup;

	init_bit_radix(&fs_info->pinned_radix);
	init_bit_radix(&fs_info->pending_del_radix);
	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_NOFS);
	INIT_RADIX_TREE(&fs_info->dev_radix, GFP_NOFS);
	INIT_RADIX_TREE(&fs_info->block_group_radix, GFP_KERNEL);
	INIT_LIST_HEAD(&fs_info->trans_list);
	sb_set_blocksize(sb, 4096);
	fs_info->running_transaction = NULL;
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->dev_root = dev_root;
	fs_info->sb = sb;
	fs_info->btree_inode = new_inode(sb);
	fs_info->btree_inode->i_ino = 1;
	fs_info->btree_inode->i_nlink = 1;
	fs_info->btree_inode->i_size = sb->s_bdev->bd_inode->i_size;
	fs_info->btree_inode->i_mapping->a_ops = &btree_aops;
	fs_info->do_barriers = 1;
	fs_info->extent_tree_insert_nr = 0;
	fs_info->extent_tree_prealloc_nr = 0;
	BTRFS_I(fs_info->btree_inode)->root = tree_root;
	memset(&BTRFS_I(fs_info->btree_inode)->location, 0,
	       sizeof(struct btrfs_key));
	insert_inode_hash(fs_info->btree_inode);
	mapping_set_gfp_mask(fs_info->btree_inode->i_mapping, GFP_NOFS);
	fs_info->hash_tfm = crypto_alloc_hash("sha256", 0, CRYPTO_ALG_ASYNC);
	spin_lock_init(&fs_info->hash_lock);
	if (!fs_info->hash_tfm || IS_ERR(fs_info->hash_tfm)) {
		printk("failed to allocate sha256 hash\n");
		return NULL;
	}
	mutex_init(&fs_info->trans_mutex);
	mutex_init(&fs_info->fs_mutex);
	fs_info->block_group_cache = NULL;

	__setup_root(sb->s_blocksize, dev_root,
		     fs_info, BTRFS_DEV_TREE_OBJECTID);

	__setup_root(sb->s_blocksize, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);

	dev_lookup = kmalloc(sizeof(*dev_lookup), GFP_NOFS);
	dev_lookup->block_start = 0;
	dev_lookup->num_blocks = (u32)-2;
	dev_lookup->bdev = sb->s_bdev;
	dev_lookup->device_id = 0;
	ret = radix_tree_insert(&fs_info->dev_radix, (u32)-2, dev_lookup);
	BUG_ON(ret);
	fs_info->sb_buffer = read_tree_block(tree_root,
					     BTRFS_SUPER_INFO_OFFSET /
					     sb->s_blocksize);

	if (!fs_info->sb_buffer)
		return NULL;
	disk_super = (struct btrfs_super_block *)fs_info->sb_buffer->b_data;
	if (!btrfs_super_root(disk_super))
		return NULL;

	i_size_write(fs_info->btree_inode,
		     btrfs_super_total_blocks(disk_super) <<
		     fs_info->btree_inode->i_blkbits);

	radix_tree_delete(&fs_info->dev_radix, (u32)-2);
	dev_lookup->block_start = btrfs_super_device_block_start(disk_super);
	dev_lookup->num_blocks = btrfs_super_device_num_blocks(disk_super);
	dev_lookup->device_id = btrfs_super_device_id(disk_super);

	ret = radix_tree_insert(&fs_info->dev_radix,
				dev_lookup->block_start +
				dev_lookup->num_blocks - 1, dev_lookup);
	BUG_ON(ret);

	fs_info->disk_super = disk_super;

	dev_root->node = read_tree_block(tree_root,
					  btrfs_super_device_root(disk_super));

	ret = read_device_info(dev_root);
	BUG_ON(ret);

	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super));
	BUG_ON(!tree_root->node);

	mutex_lock(&fs_info->fs_mutex);
	ret = find_and_setup_root(sb->s_blocksize, tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root);
	BUG_ON(ret);

	btrfs_read_block_groups(extent_root);

	fs_info->generation = btrfs_super_generation(disk_super) + 1;
	memset(&fs_info->kobj, 0, sizeof(fs_info->kobj));
	kobj_set_kset_s(fs_info, btrfs_subsys);
	kobject_set_name(&fs_info->kobj, "%s", sb->s_id);
	kobject_register(&fs_info->kobj);
	mutex_unlock(&fs_info->fs_mutex);
	return tree_root;
}

int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root)
{
	int ret;
	struct buffer_head *bh = root->fs_info->sb_buffer;

	btrfs_set_super_root(root->fs_info->disk_super,
			     bh_blocknr(root->fs_info->tree_root->node));
	lock_buffer(bh);
	WARN_ON(atomic_read(&bh->b_count) < 1);
	clear_buffer_dirty(bh);
	csum_tree_block(root, bh, 0);
	bh->b_end_io = end_buffer_write_sync;
	get_bh(bh);
	if (root->fs_info->do_barriers)
		ret = submit_bh(WRITE_BARRIER, bh);
	else
		ret = submit_bh(WRITE, bh);
	if (ret == -EOPNOTSUPP) {
		set_buffer_uptodate(bh);
		root->fs_info->do_barriers = 0;
		ret = submit_bh(WRITE, bh);
	}
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		WARN_ON(1);
		return -EIO;
	}
	return 0;
}

static int free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)root->root_key.objectid);
	if (root->inode)
		iput(root->inode);
	if (root->node)
		brelse(root->node);
	if (root->commit_root)
		brelse(root->commit_root);
	kfree(root);
	return 0;
}

int del_fs_roots(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *gang[8];
	int i;

	while(1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, 0,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++)
			free_fs_root(fs_info, gang[i]);
	}
	return 0;
}

static int free_dev_radix(struct btrfs_fs_info *fs_info)
{
	struct dev_lookup *lookup[8];
	struct block_device *super_bdev = fs_info->sb->s_bdev;
	int ret;
	int i;
	while(1) {
		ret = radix_tree_gang_lookup(&fs_info->dev_radix,
					     (void **)lookup, 0,
					     ARRAY_SIZE(lookup));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			if (lookup[i]->bdev != super_bdev)
				close_bdev_excl(lookup[i]->bdev);
			radix_tree_delete(&fs_info->dev_radix,
					  lookup[i]->block_start +
					  lookup[i]->num_blocks - 1);
			kfree(lookup[i]);
		}
	}
	return 0;
}

int close_ctree(struct btrfs_root *root)
{
	int ret;
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = root->fs_info;

	mutex_lock(&fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	/* run commit again to  drop the original snapshot */
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	ret = btrfs_write_and_wait_transaction(NULL, root);
	BUG_ON(ret);
	write_ctree_super(NULL, root);
	mutex_unlock(&fs_info->fs_mutex);

	if (fs_info->extent_root->node)
		btrfs_block_release(fs_info->extent_root,
				    fs_info->extent_root->node);
	if (fs_info->dev_root->node)
		btrfs_block_release(fs_info->dev_root,
				    fs_info->dev_root->node);
	if (fs_info->tree_root->node)
		btrfs_block_release(fs_info->tree_root,
				    fs_info->tree_root->node);
	btrfs_block_release(root, fs_info->sb_buffer);
	crypto_free_hash(fs_info->hash_tfm);
	truncate_inode_pages(fs_info->btree_inode->i_mapping, 0);
	iput(fs_info->btree_inode);

	free_dev_radix(fs_info);
	btrfs_free_block_groups(root->fs_info);
	del_fs_roots(fs_info);
	kfree(fs_info->extent_root);
	kfree(fs_info->tree_root);
	kobject_unregister(&fs_info->kobj);
	return 0;
}

void btrfs_block_release(struct btrfs_root *root, struct buffer_head *buf)
{
	brelse(buf);
}

