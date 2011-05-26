/*
 *  linux/fs/hpfs/anode.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling HPFS anode tree that contains file allocation info
 */

#include "hpfs_fn.h"

/* Find a sector in allocation tree */

secno hpfs_bplus_lookup(struct super_block *s, struct inode *inode,
		   struct bplus_header *btree, unsigned sec,
		   struct buffer_head *bh)
{
	anode_secno a = -1;
	struct anode *anode;
	int i;
	int c1, c2 = 0;
	go_down:
	if (hpfs_sb(s)->sb_chk) if (hpfs_stop_cycles(s, a, &c1, &c2, "hpfs_bplus_lookup")) return -1;
	if (btree->internal) {
		for (i = 0; i < btree->n_used_nodes; i++)
			if (le32_to_cpu(btree->u.internal[i].file_secno) > sec) {
				a = le32_to_cpu(btree->u.internal[i].down);
				brelse(bh);
				if (!(anode = hpfs_map_anode(s, a, &bh))) return -1;
				btree = &anode->btree;
				goto go_down;
			}
		hpfs_error(s, "sector %08x not found in internal anode %08x", sec, a);
		brelse(bh);
		return -1;
	}
	for (i = 0; i < btree->n_used_nodes; i++)
		if (le32_to_cpu(btree->u.external[i].file_secno) <= sec &&
		    le32_to_cpu(btree->u.external[i].file_secno) + le32_to_cpu(btree->u.external[i].length) > sec) {
			a = le32_to_cpu(btree->u.external[i].disk_secno) + sec - le32_to_cpu(btree->u.external[i].file_secno);
			if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, a, 1, "data")) {
				brelse(bh);
				return -1;
			}
			if (inode) {
				struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);
				hpfs_inode->i_file_sec = le32_to_cpu(btree->u.external[i].file_secno);
				hpfs_inode->i_disk_sec = le32_to_cpu(btree->u.external[i].disk_secno);
				hpfs_inode->i_n_secs = le32_to_cpu(btree->u.external[i].length);
			}
			brelse(bh);
			return a;
		}
	hpfs_error(s, "sector %08x not found in external anode %08x", sec, a);
	brelse(bh);
	return -1;
}

/* Add a sector to tree */

