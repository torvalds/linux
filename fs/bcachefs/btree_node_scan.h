/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_NODE_SCAN_H
#define _BCACHEFS_BTREE_NODE_SCAN_H

int bch2_scan_for_btree_nodes(struct bch_fs *);
bool bch2_btree_node_is_stale(struct bch_fs *, struct btree *);
int bch2_btree_has_scanned_nodes(struct bch_fs *, enum btree_id);
int bch2_get_scanned_nodes(struct bch_fs *, enum btree_id, unsigned, struct bpos, struct bpos);
void bch2_find_btree_nodes_exit(struct find_btree_nodes *);

#endif /* _BCACHEFS_BTREE_NODE_SCAN_H */
