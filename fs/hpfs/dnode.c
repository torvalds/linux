/*
 *  linux/fs/hpfs/dnode.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling directory dnode tree - adding, deleteing & searching for dirents
 */

#include "hpfs_fn.h"

static loff_t get_pos(struct dnode *d, struct hpfs_dirent *fde)
{
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end = dnode_end_de(d);
	int i = 1;
	for (de = dnode_first_de(d); de < de_end; de = de_next_de(de)) {
		if (de == fde) return ((loff_t) d->self << 4) | (loff_t)i;
		i++;
	}
	printk("HPFS: get_pos: not_found\n");
	return ((loff_t)d->self << 4) | (loff_t)1;
}

void hpfs_add_pos(struct inode *inode, loff_t *pos)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);
	int i = 0;
	loff_t **ppos;

	if (hpfs_inode->i_rddir_off)
		for (; hpfs_inode->i_rddir_off[i]; i++)
			if (hpfs_inode->i_rddir_off[i] == pos) return;
	if (!(i&0x0f)) {
		if (!(ppos = kmalloc((i+0x11) * sizeof(loff_t*), GFP_NOFS))) {
			printk("HPFS: out of memory for position list\n");
			return;
		}
		if (hpfs_inode->i_rddir_off) {
			memcpy(ppos, hpfs_inode->i_rddir_off, i * sizeof(loff_t));
			kfree(hpfs_inode->i_rddir_off);
		}
		hpfs_inode->i_rddir_off = ppos;
	}
	hpfs_inode->i_rddir_off[i] = pos;
	hpfs_inode->i_rddir_off[i + 1] = NULL;
}

void hpfs_del_pos(struct inode *inode, loff_t *pos)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);
	loff_t **i, **j;

	if (!hpfs_inode->i_rddir_off) goto not_f;
	for (i = hpfs_inode->i_rddir_off; *i; i++) if (*i == pos) goto fnd;
	goto not_f;
	fnd:
	for (j = i + 1; *j; j++) ;
	*i = *(j - 1);
	*(j - 1) = NULL;
	if (j - 1 == hpfs_inode->i_rddir_off) {
		kfree(hpfs_inode->i_rddir_off);
		hpfs_inode->i_rddir_off = NULL;
	}
	return;
	not_f:
	/*printk("HPFS: warning: position pointer %p->%08x not found\n", pos, (int)*pos);*/
	return;
}

static void for_all_poss(struct inode *inode, void (*f)(loff_t *, loff_t, loff_t),
			 loff_t p1, loff_t p2)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);
	loff_t **i;

	if (!hpfs_inode->i_rddir_off) return;
	for (i = hpfs_inode->i_rddir_off; *i; i++) (*f)(*i, p1, p2);
	return;
}

static void hpfs_pos_subst(loff_t *p, loff_t f, loff_t t)
{
	if (*p == f) *p = t;
}

/*void hpfs_hpfs_pos_substd(loff_t *p, loff_t f, loff_t t)
{
	if ((*p & ~0x3f) == (f & ~0x3f)) *p = (t & ~0x3f) | (*p & 0x3f);
}*/

static void hpfs_pos_ins(loff_t *p, loff_t d, loff_t c)
{
	if ((*p & ~0x3f) == (d & ~0x3f) && (*p & 0x3f) >= (d & 0x3f)) {
		int n = (*p & 0x3f) + c;
		if (n > 0x3f) printk("HPFS: hpfs_pos_ins: %08x + %d\n", (int)*p, (int)c >> 8);
		else *p = (*p & ~0x3f) | n;
	}
}

static void hpfs_pos_del(loff_t *p, loff_t d, loff_t c)
{
	if ((*p & ~0x3f) == (d & ~0x3f) && (*p & 0x3f) >= (d & 0x3f)) {
		int n = (*p & 0x3f) - c;
		if (n < 1) printk("HPFS: hpfs_pos_ins: %08x - %d\n", (int)*p, (int)c >> 8);
		else *p = (*p & ~0x3f) | n;
	}
}

static struct hpfs_dirent *dnode_pre_last_de(struct dnode *d)
{
	struct hpfs_dirent *de, *de_end, *dee = NULL, *deee = NULL;
	de_end = dnode_end_de(d);
	for (de = dnode_first_de(d); de < de_end; de = de_next_de(de)) {
		deee = dee; dee = de;
	}	
	return deee;
}

static struct hpfs_dirent *dnode_last_de(struct dnode *d)
{
	struct hpfs_dirent *de, *de_end, *dee = NULL;
	de_end = dnode_end_de(d);
	for (de = dnode_first_de(d); de < de_end; de = de_next_de(de)) {
		dee = de;
	}	
	return dee;
}