secno hpfs_add_sector_to_btree(struct super_block *s, secno node, int fnod, unsigned fsecno)
{
	struct bplus_header *btree;
	struct anode *anode = NULL, *ranode = NULL;
	struct fnode *fnode;
	anode_secno a, na = -1, ra, up = -1;
	secno se;
	struct buffer_head *bh, *bh1, *bh2;
	int n;
	unsigned fs;
	int c1, c2 = 0;
	if (fnod) {
		if (!(fnode = hpfs_map_fnode(s, node, &bh))) return -1;
		btree = &fnode->btree;
	} else {
		if (!(anode = hpfs_map_anode(s, node, &bh))) return -1;
		btree = &anode->btree;
	}
	a = node;
	go_down:
	if ((n = btree->n_used_nodes - 1) < -!!fnod) {
		hpfs_error(s, "anode %08x has no entries", a);
		brelse(bh);
		return -1;
	}
	if (btree->internal) {
		a = le32_to_cpu(btree->u.internal[n].down);
		btree->u.internal[n].file_secno = cpu_to_le32(-1);
		mark_buffer_dirty(bh);
		brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, a, &c1, &c2, "hpfs_add_sector_to_btree #1")) return -1;
		if (!(anode = hpfs_map_anode(s, a, &bh))) return -1;
		btree = &anode->btree;
		goto go_down;
	}
	if (n >= 0) {
		if (le32_to_cpu(btree->u.external[n].file_secno) + le32_to_cpu(btree->u.external[n].length) != fsecno) {
			hpfs_error(s, "allocated size %08x, trying to add sector %08x, %cnode %08x",
				le32_to_cpu(btree->u.external[n].file_secno) + le32_to_cpu(btree->u.external[n].length), fsecno,
				fnod?'f':'a', node);
			brelse(bh);
			return -1;
		}
		if (hpfs_alloc_if_possible(s, se = le32_to_cpu(btree->u.external[n].disk_secno) + le32_to_cpu(btree->u.external[n].length))) {
			btree->u.external[n].length = cpu_to_le32(le32_to_cpu(btree->u.external[n].length) + 1);
			mark_buffer_dirty(bh);
			brelse(bh);
			return se;
		}
	} else {
		if (fsecno) {
			hpfs_error(s, "empty file %08x, trying to add sector %08x", node, fsecno);
			brelse(bh);
			return -1;
		}
		se = !fnod ? node : (node + 16384) & ~16383;
	}	
	if (!(se = hpfs_alloc_sector(s, se, 1, fsecno*ALLOC_M>ALLOC_FWD_MAX ? ALLOC_FWD_MAX : fsecno*ALLOC_M<ALLOC_FWD_MIN ? ALLOC_FWD_MIN : fsecno*ALLOC_M))) {
		brelse(bh);
		return -1;
	}
	fs = n < 0 ? 0 : le32_to_cpu(btree->u.external[n].file_secno) + le32_to_cpu(btree->u.external[n].length);
	if (!btree->n_free_nodes) {
		up = a != node ? le32_to_cpu(anode->up) : -1;
		if (!(anode = hpfs_alloc_anode(s, a, &na, &bh1))) {
			brelse(bh);
			hpfs_free_sectors(s, se, 1);
			return -1;
		}
		if (a == node && fnod) {
			anode->up = cpu_to_le32(node);
			anode->btree.fnode_parent = 1;
			anode->btree.n_used_nodes = btree->n_used_nodes;
			anode->btree.first_free = btree->first_free;
			anode->btree.n_free_nodes = 40 - anode->btree.n_used_nodes;
			memcpy(&anode->u, &btree->u, btree->n_used_nodes * 12);
			btree->internal = 1;
			btree->n_free_nodes = 11;
			btree->n_used_nodes = 1;
			btree->first_free = cpu_to_le16((char *)&(btree->u.internal[1]) - (char *)btree);
			btree->u.internal[0].file_secno = cpu_to_le32(-1);
			btree->u.internal[0].down = cpu_to_le32(na);
			mark_buffer_dirty(bh);
		} else if (!(ranode = hpfs_alloc_anode(s, /*a*/0, &ra, &bh2))) {
			brelse(bh);
			brelse(bh1);
			hpfs_free_sectors(s, se, 1);
			hpfs_free_sectors(s, na, 1);
			return -1;
		}
		brelse(bh);
		bh = bh1;
		btree = &anode->btree;
	}
	btree->n_free_nodes--; n = btree->n_used_nodes++;
	btree->first_free = cpu_to_le16(le16_to_cpu(btree->first_free) + 12);
	btree->u.external[n].disk_secno = cpu_to_le32(se);
	btree->u.external[n].file_secno = cpu_to_le32(fs);
	btree->u.external[n].length = cpu_to_le32(1);
	mark_buffer_dirty(bh);
	brelse(bh);
	if ((a == node && fnod) || na == -1) return se;
	c2 = 0;
	while (up != (anode_secno)-1) {
		struct anode *new_anode;
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, up, &c1, &c2, "hpfs_add_sector_to_btree #2")) return -1;
		if (up != node || !fnod) {
			if (!(anode = hpfs_map_anode(s, up, &bh))) return -1;
			btree = &anode->btree;
		} else {
			if (!(fnode = hpfs_map_fnode(s, up, &bh))) return -1;
			btree = &fnode->btree;
		}
		if (btree->n_free_nodes) {
			btree->n_free_nodes--; n = btree->n_used_nodes++;
			btree->first_free = cpu_to_le16(le16_to_cpu(btree->first_free) + 8);
			btree->u.internal[n].file_secno = cpu_to_le32(-1);
			btree->u.internal[n].down = cpu_to_le32(na);
			btree->u.internal[n-1].file_secno = cpu_to_le32(fs);
			mark_buffer_dirty(bh);
			brelse(bh);
			brelse(bh2);
			hpfs_free_sectors(s, ra, 1);
			if ((anode = hpfs_map_anode(s, na, &bh))) {
				anode->up = cpu_to_le32(up);
				anode->btree.fnode_parent = up == node && fnod;
				mark_buffer_dirty(bh);
				brelse(bh);
			}
			return se;
		}
		up = up != node ? le32_to_cpu(anode->up) : -1;
		btree->u.internal[btree->n_used_nodes - 1].file_secno = cpu_to_le32(/*fs*/-1);
		mark_buffer_dirty(bh);
		brelse(bh);
		a = na;
		if ((new_anode = hpfs_alloc_anode(s, a, &na, &bh))) {
			anode = new_anode;
			/*anode->up = cpu_to_le32(up != -1 ? up : ra);*/
			anode->btree.internal = 1;
			anode->btree.n_used_nodes = 1;
			anode->btree.n_free_nodes = 59;
			anode->btree.first_free = cpu_to_le16(16);
			anode->btree.u.internal[0].down = cpu_to_le32(a);
			anode->btree.u.internal[0].file_secno = cpu_to_le32(-1);
			mark_buffer_dirty(bh);
			brelse(bh);
			if ((anode = hpfs_map_anode(s, a, &bh))) {
				anode->up = cpu_to_le32(na);
				mark_buffer_dirty(bh);
				brelse(bh);
			}
		} else na = a;
	}
	if ((anode = hpfs_map_anode(s, na, &bh))) {
		anode->up = cpu_to_le32(node);
		if (fnod) anode->btree.fnode_parent = 1;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	if (!fnod) {
		if (!(anode = hpfs_map_anode(s, node, &bh))) {
			brelse(bh2);
			return -1;
		}
		btree = &anode->btree;
	} else {
		if (!(fnode = hpfs_map_fnode(s, node, &bh))) {
			brelse(bh2);
			return -1;
		}
		btree = &fnode->btree;
	}
	ranode->up = cpu_to_le32(node);
	memcpy(&ranode->btree, btree, le16_to_cpu(btree->first_free));
	if (fnod) ranode->btree.fnode_parent = 1;
	ranode->btree.n_free_nodes = (ranode->btree.internal ? 60 : 40) - ranode->btree.n_used_nodes;
	if (ranode->btree.internal) for (n = 0; n < ranode->btree.n_used_nodes; n++) {
		struct anode *unode;
		if ((unode = hpfs_map_anode(s, le32_to_cpu(ranode->u.internal[n].down), &bh1))) {
			unode->up = cpu_to_le32(ra);
			unode->btree.fnode_parent = 0;
			mark_buffer_dirty(bh1);
			brelse(bh1);
		}
	}
	btree->internal = 1;
	btree->n_free_nodes = fnod ? 10 : 58;
	btree->n_used_nodes = 2;
	btree->first_free = cpu_to_le16((char *)&btree->u.internal[2] - (char *)btree);
	btree->u.internal[0].file_secno = cpu_to_le32(fs);
	btree->u.internal[0].down = cpu_to_le32(ra);
	btree->u.internal[1].file_secno = cpu_to_le32(-1);
	btree->u.internal[1].down = cpu_to_le32(na);
	mark_buffer_dirty(bh);
	brelse(bh);
	mark_buffer_dirty(bh2);
	brelse(bh2);
	return se;
}

