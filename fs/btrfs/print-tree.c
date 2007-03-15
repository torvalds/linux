#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"

void btrfs_print_leaf(struct btrfs_root *root, struct btrfs_leaf *l)
{
	int i;
	u32 nr = btrfs_header_nritems(&l->header);
	struct btrfs_item *item;
	struct btrfs_extent_item *ei;
	struct btrfs_root_item *ri;
	struct btrfs_dir_item *di;
	u32 type;
	u32 namelen;

	printf("leaf %Lu total ptrs %d free space %d\n",
		btrfs_header_blocknr(&l->header), nr,
		btrfs_leaf_free_space(root, l));
	fflush(stdout);
	for (i = 0 ; i < nr ; i++) {
		item = l->items + i;
		type = btrfs_disk_key_type(&item->key);
		printf("\titem %d key (%Lu %Lu %u) itemoff %d itemsize %d\n",
			i,
			btrfs_disk_key_objectid(&item->key),
			btrfs_disk_key_offset(&item->key),
			btrfs_disk_key_flags(&item->key),
			btrfs_item_offset(item),
			btrfs_item_size(item));
		switch (type) {
		case BTRFS_INODE_ITEM_KEY:
			break;
		case BTRFS_DIR_ITEM_KEY:
			namelen = btrfs_item_size(l->items + i) - sizeof(*di);
			di = btrfs_item_ptr(l, i, struct btrfs_dir_item);
			printf("\t\tdir oid %Lu flags %u type %u\n",
				btrfs_dir_objectid(di),
				btrfs_dir_flags(di),
				btrfs_dir_type(di));
			printf("\t\tname %.*s\n",
				namelen, (char *)(di + 1));

			break;
		case BTRFS_ROOT_ITEM_KEY:
			ri = btrfs_item_ptr(l, i, struct btrfs_root_item);
			printf("\t\troot data blocknr %Lu refs %u\n",
				btrfs_root_blocknr(ri), btrfs_root_refs(ri));
			break;
		case BTRFS_EXTENT_ITEM_KEY:
			ei = btrfs_item_ptr(l, i, struct btrfs_extent_item);
			printf("\t\textent data refs %u owner %Lu\n",
				btrfs_extent_refs(ei), btrfs_extent_owner(ei));
			break;
		case BTRFS_STRING_ITEM_KEY:
			printf("\t\titem data %.*s\n", btrfs_item_size(item),
				btrfs_leaf_data(l) + btrfs_item_offset(item));
			break;
		};
		fflush(stdout);
	}
}
void btrfs_print_tree(struct btrfs_root *root, struct btrfs_buffer *t)
{
	int i;
	u32 nr;
	struct btrfs_node *c;

	if (!t)
		return;
	c = &t->node;
	nr = btrfs_header_nritems(&c->header);
	if (btrfs_is_leaf(c)) {
		btrfs_print_leaf(root, (struct btrfs_leaf *)c);
		return;
	}
	printf("node %Lu level %d total ptrs %d free spc %u\n", t->blocknr,
	        btrfs_header_level(&c->header), nr,
		(u32)BTRFS_NODEPTRS_PER_BLOCK(root) - nr);
	fflush(stdout);
	for (i = 0; i < nr; i++) {
		printf("\tkey %d (%Lu %Lu %u) block %Lu\n",
		       i,
		       c->ptrs[i].key.objectid,
		       c->ptrs[i].key.offset,
		       c->ptrs[i].key.flags,
		       btrfs_node_blockptr(c, i));
		fflush(stdout);
	}
	for (i = 0; i < nr; i++) {
		struct btrfs_buffer *next_buf = read_tree_block(root,
						btrfs_node_blockptr(c, i));
		struct btrfs_node *next = &next_buf->node;
		if (btrfs_is_leaf(next) &&
		    btrfs_header_level(&c->header) != 1)
			BUG();
		if (btrfs_header_level(&next->header) !=
			btrfs_header_level(&c->header) - 1)
			BUG();
		btrfs_print_tree(root, next_buf);
		btrfs_block_release(root, next_buf);
	}
}