static void set_last_pointer(struct super_block *s, struct dnode *d, dnode_secno ptr)
{
	struct hpfs_dirent *de;
	if (!(de = dnode_last_de(d))) {
		hpfs_error(s, "set_last_pointer: empty dnode %08x", d->self);
		return;
	}
	if (hpfs_sb(s)->sb_chk) {
		if (de->down) {
			hpfs_error(s, "set_last_pointer: dnode %08x has already last pointer %08x",
				d->self, de_down_pointer(de));
			return;
		}
		if (de->length != 32) {
			hpfs_error(s, "set_last_pointer: bad last dirent in dnode %08x", d->self);
			return;
		}
	}
	if (ptr) {
		if ((d->first_free += 4) > 2048) {
			hpfs_error(s,"set_last_pointer: too long dnode %08x", d->self);
			d->first_free -= 4;
			return;
		}
		de->length = 36;
		de->down = 1;
		*(dnode_secno *)((char *)de + 32) = ptr;
	}
}

/* Add an entry to dnode and don't care if it grows over 2048 bytes */

struct hpfs_dirent *hpfs_add_de(struct super_block *s, struct dnode *d, unsigned char *name,
				unsigned namelen, secno down_ptr)
{
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end = dnode_end_de(d);
	unsigned d_size = de_size(namelen, down_ptr);
	for (de = dnode_first_de(d); de < de_end; de = de_next_de(de)) {
		int c = hpfs_compare_names(s, name, namelen, de->name, de->namelen, de->last);
		if (!c) {
			hpfs_error(s, "name (%c,%d) already exists in dnode %08x", *name, namelen, d->self);
			return NULL;
		}
		if (c < 0) break;
	}
	memmove((char *)de + d_size, de, (char *)de_end - (char *)de);
	memset(de, 0, d_size);
	if (down_ptr) {
		*(int *)((char *)de + d_size - 4) = down_ptr;
		de->down = 1;
	}
	de->length = d_size;
	if (down_ptr) de->down = 1;
	de->not_8x3 = hpfs_is_name_long(name, namelen);
	de->namelen = namelen;
	memcpy(de->name, name, namelen);
	d->first_free += d_size;
	return de;
}

/* Delete dirent and don't care about its subtree */

static void hpfs_delete_de(struct super_block *s, struct dnode *d,
			   struct hpfs_dirent *de)
{
	if (de->last) {
		hpfs_error(s, "attempt to delete last dirent in dnode %08x", d->self);
		return;
	}
	d->first_free -= de->length;
	memmove(de, de_next_de(de), d->first_free + (char *)d - (char *)de);
}

static void fix_up_ptrs(struct super_block *s, struct dnode *d)
{
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end = dnode_end_de(d);
	dnode_secno dno = d->self;
	for (de = dnode_first_de(d); de < de_end; de = de_next_de(de))
		if (de->down) {
			struct quad_buffer_head qbh;
			struct dnode *dd;
			if ((dd = hpfs_map_dnode(s, de_down_pointer(de), &qbh))) {
				if (dd->up != dno || dd->root_dnode) {
					dd->up = dno;
					dd->root_dnode = 0;
					hpfs_mark_4buffers_dirty(&qbh);
				}
				hpfs_brelse4(&qbh);
			}
		}
}

/* Add an entry to dnode and do dnode splitting if required */