/*
 * Remove allocation tree. Recursion would look much nicer but
 * I want to avoid it because it can cause stack overflow.
 */

void hpfs_remove_btree(struct super_block *s, struct bplus_header *btree)
{
	struct bplus_header *btree1 = btree;
	struct anode *anode = NULL;
	anode_secno ano = 0, oano;
	struct buffer_head *bh;
	int level = 0;
	int pos = 0;
	int i;
	int c1, c2 = 0;
	int d1, d2;
	go_down:
	d2 = 0;
	while (btree1->internal) {
		ano = le32_to_cpu(btree1->u.internal[pos].down);
		if (level) brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, ano, &d1, &d2, "hpfs_remove_btree #1"))
				return;
		if (!(anode = hpfs_map_anode(s, ano, &bh))) return;
		btree1 = &anode->btree;
		level++;
		pos = 0;
	}
	for (i = 0; i < btree1->n_used_nodes; i++)
		hpfs_free_sectors(s, le32_to_cpu(btree1->u.external[i].disk_secno), le32_to_cpu(btree1->u.external[i].length));
	go_up:
	if (!level) return;
	brelse(bh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, ano, &c1, &c2, "hpfs_remove_btree #2")) return;
	hpfs_free_sectors(s, ano, 1);
	oano = ano;
	ano = le32_to_cpu(anode->up);
	if (--level) {
		if (!(anode = hpfs_map_anode(s, ano, &bh))) return;
		btree1 = &anode->btree;
	} else btree1 = btree;
	for (i = 0; i < btree1->n_used_nodes; i++) {
		if (le32_to_cpu(btree1->u.internal[i].down) == oano) {
			if ((pos = i + 1) < btree1->n_used_nodes)
				goto go_down;
			else
				goto go_up;
		}
	}
	hpfs_error(s,
		   "reference to anode %08x not found in anode %08x "
		   "(probably bad up pointer)",
		   oano, level ? ano : -1);
	if (level)
		brelse(bh);
}

