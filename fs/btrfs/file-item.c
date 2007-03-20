#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

int btrfs_create_file(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, u64 dirid, u64 *objectid)
{
	return 0;
}