static int hpfs_add_to_dnode(struct inode *i, dnode_secno dno,
			     unsigned char *name, unsigned namelen,
			     struct hpfs_dirent *new_de, dnode_secno down_ptr)
{
	struct quad_buffer_head qbh, qbh1, qbh2;
	struct dnode *d, *ad, *rd, *nd = NULL;
	dnode_secno adno, rdno;
	struct hpfs_dirent *de;
	struct hpfs_dirent nde;
	char *nname;
	int h;
	int pos;
	struct buffer_head *bh;
	struct fnode *fnode;
	int c1, c2 = 0;
	if (!(nname = kmalloc(256, GFP_NOFS))) {
		printk("HPFS: out of memory, can't add to dnode\n");
		return 1;
	}
	go_up:
	if (namelen >= 256) {
		hpfs_error(i->i_sb, "hpfs_add_to_dnode: namelen == %d", namelen);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	if (!(d = hpfs_map_dnode(i->i_sb, dno, &qbh))) {
		kfree(nd);
		kfree(nname);
		return 1;
	}
	go_up_a:
	if (hpfs_sb(i->i_sb)->sb_chk)
		if (hpfs_stop_cycles(i->i_sb, dno, &c1, &c2, "hpfs_add_to_dnode")) {
			hpfs_brelse4(&qbh);
			kfree(nd);
			kfree(nname);
			return 1;
		}
	if (d->first_free + de_size(namelen, down_ptr) <= 2048) {
		loff_t t;
		copy_de(de=hpfs_add_de(i->i_sb, d, name, namelen, down_ptr), new_de);
		t = get_pos(d, de);
		for_all_poss(i, hpfs_pos_ins, t, 1);
		for_all_poss(i, hpfs_pos_subst, 4, t);
		for_all_poss(i, hpfs_pos_subst, 5, t + 1);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		kfree(nd);
		kfree(nname);
		return 0;
	}
	if (!nd) if (!(nd = kmalloc(0x924, GFP_NOFS))) {
		/* 0x924 is a max size of dnode after adding a dirent with
		   max name length. We alloc this only once. There must
		   not be any error while splitting dnodes, otherwise the
		   whole directory, not only file we're adding, would
		   be lost. */
		printk("HPFS: out of memory for dnode splitting\n");
		hpfs_brelse4(&qbh);
		kfree(nname);
		return 1;
	}	
	memcpy(nd, d, d->first_free);
	copy_de(de = hpfs_add_de(i->i_sb, nd, name, namelen, down_ptr), new_de);
	for_all_poss(i, hpfs_pos_ins, get_pos(nd, de), 1);
	h = ((char *)dnode_last_de(nd) - (char *)nd) / 2 + 10;
	if (!(ad = hpfs_alloc_dnode(i->i_sb, d->up, &adno, &qbh1, 0))) {
		hpfs_error(i->i_sb, "unable to alloc dnode - dnode tree will be corrupted");
		hpfs_brelse4(&qbh);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	i->i_size += 2048;
	i->i_blocks += 4;
	pos = 1;
	for (de = dnode_first_de(nd); (char *)de_next_de(de) - (char *)nd < h; de = de_next_de(de)) {
		copy_de(hpfs_add_de(i->i_sb, ad, de->name, de->namelen, de->down ? de_down_pointer(de) : 0), de);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)dno << 4) | pos, ((loff_t)adno << 4) | pos);
		pos++;
	}
	copy_de(new_de = &nde, de);
	memcpy(name = nname, de->name, namelen = de->namelen);
	for_all_poss(i, hpfs_pos_subst, ((loff_t)dno << 4) | pos, 4);
	down_ptr = adno;
	set_last_pointer(i->i_sb, ad, de->down ? de_down_pointer(de) : 0);
	de = de_next_de(de);
	memmove((char *)nd + 20, de, nd->first_free + (char *)nd - (char *)de);
	nd->first_free -= (char *)de - (char *)nd - 20;
	memcpy(d, nd, nd->first_free);
	for_all_poss(i, hpfs_pos_del, (loff_t)dno << 4, pos);
	fix_up_ptrs(i->i_sb, ad);
	if (!d->root_dnode) {
		dno = ad->up = d->up;
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		hpfs_mark_4buffers_dirty(&qbh1);
		hpfs_brelse4(&qbh1);
		goto go_up;
	}
	if (!(rd = hpfs_alloc_dnode(i->i_sb, d->up, &rdno, &qbh2, 0))) {
		hpfs_error(i->i_sb, "unable to alloc dnode - dnode tree will be corrupted");
		hpfs_brelse4(&qbh);
		hpfs_brelse4(&qbh1);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	i->i_size += 2048;
	i->i_blocks += 4;
	rd->root_dnode = 1;
	rd->up = d->up;
	if (!(fnode = hpfs_map_fnode(i->i_sb, d->up, &bh))) {
		hpfs_free_dnode(i->i_sb, rdno);
		hpfs_brelse4(&qbh);
		hpfs_brelse4(&qbh1);
		hpfs_brelse4(&qbh2);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	fnode->u.external[0].disk_secno = rdno;
	mark_buffer_dirty(bh);
	brelse(bh);
	d->up = ad->up = hpfs_i(i)->i_dno = rdno;
	d->root_dnode = ad->root_dnode = 0;
	hpfs_mark_4buffers_dirty(&qbh);
	hpfs_brelse4(&qbh);
	hpfs_mark_4buffers_dirty(&qbh1);
	hpfs_brelse4(&qbh1);
	qbh = qbh2;
	set_last_pointer(i->i_sb, rd, dno);
	dno = rdno;
	d = rd;
	goto go_up_a;
}

/*
 * Add an entry to directory btree.
 * I hate such crazy directory structure.
 * It's easy to read but terrible to write.
 * I wrote this directory code 4 times.
 * I hope, now it's finally bug-free.
 */

int hpfs_add_dirent(struct inode *i, unsigned char *name, unsigned namelen,
		    struct hpfs_dirent *new_de, int cdepth)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	struct dnode *d;
	struct hpfs_dirent *de, *de_end;
	struct quad_buffer_head qbh;
	dnode_secno dno;
	int c;
	int c1, c2 = 0;
	dno = hpfs_inode->i_dno;
	down:
	if (hpfs_sb(i->i_sb)->sb_chk)
		if (hpfs_stop_cycles(i->i_sb, dno, &c1, &c2, "hpfs_add_dirent")) return 1;
	if (!(d = hpfs_map_dnode(i->i_sb, dno, &qbh))) return 1;
	de_end = dnode_end_de(d);
	for (de = dnode_first_de(d); de < de_end; de = de_next_de(de)) {
		if (!(c = hpfs_compare_names(i->i_sb, name, namelen, de->name, de->namelen, de->last))) {
			hpfs_brelse4(&qbh);
			return -1;
		}	
		if (c < 0) {
			if (de->down) {
				dno = de_down_pointer(de);
				hpfs_brelse4(&qbh);
				goto down;
			}
			break;
		}
	}
	hpfs_brelse4(&qbh);
	if (!cdepth) hpfs_lock_creation(i->i_sb);
	if (hpfs_check_free_dnodes(i->i_sb, FREE_DNODES_ADD)) {
		c = 1;
		goto ret;
	}	
	i->i_version++;
	c = hpfs_add_to_dnode(i, dno, name, namelen, new_de, 0);
	ret:
	if (!cdepth) hpfs_unlock_creation(i->i_sb);
	return c;
}