/* Just a wrapper around hpfs_bplus_lookup .. used for reading eas */

static secno anode_lookup(struct super_block *s, anode_secno a, unsigned sec)
{
	struct anode *anode;
	struct buffer_head *bh;
	if (!(anode = hpfs_map_anode(s, a, &bh))) return -1;
	return hpfs_bplus_lookup(s, NULL, &anode->btree, sec, bh);
}

int hpfs_ea_read(struct super_block *s, secno a, int ano, unsigned pos,
	    unsigned len, char *buf)
{
	struct buffer_head *bh;
	char *data;
	secno sec;
	unsigned l;
	while (len) {
		if (ano) {
			if ((sec = anode_lookup(s, a, pos >> 9)) == -1)
				return -1;
		} else sec = a + (pos >> 9);
		if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, sec, 1, "ea #1")) return -1;
		if (!(data = hpfs_map_sector(s, sec, &bh, (len - 1) >> 9)))
			return -1;
		l = 0x200 - (pos & 0x1ff); if (l > len) l = len;
		memcpy(buf, data + (pos & 0x1ff), l);
		brelse(bh);
		buf += l; pos += l; len -= l;
	}
	return 0;
}

int hpfs_ea_write(struct super_block *s, secno a, int ano, unsigned pos,
	     unsigned len, const char *buf)
{
	struct buffer_head *bh;
	char *data;
	secno sec;
	unsigned l;
	while (len) {
		if (ano) {
			if ((sec = anode_lookup(s, a, pos >> 9)) == -1)
				return -1;
		} else sec = a + (pos >> 9);
		if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, sec, 1, "ea #2")) return -1;
		if (!(data = hpfs_map_sector(s, sec, &bh, (len - 1) >> 9)))
			return -1;
		l = 0x200 - (pos & 0x1ff); if (l > len) l = len;
		memcpy(data + (pos & 0x1ff), buf, l);
		mark_buffer_dirty(bh);
		brelse(bh);
		buf += l; pos += l; len -= l;
	}
	return 0;
}

void hpfs_ea_remove(struct super_block *s, secno a, int ano, unsigned len)
{
	struct anode *anode;
	struct buffer_head *bh;
	if (ano) {
		if (!(anode = hpfs_map_anode(s, a, &bh))) return;
		hpfs_remove_btree(s, &anode->btree);
		brelse(bh);
		hpfs_free_sectors(s, a, 1);
	} else hpfs_free_sectors(s, a, (len + 511) >> 9);
}

/* Truncate allocation tree. Doesn't join anodes - I hope it doesn't matter */

