#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"

void print_leaf(struct leaf *l)
{
	int i;
	u32 nr = btrfs_header_nritems(&l->header);
	struct item *item;
	struct extent_item *ei;
	printf("leaf %Lu total ptrs %d free space %d\n",
		btrfs_header_blocknr(&l->header), nr, leaf_free_space(l));
	fflush(stdout);
	for (i = 0 ; i < nr ; i++) {
		item = l->items + i;
		printf("\titem %d key (%Lu %u %Lu) itemoff %d itemsize %d\n",
			i,
			item->key.objectid, item->key.flags, item->key.offset,
			item->offset, item->size);
		fflush(stdout);
		printf("\t\titem data %.*s\n", item->size,
			l->data+item->offset);
		ei = (struct extent_item *)(l->data + item->offset);
		printf("\t\textent data refs %u owner %Lu\n", ei->refs,
			ei->owner);
		fflush(stdout);
	}
}
void print_tree(struct ctree_root *root, struct tree_buffer *t)
{
	int i;
	u32 nr;
	struct node *c;

	if (!t)
		return;
	c = &t->node;
	nr = btrfs_header_nritems(&c->header);
	if (btrfs_is_leaf(c)) {
		print_leaf((struct leaf *)c);
		return;
	}
	printf("node %Lu level %d total ptrs %d free spc %u\n", t->blocknr,
	        btrfs_header_level(&c->header), nr,
		(u32)NODEPTRS_PER_BLOCK - nr);
	fflush(stdout);
	for (i = 0; i < nr; i++) {
		printf("\tkey %d (%Lu %u %Lu) block %Lu\n",
		       i,
		       c->keys[i].objectid, c->keys[i].flags, c->keys[i].offset,
		       c->blockptrs[i]);
		fflush(stdout);
	}
	for (i = 0; i < nr; i++) {
		struct tree_buffer *next_buf = read_tree_block(root,
							    c->blockptrs[i]);
		struct node *next = &next_buf->node;
		if (btrfs_is_leaf(next) &&
		    btrfs_header_level(&c->header) != 1)
			BUG();
		if (btrfs_header_level(&next->header) !=
			btrfs_header_level(&c->header) - 1)
			BUG();
		print_tree(root, next_buf);
		tree_block_release(root, next_buf);
	}

}