/* 
 * Find dirent with higher name in 'from' subtree and move it to 'to' dnode.
 * Return the dnode we moved from (to be checked later if it's empty)
 */

static secno move_to_top(struct inode *i, dnode_secno from, dnode_secno to)
{
	dnode_secno dno, ddno;
	dnode_secno chk_up = to;
	struct dnode *dnode;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de, *nde;
	int a;
	loff_t t;
	int c1, c2 = 0;
	dno = from;
	while (1) {
		if (hpfs_sb(i->i_sb)->sb_chk)
			if (hpfs_stop_cycles(i->i_sb, dno, &c1, &c2, "move_to_top"))
				return 0;
		if (!(dnode = hpfs_map_dnode(i->i_sb, dno, &qbh))) return 0;
		if (hpfs_sb(i->i_sb)->sb_chk) {
			if (dnode->up != chk_up) {
				hpfs_error(i->i_sb, "move_to_top: up pointer from %08x should be %08x, is %08x",
					dno, chk_up, dnode->up);
				hpfs_brelse4(&qbh);
				return 0;
			}
			chk_up = dno;
		}
		if (!(de = dnode_last_de(dnode))) {
			hpfs_error(i->i_sb, "move_to_top: dnode %08x has no last de", dno);
			hpfs_brelse4(&qbh);
			return 0;
		}
		if (!de->down) break;
		dno = de_down_pointer(de);
		hpfs_brelse4(&qbh);
	}
	while (!(de = dnode_pre_last_de(dnode))) {
		dnode_secno up = dnode->up;
		hpfs_brelse4(&qbh);
		hpfs_free_dnode(i->i_sb, dno);
		i->i_size -= 2048;
		i->i_blocks -= 4;
		for_all_poss(i, hpfs_pos_subst, ((loff_t)dno << 4) | 1, 5);
		if (up == to) return to;
		if (!(dnode = hpfs_map_dnode(i->i_sb, up, &qbh))) return 0;
		if (dnode->root_dnode) {
			hpfs_error(i->i_sb, "move_to_top: got to root_dnode while moving from %08x to %08x", from, to);
			hpfs_brelse4(&qbh);
			return 0;
		}
		de = dnode_last_de(dnode);
		if (!de || !de->down) {
			hpfs_error(i->i_sb, "move_to_top: dnode %08x doesn't point down to %08x", up, dno);
			hpfs_brelse4(&qbh);
			return 0;
		}
		dnode->first_free -= 4;
		de->length -= 4;
		de->down = 0;
		hpfs_mark_4buffers_dirty(&qbh);
		dno = up;
	}
	t = get_pos(dnode, de);
	for_all_poss(i, hpfs_pos_subst, t, 4);
	for_all_poss(i, hpfs_pos_subst, t + 1, 5);
	if (!(nde = kmalloc(de->length, GFP_NOFS))) {
		hpfs_error(i->i_sb, "out of memory for dirent - directory will be corrupted");
		hpfs_brelse4(&qbh);
		return 0;
	}
	memcpy(nde, de, de->length);
	ddno = de->down ? de_down_pointer(de) : 0;
	hpfs_delete_de(i->i_sb, dnode, de);
	set_last_pointer(i->i_sb, dnode, ddno);
	hpfs_mark_4buffers_dirty(&qbh);
	hpfs_brelse4(&qbh);
	a = hpfs_add_to_dnode(i, to, nde->name, nde->namelen, nde, from);
	kfree(nde);
	if (a) return 0;
	return dno;
}

/* 
 * Check if a dnode is empty and delete it from the tree
 * (chkdsk doesn't like empty dnodes)
 */