void hpfs_truncate_btree(struct super_block *s, secno f, int fno, unsigned secs)
{
	struct fnode *fnode;
	struct anode *anode;
	struct buffer_head *bh;
	struct bplus_header *btree;
	anode_secno node = f;
	int i, j, nodes;
	int c1, c2 = 0;
	if (fno) {
		if (!(fnode = hpfs_map_fnode(s, f, &bh))) return;
		btree = &fnode->btree;
	} else {
		if (!(anode = hpfs_map_anode(s, f, &bh))) return;
		btree = &anode->btree;
	}
	if (!secs) {
		hpfs_remove_btree(s, btree);
		if (fno) {
			btree->n_free_nodes = 8;
			btree->n_used_nodes = 0;
			btree->first_free = cpu_to_le16(8);
			btree->internal = 0;
			mark_buffer_dirty(bh);
		} else hpfs_free_sectors(s, f, 1);
		brelse(bh);
		return;
	}
	while (btree->internal) {
		nodes = btree->n_used_nodes + btree->n_free_nodes;
		for (i = 0; i < btree->n_used_nodes; i++)
			if (le32_to_cpu(btree->u.internal[i].file_secno) >= secs) goto f;
		brelse(bh);
		hpfs_error(s, "internal btree %08x doesn't end with -1", node);
		return;
		f:
		for (j = i + 1; j < btree->n_used_nodes; j++)
			hpfs_ea_remove(s, le32_to_cpu(btree->u.internal[j].down), 1, 0);
		btree->n_used_nodes = i + 1;
		btree->n_free_nodes = nodes - btree->n_used_nodes;
		btree->first_free = cpu_to_le16(8 + 8 * btree->n_used_nodes);
		mark_buffer_dirty(bh);
		if (btree->u.internal[i].file_secno == cpu_to_le32(secs)) {
			brelse(bh);
			return;
		}
		node = le32_to_cpu(btree->u.internal[i].down);
		brelse(bh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, node, &c1, &c2, "hpfs_truncate_btree"))
				return;
		if (!(anode = hpfs_map_anode(s, node, &bh))) return;
		btree = &anode->btree;
	}	
	nodes = btree->n_used_nodes + btree->n_free_nodes;
	for (i = 0; i < btree->n_used_nodes; i++)
		if (le32_to_cpu(btree->u.external[i].file_secno) + le32_to_cpu(btree->u.external[i].length) >= secs) goto ff;
	brelse(bh);
	return;
	ff:
	if (secs <= le32_to_cpu(btree->u.external[i].file_secno)) {
		hpfs_error(s, "there is an allocation error in file %08x, sector %08x", f, secs);
		if (i) i--;
	}
	else if (le32_to_cpu(btree->u.external[i].file_secno) + le32_to_cpu(btree->u.external[i].length) > secs) {
		hpfs_free_sectors(s, le32_to_cpu(btree->u.external[i].disk_secno) + secs -
			le32_to_cpu(btree->u.external[i].file_secno), le32_to_cpu(btree->u.external[i].length)
			- secs + le32_to_cpu(btree->u.external[i].file_secno)); /* I hope gcc optimizes this :-) */
		btree->u.external[i].length = cpu_to_le32(secs - le32_to_cpu(btree->u.external[i].file_secno));
	}
	for (j = i + 1; j < btree->n_used_nodes; j++)
		hpfs_free_sectors(s, le32_to_cpu(btree->u.external[j].disk_secno), le32_to_cpu(btree->u.external[j].length));
	btree->n_used_nodes = i + 1;
	btree->n_free_nodes = nodes - btree->n_used_nodes;
	btree->first_free = cpu_to_le16(8 + 12 * btree->n_used_nodes);
	mark_buffer_dirty(bh);
	brelse(bh);
}

/* Remove file or directory and it's eas - note that directory must
   be empty when this is called. */

void hpfs_remove_fnode(struct super_block *s, fnode_secno fno)
{
	struct buffer_head *bh;
	struct fnode *fnode;
	struct extended_attribute *ea;
	struct extended_attribute *ea_end;
	if (!(fnode = hpfs_map_fnode(s, fno, &bh))) return;
	if (!fnode->dirflag) hpfs_remove_btree(s, &fnode->btree);
	else hpfs_remove_dtree(s, le32_to_cpu(fnode->u.external[0].disk_secno));
	ea_end = fnode_end_ea(fnode);
	for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
		if (ea->indirect)
			hpfs_ea_remove(s, ea_sec(ea), ea->anode, ea_len(ea));
	hpfs_ea_ext_remove(s, le32_to_cpu(fnode->ea_secno), fnode->ea_anode, le32_to_cpu(fnode->ea_size_l));
	brelse(bh);
	hpfs_free_sectors(s, fno, 1);
}
