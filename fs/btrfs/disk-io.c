#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

static int check_tree_block(struct btrfs_root *root, struct buffer_head *buf)
{
	struct btrfs_node *node = btrfs_buffer_node(buf);
	if (buf->b_blocknr != btrfs_header_blocknr(&node->header)) {
		BUG();
	}
	if (root->node && btrfs_header_parentid(&node->header) !=
	    btrfs_header_parentid(btrfs_buffer_header(root->node))) {
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
		if (buffer_mapped(bh) && bh->b_blocknr == blocknr) {
			ret = bh;
			get_bh(bh);
			goto out_unlock;
		}
		bh = bh->b_this_page;
	} while (bh != head);
out_unlock:
	unlock_page(page);
	if (ret) {
		touch_buffer(ret);
	}
	page_cache_release(page);
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
			bh->b_bdev = root->fs_info->sb->s_bdev;
			bh->b_blocknr = first_block;
			set_buffer_mapped(bh);
		}
		if (bh->b_blocknr == blocknr) {
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

static sector_t max_block(struct block_device *bdev)
{
	sector_t retval = ~((sector_t)0);
	loff_t sz = i_size_read(bdev->bd_inode);

	if (sz) {
		unsigned int size = block_size(bdev);
		unsigned int sizebits = blksize_bits(size);
		retval = (sz >> sizebits);
	}
	return retval;
}

static int btree_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh, int create)
{
	if (iblock >= max_block(inode->i_sb->s_bdev)) {
		if (create)
			return -EIO;

		/*
		 * for reads, we're just trying to fill a partial page.
		 * return a hole, they will have to call get_block again
		 * before they can fill it, and they will get -EIO at that
		 * time
		 */
		return 0;
	}
	bh->b_bdev = inode->i_sb->s_bdev;
	bh->b_blocknr = iblock;
	set_buffer_mapped(bh);
	return 0;
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
			printk("checksum verify failed on %lu\n",
			       bh->b_blocknr);
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
	struct btrfs_root *root = btrfs_sb(page->mapping->host->i_sb);
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
	root->commit_root = NULL;
	root->blocksize = blocksize;
	root->ref_cows = 0;
	root->fs_info = fs_info;
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
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

struct btrfs_root *open_ctree(struct super_block *sb)
{
	struct btrfs_root *root = kmalloc(sizeof(struct btrfs_root),
					  GFP_NOFS);
	struct btrfs_root *extent_root = kmalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *tree_root = kmalloc(sizeof(struct btrfs_root),
					       GFP_NOFS);
	struct btrfs_root *inode_root = kmalloc(sizeof(struct btrfs_root),
						GFP_NOFS);
	struct btrfs_fs_info *fs_info = kmalloc(sizeof(*fs_info),
						GFP_NOFS);
	int ret;
	struct btrfs_super_block *disk_super;

	init_bit_radix(&fs_info->pinned_radix);
	init_bit_radix(&fs_info->pending_del_radix);
	sb_set_blocksize(sb, 4096);
	fs_info->running_transaction = NULL;
	fs_info->fs_root = root;
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->inode_root = inode_root;
	fs_info->last_inode_alloc = 0;
	fs_info->sb = sb;
	fs_info->btree_inode = new_inode(sb);
	fs_info->btree_inode->i_ino = 1;
	fs_info->btree_inode->i_nlink = 1;
	fs_info->btree_inode->i_size = sb->s_bdev->bd_inode->i_size;
	fs_info->btree_inode->i_mapping->a_ops = &btree_aops;
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
	memset(&fs_info->current_insert, 0, sizeof(fs_info->current_insert));
	memset(&fs_info->last_insert, 0, sizeof(fs_info->last_insert));

	__setup_root(sb->s_blocksize, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);
	fs_info->sb_buffer = read_tree_block(tree_root,
					     BTRFS_SUPER_INFO_OFFSET /
					     sb->s_blocksize);

	if (!fs_info->sb_buffer) {
printk("failed2\n");
		return NULL;
	}
	disk_super = (struct btrfs_super_block *)fs_info->sb_buffer->b_data;
	if (!btrfs_super_root(disk_super)) {
		return NULL;
	}
	fs_info->disk_super = disk_super;
	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super));
	BUG_ON(!tree_root->node);

	mutex_lock(&fs_info->fs_mutex);
	ret = find_and_setup_root(sb->s_blocksize, tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root);
	BUG_ON(ret);

	ret = find_and_setup_root(sb->s_blocksize, tree_root, fs_info,
				  BTRFS_INODE_MAP_OBJECTID, inode_root);
	BUG_ON(ret);

	ret = find_and_setup_root(sb->s_blocksize, tree_root, fs_info,
				  BTRFS_FS_TREE_OBJECTID, root);
	mutex_unlock(&fs_info->fs_mutex);
	BUG_ON(ret);
	root->commit_root = root->node;
	get_bh(root->node);
	root->ref_cows = 1;
	root->fs_info->generation = root->root_key.offset + 1;
	return root;
}

int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root)
{
	struct buffer_head *bh = root->fs_info->sb_buffer;

	btrfs_set_super_root(root->fs_info->disk_super,
			     root->fs_info->tree_root->node->b_blocknr);
	lock_buffer(bh);
	WARN_ON(atomic_read(&bh->b_count) < 1);
	clear_buffer_dirty(bh);
	csum_tree_block(root, bh, 0);
	bh->b_end_io = end_buffer_write_sync;
	get_bh(bh);
	submit_bh(WRITE, bh);
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		WARN_ON(1);
		return -EIO;
	}
	return 0;
}

int close_ctree(struct btrfs_root *root)
{
	int ret;
	struct btrfs_trans_handle *trans;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	/* run commit again to  drop the original snapshot */
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	ret = btrfs_write_and_wait_transaction(NULL, root);
	BUG_ON(ret);
	write_ctree_super(NULL, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	if (root->node)
		btrfs_block_release(root, root->node);
	if (root->fs_info->extent_root->node)
		btrfs_block_release(root->fs_info->extent_root,
				    root->fs_info->extent_root->node);
	if (root->fs_info->inode_root->node)
		btrfs_block_release(root->fs_info->inode_root,
				    root->fs_info->inode_root->node);
	if (root->fs_info->tree_root->node)
		btrfs_block_release(root->fs_info->tree_root,
				    root->fs_info->tree_root->node);
	btrfs_block_release(root, root->commit_root);
	btrfs_block_release(root, root->fs_info->sb_buffer);
	crypto_free_hash(root->fs_info->hash_tfm);
	truncate_inode_pages(root->fs_info->btree_inode->i_mapping, 0);
	iput(root->fs_info->btree_inode);
	kfree(root->fs_info->extent_root);
	kfree(root->fs_info->inode_root);
	kfree(root->fs_info->tree_root);
	kfree(root->fs_info);
	kfree(root);
	return 0;
}

void btrfs_block_release(struct btrfs_root *root, struct buffer_head *buf)
{
	brelse(buf);
}