static void delete_empty_dnode(struct inode *i, dnode_secno dno)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	struct quad_buffer_head qbh;
	struct dnode *dnode;
	dnode_secno down, up, ndown;
	int p;
	struct hpfs_dirent *de;
	int c1, c2 = 0;
	try_it_again:
	if (hpfs_stop_cycles(i->i_sb, dno, &c1, &c2, "delete_empty_dnode")) return;
	if (!(dnode = hpfs_map_dnode(i->i_sb, dno, &qbh))) return;
	if (dnode->first_free > 56) goto end;
	if (dnode->first_free == 52 || dnode->first_free == 56) {
		struct hpfs_dirent *de_end;
		int root = dnode->root_dnode;
		up = dnode->up;
		de = dnode_first_de(dnode);
		down = de->down ? de_down_pointer(de) : 0;
		if (hpfs_sb(i->i_sb)->sb_chk) if (root && !down) {
			hpfs_error(i->i_sb, "delete_empty_dnode: root dnode %08x is empty", dno);
			goto end;
		}
		hpfs_brelse4(&qbh);
		hpfs_free_dnode(i->i_sb, dno);
		i->i_size -= 2048;
		i->i_blocks -= 4;
		if (root) {
			struct fnode *fnode;
			struct buffer_head *bh;
			struct dnode *d1;
			struct quad_buffer_head qbh1;
			if (hpfs_sb(i->i_sb)->sb_chk)
			    if (up != i->i_ino) {
				hpfs_error(i->i_sb,
					"bad pointer to fnode, dnode %08x, pointing to %08x, should be %08lx",
					dno, up, (unsigned long)i->i_ino);
				return;
			    }
			if ((d1 = hpfs_map_dnode(i->i_sb, down, &qbh1))) {
				d1->up = up;
				d1->root_dnode = 1;
				hpfs_mark_4buffers_dirty(&qbh1);
				hpfs_brelse4(&qbh1);
			}
			if ((fnode = hpfs_map_fnode(i->i_sb, up, &bh))) {
				fnode->u.external[0].disk_secno = down;
				mark_buffer_dirty(bh);
				brelse(bh);
			}
			hpfs_inode->i_dno = down;
			for_all_poss(i, hpfs_pos_subst, ((loff_t)dno << 4) | 1, (loff_t) 12);
			return;
		}
		if (!(dnode = hpfs_map_dnode(i->i_sb, up, &qbh))) return;
		p = 1;
		de_end = dnode_end_de(dnode);
		for (de = dnode_first_de(dnode); de < de_end; de = de_next_de(de), p++)
			if (de->down) if (de_down_pointer(de) == dno) goto fnd;
		hpfs_error(i->i_sb, "delete_empty_dnode: pointer to dnode %08x not found in dnode %08x", dno, up);
		goto end;
		fnd:
		for_all_poss(i, hpfs_pos_subst, ((loff_t)dno << 4) | 1, ((loff_t)up << 4) | p);
		if (!down) {
			de->down = 0;
			de->length -= 4;
			dnode->first_free -= 4;
			memmove(de_next_de(de), (char *)de_next_de(de) + 4,
				(char *)dnode + dnode->first_free - (char *)de_next_de(de));
		} else {
			struct dnode *d1;
			struct quad_buffer_head qbh1;
			*(dnode_secno *) ((void *) de + de->length - 4) = down;
			if ((d1 = hpfs_map_dnode(i->i_sb, down, &qbh1))) {
				d1->up = up;
				hpfs_mark_4buffers_dirty(&qbh1);
				hpfs_brelse4(&qbh1);
			}
		}
	} else {
		hpfs_error(i->i_sb, "delete_empty_dnode: dnode %08x, first_free == %03x", dno, dnode->first_free);
		goto end;
	}

	if (!de->last) {
		struct hpfs_dirent *de_next = de_next_de(de);
		struct hpfs_dirent *de_cp;
		struct dnode *d1;
		struct quad_buffer_head qbh1;
		if (!de_next->down) goto endm;
		ndown = de_down_pointer(de_next);
		if (!(de_cp = kmalloc(de->length, GFP_NOFS))) {
			printk("HPFS: out of memory for dtree balancing\n");
			goto endm;
		}
		memcpy(de_cp, de, de->length);
		hpfs_delete_de(i->i_sb, dnode, de);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)up << 4) | p, 4);
		for_all_poss(i, hpfs_pos_del, ((loff_t)up << 4) | p, 1);
		if (de_cp->down) if ((d1 = hpfs_map_dnode(i->i_sb, de_down_pointer(de_cp), &qbh1))) {
			d1->up = ndown;
			hpfs_mark_4buffers_dirty(&qbh1);
			hpfs_brelse4(&qbh1);
		}
		hpfs_add_to_dnode(i, ndown, de_cp->name, de_cp->namelen, de_cp, de_cp->down ? de_down_pointer(de_cp) : 0);
		/*printk("UP-TO-DNODE: %08x (ndown = %08x, down = %08x, dno = %08x)\n", up, ndown, down, dno);*/
		dno = up;
		kfree(de_cp);
		goto try_it_again;
	} else {
		struct hpfs_dirent *de_prev = dnode_pre_last_de(dnode);
		struct hpfs_dirent *de_cp;
		struct dnode *d1;
		struct quad_buffer_head qbh1;
		dnode_secno dlp;
		if (!de_prev) {
			hpfs_error(i->i_sb, "delete_empty_dnode: empty dnode %08x", up);
			hpfs_mark_4buffers_dirty(&qbh);
			hpfs_brelse4(&qbh);
			dno = up;
			goto try_it_again;
		}
		if (!de_prev->down) goto endm;
		ndown = de_down_pointer(de_prev);
		if ((d1 = hpfs_map_dnode(i->i_sb, ndown, &qbh1))) {
			struct hpfs_dirent *del = dnode_last_de(d1);
			dlp = del->down ? de_down_pointer(del) : 0;
			if (!dlp && down) {
				if (d1->first_free > 2044) {
					if (hpfs_sb(i->i_sb)->sb_chk >= 2) {
						printk("HPFS: warning: unbalanced dnode tree, see hpfs.txt 4 more info\n");
						printk("HPFS: warning: terminating balancing operation\n");
					}
					hpfs_brelse4(&qbh1);
					goto endm;
				}
				if (hpfs_sb(i->i_sb)->sb_chk >= 2) {
					printk("HPFS: warning: unbalanced dnode tree, see hpfs.txt 4 more info\n");
					printk("HPFS: warning: goin'on\n");
				}
				del->length += 4;
				del->down = 1;
				d1->first_free += 4;
			}
			if (dlp && !down) {
				del->length -= 4;
				del->down = 0;
				d1->first_free -= 4;
			} else if (down)
				*(dnode_secno *) ((void *) del + del->length - 4) = down;
		} else goto endm;
		if (!(de_cp = kmalloc(de_prev->length, GFP_NOFS))) {
			printk("HPFS: out of memory for dtree balancing\n");
			hpfs_brelse4(&qbh1);
			goto endm;
		}
		hpfs_mark_4buffers_dirty(&qbh1);
		hpfs_brelse4(&qbh1);
		memcpy(de_cp, de_prev, de_prev->length);
		hpfs_delete_de(i->i_sb, dnode, de_prev);
		if (!de_prev->down) {
			de_prev->length += 4;
			de_prev->down = 1;
			dnode->first_free += 4;
		}
		*(dnode_secno *) ((void *) de_prev + de_prev->length - 4) = ndown;
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)up << 4) | (p - 1), 4);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)up << 4) | p, ((loff_t)up << 4) | (p - 1));
		if (down) if ((d1 = hpfs_map_dnode(i->i_sb, de_down_pointer(de), &qbh1))) {
			d1->up = ndown;
			hpfs_mark_4buffers_dirty(&qbh1);
			hpfs_brelse4(&qbh1);
		}
		hpfs_add_to_dnode(i, ndown, de_cp->name, de_cp->namelen, de_cp, dlp);
		dno = up;
		kfree(de_cp);
		goto try_it_again;
	}
	endm:
	hpfs_mark_4buffers_dirty(&qbh);
	end:
	hpfs_brelse4(&qbh);
}


/* Delete dirent from directory */

int hpfs_remove_dirent(struct inode *i, dnode_secno dno, struct hpfs_dirent *de,
		       struct quad_buffer_head *qbh, int depth)
{
	struct dnode *dnode = qbh->data;
	dnode_secno down = 0;
	int lock = 0;
	loff_t t;
	if (de->first || de->last) {
		hpfs_error(i->i_sb, "hpfs_remove_dirent: attempt to delete first or last dirent in dnode %08x", dno);
		hpfs_brelse4(qbh);
		return 1;
	}
	if (de->down) down = de_down_pointer(de);
	if (depth && (de->down || (de == dnode_first_de(dnode) && de_next_de(de)->last))) {
		lock = 1;
		hpfs_lock_creation(i->i_sb);
		if (hpfs_check_free_dnodes(i->i_sb, FREE_DNODES_DEL)) {
			hpfs_brelse4(qbh);
			hpfs_unlock_creation(i->i_sb);
			return 2;
		}
	}
	i->i_version++;
	for_all_poss(i, hpfs_pos_del, (t = get_pos(dnode, de)) + 1, 1);
	hpfs_delete_de(i->i_sb, dnode, de);
	hpfs_mark_4buffers_dirty(qbh);
	hpfs_brelse4(qbh);
	if (down) {
		dnode_secno a = move_to_top(i, down, dno);
		for_all_poss(i, hpfs_pos_subst, 5, t);
		if (a) delete_empty_dnode(i, a);
		if (lock) hpfs_unlock_creation(i->i_sb);
		return !a;
	}
	delete_empty_dnode(i, dno);
	if (lock) hpfs_unlock_creation(i->i_sb);
	return 0;
}

void hpfs_count_dnodes(struct super_block *s, dnode_secno dno, int *n_dnodes,
		       int *n_subdirs, int *n_items)
{
	struct dnode *dnode;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	dnode_secno ptr, odno = 0;
	int c1, c2 = 0;
	int d1, d2 = 0;
	go_down:
	if (n_dnodes) (*n_dnodes)++;
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, dno, &c1, &c2, "hpfs_count_dnodes #1")) return;
	ptr = 0;
	go_up:
	if (!(dnode = hpfs_map_dnode(s, dno, &qbh))) return;
	if (hpfs_sb(s)->sb_chk) if (odno && odno != -1 && dnode->up != odno)
		hpfs_error(s, "hpfs_count_dnodes: bad up pointer; dnode %08x, down %08x points to %08x", odno, dno, dnode->up);
	de = dnode_first_de(dnode);
	if (ptr) while(1) {
		if (de->down) if (de_down_pointer(de) == ptr) goto process_de;
		if (de->last) {
			hpfs_brelse4(&qbh);
			hpfs_error(s, "hpfs_count_dnodes: pointer to dnode %08x not found in dnode %08x, got here from %08x",
				ptr, dno, odno);
			return;
		}
		de = de_next_de(de);
	}
	next_de:
	if (de->down) {
		odno = dno;
		dno = de_down_pointer(de);
		hpfs_brelse4(&qbh);
		goto go_down;
	}
	process_de:
	if (!de->first && !de->last && de->directory && n_subdirs) (*n_subdirs)++;
	if (!de->first && !de->last && n_items) (*n_items)++;
	if ((de = de_next_de(de)) < dnode_end_de(dnode)) goto next_de;
	ptr = dno;
	dno = dnode->up;
	if (dnode->root_dnode) {
		hpfs_brelse4(&qbh);
		return;
	}
	hpfs_brelse4(&qbh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, ptr, &d1, &d2, "hpfs_count_dnodes #2")) return;
	odno = -1;
	goto go_up;
}

static struct hpfs_dirent *map_nth_dirent(struct super_block *s, dnode_secno dno, int n,
					  struct quad_buffer_head *qbh, struct dnode **dn)
{
	int i;
	struct hpfs_dirent *de, *de_end;
	struct dnode *dnode;
	dnode = hpfs_map_dnode(s, dno, qbh);
	if (!dnode) return NULL;
	if (dn) *dn=dnode;
	de = dnode_first_de(dnode);
	de_end = dnode_end_de(dnode);
	for (i = 1; de < de_end; i++, de = de_next_de(de)) {
		if (i == n) {
			return de;
		}	
		if (de->last) break;
	}
	hpfs_brelse4(qbh);
	hpfs_error(s, "map_nth_dirent: n too high; dnode = %08x, requested %08x", dno, n);
	return NULL;
}

dnode_secno hpfs_de_as_down_as_possible(struct super_block *s, dnode_secno dno)
{
	struct quad_buffer_head qbh;
	dnode_secno d = dno;
	dnode_secno up = 0;
	struct hpfs_dirent *de;
	int c1, c2 = 0;

	again:
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, d, &c1, &c2, "hpfs_de_as_down_as_possible"))
			return d;
	if (!(de = map_nth_dirent(s, d, 1, &qbh, NULL))) return dno;
	if (hpfs_sb(s)->sb_chk)
		if (up && ((struct dnode *)qbh.data)->up != up)
			hpfs_error(s, "hpfs_de_as_down_as_possible: bad up pointer; dnode %08x, down %08x points to %08x", up, d, ((struct dnode *)qbh.data)->up);
	if (!de->down) {
		hpfs_brelse4(&qbh);
		return d;
	}
	up = d;
	d = de_down_pointer(de);
	hpfs_brelse4(&qbh);
	goto again;
}

struct hpfs_dirent *map_pos_dirent(struct inode *inode, loff_t *posp,
				   struct quad_buffer_head *qbh)
{
	loff_t pos;
	unsigned c;
	dnode_secno dno;
	struct hpfs_dirent *de, *d;
	struct hpfs_dirent *up_de;
	struct hpfs_dirent *end_up_de;
	struct dnode *dnode;
	struct dnode *up_dnode;
	struct quad_buffer_head qbh0;

	pos = *posp;
	dno = pos >> 6 << 2;
	pos &= 077;
	if (!(de = map_nth_dirent(inode->i_sb, dno, pos, qbh, &dnode)))
		goto bail;

	/* Going to the next dirent */
	if ((d = de_next_de(de)) < dnode_end_de(dnode)) {
		if (!(++*posp & 077)) {
			hpfs_error(inode->i_sb,
				"map_pos_dirent: pos crossed dnode boundary; pos = %08llx",
				(unsigned long long)*posp);
			goto bail;
		}
		/* We're going down the tree */
		if (d->down) {
			*posp = ((loff_t) hpfs_de_as_down_as_possible(inode->i_sb, de_down_pointer(d)) << 4) + 1;
		}
	
		return de;
	}

	/* Going up */
	if (dnode->root_dnode) goto bail;

	if (!(up_dnode = hpfs_map_dnode(inode->i_sb, dnode->up, &qbh0)))
		goto bail;

	end_up_de = dnode_end_de(up_dnode);
	c = 0;
	for (up_de = dnode_first_de(up_dnode); up_de < end_up_de;
	     up_de = de_next_de(up_de)) {
		if (!(++c & 077)) hpfs_error(inode->i_sb,
			"map_pos_dirent: pos crossed dnode boundary; dnode = %08x", dnode->up);
		if (up_de->down && de_down_pointer(up_de) == dno) {
			*posp = ((loff_t) dnode->up << 4) + c;
			hpfs_brelse4(&qbh0);
			return de;
		}
	}
	
	hpfs_error(inode->i_sb, "map_pos_dirent: pointer to dnode %08x not found in parent dnode %08x",
		dno, dnode->up);
	hpfs_brelse4(&qbh0);
	
	bail:
	*posp = 12;
	return de;
}

/* Find a dirent in tree */

struct hpfs_dirent *map_dirent(struct inode *inode, dnode_secno dno, char *name, unsigned len,
			       dnode_secno *dd, struct quad_buffer_head *qbh)
{
	struct dnode *dnode;
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end;
	int c1, c2 = 0;

	if (!S_ISDIR(inode->i_mode)) hpfs_error(inode->i_sb, "map_dirent: not a directory\n");
	again:
	if (hpfs_sb(inode->i_sb)->sb_chk)
		if (hpfs_stop_cycles(inode->i_sb, dno, &c1, &c2, "map_dirent")) return NULL;
	if (!(dnode = hpfs_map_dnode(inode->i_sb, dno, qbh))) return NULL;
	
	de_end = dnode_end_de(dnode);
	for (de = dnode_first_de(dnode); de < de_end; de = de_next_de(de)) {
		int t = hpfs_compare_names(inode->i_sb, name, len, de->name, de->namelen, de->last);
		if (!t) {
			if (dd) *dd = dno;
			return de;
		}
		if (t < 0) {
			if (de->down) {
				dno = de_down_pointer(de);
				hpfs_brelse4(qbh);
				goto again;
			}
		break;
		}
	}
	hpfs_brelse4(qbh);
	return NULL;
}

/*
 * Remove empty directory. In normal cases it is only one dnode with two
 * entries, but we must handle also such obscure cases when it's a tree
 * of empty dnodes.
 */

void hpfs_remove_dtree(struct super_block *s, dnode_secno dno)
{
	struct quad_buffer_head qbh;
	struct dnode *dnode;
	struct hpfs_dirent *de;
	dnode_secno d1, d2, rdno = dno;
	while (1) {
		if (!(dnode = hpfs_map_dnode(s, dno, &qbh))) return;
		de = dnode_first_de(dnode);
		if (de->last) {
			if (de->down) d1 = de_down_pointer(de);
			else goto error;
			hpfs_brelse4(&qbh);
			hpfs_free_dnode(s, dno);
			dno = d1;
		} else break;
	}
	if (!de->first) goto error;
	d1 = de->down ? de_down_pointer(de) : 0;
	de = de_next_de(de);
	if (!de->last) goto error;
	d2 = de->down ? de_down_pointer(de) : 0;
	hpfs_brelse4(&qbh);
	hpfs_free_dnode(s, dno);
	do {
		while (d1) {
			if (!(dnode = hpfs_map_dnode(s, dno = d1, &qbh))) return;
			de = dnode_first_de(dnode);
			if (!de->last) goto error;
			d1 = de->down ? de_down_pointer(de) : 0;
			hpfs_brelse4(&qbh);
			hpfs_free_dnode(s, dno);
		}
		d1 = d2;
		d2 = 0;
	} while (d1);
	return;
	error:
	hpfs_brelse4(&qbh);
	hpfs_free_dnode(s, dno);
	hpfs_error(s, "directory %08x is corrupted or not empty", rdno);
}

/* 
 * Find dirent for specified fnode. Use truncated 15-char name in fnode as
 * a help for searching.
 */

struct hpfs_dirent *map_fnode_dirent(struct super_block *s, fnode_secno fno,
				     struct fnode *f, struct quad_buffer_head *qbh)
{
	char *name1;
	char *name2;
	int name1len, name2len;
	struct dnode *d;
	dnode_secno dno, downd;
	struct fnode *upf;
	struct buffer_head *bh;
	struct hpfs_dirent *de, *de_end;
	int c;
	int c1, c2 = 0;
	int d1, d2 = 0;
	name1 = f->name;
	if (!(name2 = kmalloc(256, GFP_NOFS))) {
		printk("HPFS: out of memory, can't map dirent\n");
		return NULL;
	}
	if (f->len <= 15)
		memcpy(name2, name1, name1len = name2len = f->len);
	else {
		memcpy(name2, name1, 15);
		memset(name2 + 15, 0xff, 256 - 15);
		/*name2[15] = 0xff;*/
		name1len = 15; name2len = 256;
	}
	if (!(upf = hpfs_map_fnode(s, f->up, &bh))) {
		kfree(name2);
		return NULL;
	}	
	if (!upf->dirflag) {
		brelse(bh);
		hpfs_error(s, "fnode %08x has non-directory parent %08x", fno, f->up);
		kfree(name2);
		return NULL;
	}
	dno = upf->u.external[0].disk_secno;
	brelse(bh);
	go_down:
	downd = 0;
	go_up:
	if (!(d = hpfs_map_dnode(s, dno, qbh))) {
		kfree(name2);
		return NULL;
	}
	de_end = dnode_end_de(d);
	de = dnode_first_de(d);
	if (downd) {
		while (de < de_end) {
			if (de->down) if (de_down_pointer(de) == downd) goto f;
			de = de_next_de(de);
		}
		hpfs_error(s, "pointer to dnode %08x not found in dnode %08x", downd, dno);
		hpfs_brelse4(qbh);
		kfree(name2);
		return NULL;
	}
	next_de:
	if (de->fnode == fno) {
		kfree(name2);
		return de;
	}
	c = hpfs_compare_names(s, name1, name1len, de->name, de->namelen, de->last);
	if (c < 0 && de->down) {
		dno = de_down_pointer(de);
		hpfs_brelse4(qbh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, dno, &c1, &c2, "map_fnode_dirent #1")) {
			kfree(name2);
			return NULL;
		}
		goto go_down;
	}
	f:
	if (de->fnode == fno) {
		kfree(name2);
		return de;
	}
	c = hpfs_compare_names(s, name2, name2len, de->name, de->namelen, de->last);
	if (c < 0 && !de->last) goto not_found;
	if ((de = de_next_de(de)) < de_end) goto next_de;
	if (d->root_dnode) goto not_found;
	downd = dno;
	dno = d->up;
	hpfs_brelse4(qbh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, downd, &d1, &d2, "map_fnode_dirent #2")) {
			kfree(name2);
			return NULL;
		}
	goto go_up;
	not_found:
	hpfs_brelse4(qbh);
	hpfs_error(s, "dirent for fnode %08x not found", fno);
	kfree(name2);
	return NULL;
}
