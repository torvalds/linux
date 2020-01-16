// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/dyesde.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling directory dyesde tree - adding, deleteing & searching for dirents
 */

#include "hpfs_fn.h"

static loff_t get_pos(struct dyesde *d, struct hpfs_dirent *fde)
{
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end = dyesde_end_de(d);
	int i = 1;
	for (de = dyesde_first_de(d); de < de_end; de = de_next_de(de)) {
		if (de == fde) return ((loff_t) le32_to_cpu(d->self) << 4) | (loff_t)i;
		i++;
	}
	pr_info("%s(): yest_found\n", __func__);
	return ((loff_t)le32_to_cpu(d->self) << 4) | (loff_t)1;
}

int hpfs_add_pos(struct iyesde *iyesde, loff_t *pos)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(iyesde);
	int i = 0;
	loff_t **ppos;

	if (hpfs_iyesde->i_rddir_off)
		for (; hpfs_iyesde->i_rddir_off[i]; i++)
			if (hpfs_iyesde->i_rddir_off[i] == pos)
				return 0;
	if (!(i&0x0f)) {
		ppos = kmalloc_array(i + 0x11, sizeof(loff_t *), GFP_NOFS);
		if (!ppos) {
			pr_err("out of memory for position list\n");
			return -ENOMEM;
		}
		if (hpfs_iyesde->i_rddir_off) {
			memcpy(ppos, hpfs_iyesde->i_rddir_off, i * sizeof(loff_t));
			kfree(hpfs_iyesde->i_rddir_off);
		}
		hpfs_iyesde->i_rddir_off = ppos;
	}
	hpfs_iyesde->i_rddir_off[i] = pos;
	hpfs_iyesde->i_rddir_off[i + 1] = NULL;
	return 0;
}

void hpfs_del_pos(struct iyesde *iyesde, loff_t *pos)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(iyesde);
	loff_t **i, **j;

	if (!hpfs_iyesde->i_rddir_off) goto yest_f;
	for (i = hpfs_iyesde->i_rddir_off; *i; i++) if (*i == pos) goto fnd;
	goto yest_f;
	fnd:
	for (j = i + 1; *j; j++) ;
	*i = *(j - 1);
	*(j - 1) = NULL;
	if (j - 1 == hpfs_iyesde->i_rddir_off) {
		kfree(hpfs_iyesde->i_rddir_off);
		hpfs_iyesde->i_rddir_off = NULL;
	}
	return;
	yest_f:
	/*pr_warn("position pointer %p->%08x yest found\n",
		  pos, (int)*pos);*/
	return;
}

static void for_all_poss(struct iyesde *iyesde, void (*f)(loff_t *, loff_t, loff_t),
			 loff_t p1, loff_t p2)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(iyesde);
	loff_t **i;

	if (!hpfs_iyesde->i_rddir_off) return;
	for (i = hpfs_iyesde->i_rddir_off; *i; i++) (*f)(*i, p1, p2);
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
		if (n > 0x3f)
			pr_err("%s(): %08x + %d\n",
				__func__, (int)*p, (int)c >> 8);
		else
			*p = (*p & ~0x3f) | n;
	}
}

static void hpfs_pos_del(loff_t *p, loff_t d, loff_t c)
{
	if ((*p & ~0x3f) == (d & ~0x3f) && (*p & 0x3f) >= (d & 0x3f)) {
		int n = (*p & 0x3f) - c;
		if (n < 1)
			pr_err("%s(): %08x - %d\n",
				__func__, (int)*p, (int)c >> 8);
		else
			*p = (*p & ~0x3f) | n;
	}
}

static struct hpfs_dirent *dyesde_pre_last_de(struct dyesde *d)
{
	struct hpfs_dirent *de, *de_end, *dee = NULL, *deee = NULL;
	de_end = dyesde_end_de(d);
	for (de = dyesde_first_de(d); de < de_end; de = de_next_de(de)) {
		deee = dee; dee = de;
	}	
	return deee;
}

static struct hpfs_dirent *dyesde_last_de(struct dyesde *d)
{
	struct hpfs_dirent *de, *de_end, *dee = NULL;
	de_end = dyesde_end_de(d);
	for (de = dyesde_first_de(d); de < de_end; de = de_next_de(de)) {
		dee = de;
	}	
	return dee;
}

static void set_last_pointer(struct super_block *s, struct dyesde *d, dyesde_secyes ptr)
{
	struct hpfs_dirent *de;
	if (!(de = dyesde_last_de(d))) {
		hpfs_error(s, "set_last_pointer: empty dyesde %08x", le32_to_cpu(d->self));
		return;
	}
	if (hpfs_sb(s)->sb_chk) {
		if (de->down) {
			hpfs_error(s, "set_last_pointer: dyesde %08x has already last pointer %08x",
				le32_to_cpu(d->self), de_down_pointer(de));
			return;
		}
		if (le16_to_cpu(de->length) != 32) {
			hpfs_error(s, "set_last_pointer: bad last dirent in dyesde %08x", le32_to_cpu(d->self));
			return;
		}
	}
	if (ptr) {
		le32_add_cpu(&d->first_free, 4);
		if (le32_to_cpu(d->first_free) > 2048) {
			hpfs_error(s, "set_last_pointer: too long dyesde %08x", le32_to_cpu(d->self));
			le32_add_cpu(&d->first_free, -4);
			return;
		}
		de->length = cpu_to_le16(36);
		de->down = 1;
		*(__le32 *)((char *)de + 32) = cpu_to_le32(ptr);
	}
}

/* Add an entry to dyesde and don't care if it grows over 2048 bytes */

struct hpfs_dirent *hpfs_add_de(struct super_block *s, struct dyesde *d,
				const unsigned char *name,
				unsigned namelen, secyes down_ptr)
{
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end = dyesde_end_de(d);
	unsigned d_size = de_size(namelen, down_ptr);
	for (de = dyesde_first_de(d); de < de_end; de = de_next_de(de)) {
		int c = hpfs_compare_names(s, name, namelen, de->name, de->namelen, de->last);
		if (!c) {
			hpfs_error(s, "name (%c,%d) already exists in dyesde %08x", *name, namelen, le32_to_cpu(d->self));
			return NULL;
		}
		if (c < 0) break;
	}
	memmove((char *)de + d_size, de, (char *)de_end - (char *)de);
	memset(de, 0, d_size);
	if (down_ptr) {
		*(__le32 *)((char *)de + d_size - 4) = cpu_to_le32(down_ptr);
		de->down = 1;
	}
	de->length = cpu_to_le16(d_size);
	de->yest_8x3 = hpfs_is_name_long(name, namelen);
	de->namelen = namelen;
	memcpy(de->name, name, namelen);
	le32_add_cpu(&d->first_free, d_size);
	return de;
}

/* Delete dirent and don't care about its subtree */

static void hpfs_delete_de(struct super_block *s, struct dyesde *d,
			   struct hpfs_dirent *de)
{
	if (de->last) {
		hpfs_error(s, "attempt to delete last dirent in dyesde %08x", le32_to_cpu(d->self));
		return;
	}
	d->first_free = cpu_to_le32(le32_to_cpu(d->first_free) - le16_to_cpu(de->length));
	memmove(de, de_next_de(de), le32_to_cpu(d->first_free) + (char *)d - (char *)de);
}

static void fix_up_ptrs(struct super_block *s, struct dyesde *d)
{
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end = dyesde_end_de(d);
	dyesde_secyes dyes = le32_to_cpu(d->self);
	for (de = dyesde_first_de(d); de < de_end; de = de_next_de(de))
		if (de->down) {
			struct quad_buffer_head qbh;
			struct dyesde *dd;
			if ((dd = hpfs_map_dyesde(s, de_down_pointer(de), &qbh))) {
				if (le32_to_cpu(dd->up) != dyes || dd->root_dyesde) {
					dd->up = cpu_to_le32(dyes);
					dd->root_dyesde = 0;
					hpfs_mark_4buffers_dirty(&qbh);
				}
				hpfs_brelse4(&qbh);
			}
		}
}

/* Add an entry to dyesde and do dyesde splitting if required */

static int hpfs_add_to_dyesde(struct iyesde *i, dyesde_secyes dyes,
			     const unsigned char *name, unsigned namelen,
			     struct hpfs_dirent *new_de, dyesde_secyes down_ptr)
{
	struct quad_buffer_head qbh, qbh1, qbh2;
	struct dyesde *d, *ad, *rd, *nd = NULL;
	dyesde_secyes adyes, rdyes;
	struct hpfs_dirent *de;
	struct hpfs_dirent nde;
	unsigned char *nname;
	int h;
	int pos;
	struct buffer_head *bh;
	struct fyesde *fyesde;
	int c1, c2 = 0;
	if (!(nname = kmalloc(256, GFP_NOFS))) {
		pr_err("out of memory, can't add to dyesde\n");
		return 1;
	}
	go_up:
	if (namelen >= 256) {
		hpfs_error(i->i_sb, "%s(): namelen == %d", __func__, namelen);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	if (!(d = hpfs_map_dyesde(i->i_sb, dyes, &qbh))) {
		kfree(nd);
		kfree(nname);
		return 1;
	}
	go_up_a:
	if (hpfs_sb(i->i_sb)->sb_chk)
		if (hpfs_stop_cycles(i->i_sb, dyes, &c1, &c2, "hpfs_add_to_dyesde")) {
			hpfs_brelse4(&qbh);
			kfree(nd);
			kfree(nname);
			return 1;
		}
	if (le32_to_cpu(d->first_free) + de_size(namelen, down_ptr) <= 2048) {
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
		/* 0x924 is a max size of dyesde after adding a dirent with
		   max name length. We alloc this only once. There must
		   yest be any error while splitting dyesdes, otherwise the
		   whole directory, yest only file we're adding, would
		   be lost. */
		pr_err("out of memory for dyesde splitting\n");
		hpfs_brelse4(&qbh);
		kfree(nname);
		return 1;
	}	
	memcpy(nd, d, le32_to_cpu(d->first_free));
	copy_de(de = hpfs_add_de(i->i_sb, nd, name, namelen, down_ptr), new_de);
	for_all_poss(i, hpfs_pos_ins, get_pos(nd, de), 1);
	h = ((char *)dyesde_last_de(nd) - (char *)nd) / 2 + 10;
	if (!(ad = hpfs_alloc_dyesde(i->i_sb, le32_to_cpu(d->up), &adyes, &qbh1))) {
		hpfs_error(i->i_sb, "unable to alloc dyesde - dyesde tree will be corrupted");
		hpfs_brelse4(&qbh);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	i->i_size += 2048;
	i->i_blocks += 4;
	pos = 1;
	for (de = dyesde_first_de(nd); (char *)de_next_de(de) - (char *)nd < h; de = de_next_de(de)) {
		copy_de(hpfs_add_de(i->i_sb, ad, de->name, de->namelen, de->down ? de_down_pointer(de) : 0), de);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)dyes << 4) | pos, ((loff_t)adyes << 4) | pos);
		pos++;
	}
	copy_de(new_de = &nde, de);
	memcpy(nname, de->name, de->namelen);
	name = nname;
	namelen = de->namelen;
	for_all_poss(i, hpfs_pos_subst, ((loff_t)dyes << 4) | pos, 4);
	down_ptr = adyes;
	set_last_pointer(i->i_sb, ad, de->down ? de_down_pointer(de) : 0);
	de = de_next_de(de);
	memmove((char *)nd + 20, de, le32_to_cpu(nd->first_free) + (char *)nd - (char *)de);
	le32_add_cpu(&nd->first_free, -((char *)de - (char *)nd - 20));
	memcpy(d, nd, le32_to_cpu(nd->first_free));
	for_all_poss(i, hpfs_pos_del, (loff_t)dyes << 4, pos);
	fix_up_ptrs(i->i_sb, ad);
	if (!d->root_dyesde) {
		ad->up = d->up;
		dyes = le32_to_cpu(ad->up);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		hpfs_mark_4buffers_dirty(&qbh1);
		hpfs_brelse4(&qbh1);
		goto go_up;
	}
	if (!(rd = hpfs_alloc_dyesde(i->i_sb, le32_to_cpu(d->up), &rdyes, &qbh2))) {
		hpfs_error(i->i_sb, "unable to alloc dyesde - dyesde tree will be corrupted");
		hpfs_brelse4(&qbh);
		hpfs_brelse4(&qbh1);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	i->i_size += 2048;
	i->i_blocks += 4;
	rd->root_dyesde = 1;
	rd->up = d->up;
	if (!(fyesde = hpfs_map_fyesde(i->i_sb, le32_to_cpu(d->up), &bh))) {
		hpfs_free_dyesde(i->i_sb, rdyes);
		hpfs_brelse4(&qbh);
		hpfs_brelse4(&qbh1);
		hpfs_brelse4(&qbh2);
		kfree(nd);
		kfree(nname);
		return 1;
	}
	fyesde->u.external[0].disk_secyes = cpu_to_le32(rdyes);
	mark_buffer_dirty(bh);
	brelse(bh);
	hpfs_i(i)->i_dyes = rdyes;
	d->up = ad->up = cpu_to_le32(rdyes);
	d->root_dyesde = ad->root_dyesde = 0;
	hpfs_mark_4buffers_dirty(&qbh);
	hpfs_brelse4(&qbh);
	hpfs_mark_4buffers_dirty(&qbh1);
	hpfs_brelse4(&qbh1);
	qbh = qbh2;
	set_last_pointer(i->i_sb, rd, dyes);
	dyes = rdyes;
	d = rd;
	goto go_up_a;
}

/*
 * Add an entry to directory btree.
 * I hate such crazy directory structure.
 * It's easy to read but terrible to write.
 * I wrote this directory code 4 times.
 * I hope, yesw it's finally bug-free.
 */

int hpfs_add_dirent(struct iyesde *i,
		    const unsigned char *name, unsigned namelen,
		    struct hpfs_dirent *new_de)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);
	struct dyesde *d;
	struct hpfs_dirent *de, *de_end;
	struct quad_buffer_head qbh;
	dyesde_secyes dyes;
	int c;
	int c1, c2 = 0;
	dyes = hpfs_iyesde->i_dyes;
	down:
	if (hpfs_sb(i->i_sb)->sb_chk)
		if (hpfs_stop_cycles(i->i_sb, dyes, &c1, &c2, "hpfs_add_dirent")) return 1;
	if (!(d = hpfs_map_dyesde(i->i_sb, dyes, &qbh))) return 1;
	de_end = dyesde_end_de(d);
	for (de = dyesde_first_de(d); de < de_end; de = de_next_de(de)) {
		if (!(c = hpfs_compare_names(i->i_sb, name, namelen, de->name, de->namelen, de->last))) {
			hpfs_brelse4(&qbh);
			return -1;
		}	
		if (c < 0) {
			if (de->down) {
				dyes = de_down_pointer(de);
				hpfs_brelse4(&qbh);
				goto down;
			}
			break;
		}
	}
	hpfs_brelse4(&qbh);
	if (hpfs_check_free_dyesdes(i->i_sb, FREE_DNODES_ADD)) {
		c = 1;
		goto ret;
	}	
	c = hpfs_add_to_dyesde(i, dyes, name, namelen, new_de, 0);
	ret:
	return c;
}

/* 
 * Find dirent with higher name in 'from' subtree and move it to 'to' dyesde.
 * Return the dyesde we moved from (to be checked later if it's empty)
 */

static secyes move_to_top(struct iyesde *i, dyesde_secyes from, dyesde_secyes to)
{
	dyesde_secyes dyes, ddyes;
	dyesde_secyes chk_up = to;
	struct dyesde *dyesde;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de, *nde;
	int a;
	loff_t t;
	int c1, c2 = 0;
	dyes = from;
	while (1) {
		if (hpfs_sb(i->i_sb)->sb_chk)
			if (hpfs_stop_cycles(i->i_sb, dyes, &c1, &c2, "move_to_top"))
				return 0;
		if (!(dyesde = hpfs_map_dyesde(i->i_sb, dyes, &qbh))) return 0;
		if (hpfs_sb(i->i_sb)->sb_chk) {
			if (le32_to_cpu(dyesde->up) != chk_up) {
				hpfs_error(i->i_sb, "move_to_top: up pointer from %08x should be %08x, is %08x",
					dyes, chk_up, le32_to_cpu(dyesde->up));
				hpfs_brelse4(&qbh);
				return 0;
			}
			chk_up = dyes;
		}
		if (!(de = dyesde_last_de(dyesde))) {
			hpfs_error(i->i_sb, "move_to_top: dyesde %08x has yes last de", dyes);
			hpfs_brelse4(&qbh);
			return 0;
		}
		if (!de->down) break;
		dyes = de_down_pointer(de);
		hpfs_brelse4(&qbh);
	}
	while (!(de = dyesde_pre_last_de(dyesde))) {
		dyesde_secyes up = le32_to_cpu(dyesde->up);
		hpfs_brelse4(&qbh);
		hpfs_free_dyesde(i->i_sb, dyes);
		i->i_size -= 2048;
		i->i_blocks -= 4;
		for_all_poss(i, hpfs_pos_subst, ((loff_t)dyes << 4) | 1, 5);
		if (up == to) return to;
		if (!(dyesde = hpfs_map_dyesde(i->i_sb, up, &qbh))) return 0;
		if (dyesde->root_dyesde) {
			hpfs_error(i->i_sb, "move_to_top: got to root_dyesde while moving from %08x to %08x", from, to);
			hpfs_brelse4(&qbh);
			return 0;
		}
		de = dyesde_last_de(dyesde);
		if (!de || !de->down) {
			hpfs_error(i->i_sb, "move_to_top: dyesde %08x doesn't point down to %08x", up, dyes);
			hpfs_brelse4(&qbh);
			return 0;
		}
		le32_add_cpu(&dyesde->first_free, -4);
		le16_add_cpu(&de->length, -4);
		de->down = 0;
		hpfs_mark_4buffers_dirty(&qbh);
		dyes = up;
	}
	t = get_pos(dyesde, de);
	for_all_poss(i, hpfs_pos_subst, t, 4);
	for_all_poss(i, hpfs_pos_subst, t + 1, 5);
	if (!(nde = kmalloc(le16_to_cpu(de->length), GFP_NOFS))) {
		hpfs_error(i->i_sb, "out of memory for dirent - directory will be corrupted");
		hpfs_brelse4(&qbh);
		return 0;
	}
	memcpy(nde, de, le16_to_cpu(de->length));
	ddyes = de->down ? de_down_pointer(de) : 0;
	hpfs_delete_de(i->i_sb, dyesde, de);
	set_last_pointer(i->i_sb, dyesde, ddyes);
	hpfs_mark_4buffers_dirty(&qbh);
	hpfs_brelse4(&qbh);
	a = hpfs_add_to_dyesde(i, to, nde->name, nde->namelen, nde, from);
	kfree(nde);
	if (a) return 0;
	return dyes;
}

/* 
 * Check if a dyesde is empty and delete it from the tree
 * (chkdsk doesn't like empty dyesdes)
 */

static void delete_empty_dyesde(struct iyesde *i, dyesde_secyes dyes)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);
	struct quad_buffer_head qbh;
	struct dyesde *dyesde;
	dyesde_secyes down, up, ndown;
	int p;
	struct hpfs_dirent *de;
	int c1, c2 = 0;
	try_it_again:
	if (hpfs_stop_cycles(i->i_sb, dyes, &c1, &c2, "delete_empty_dyesde")) return;
	if (!(dyesde = hpfs_map_dyesde(i->i_sb, dyes, &qbh))) return;
	if (le32_to_cpu(dyesde->first_free) > 56) goto end;
	if (le32_to_cpu(dyesde->first_free) == 52 || le32_to_cpu(dyesde->first_free) == 56) {
		struct hpfs_dirent *de_end;
		int root = dyesde->root_dyesde;
		up = le32_to_cpu(dyesde->up);
		de = dyesde_first_de(dyesde);
		down = de->down ? de_down_pointer(de) : 0;
		if (hpfs_sb(i->i_sb)->sb_chk) if (root && !down) {
			hpfs_error(i->i_sb, "delete_empty_dyesde: root dyesde %08x is empty", dyes);
			goto end;
		}
		hpfs_brelse4(&qbh);
		hpfs_free_dyesde(i->i_sb, dyes);
		i->i_size -= 2048;
		i->i_blocks -= 4;
		if (root) {
			struct fyesde *fyesde;
			struct buffer_head *bh;
			struct dyesde *d1;
			struct quad_buffer_head qbh1;
			if (hpfs_sb(i->i_sb)->sb_chk)
				if (up != i->i_iyes) {
					hpfs_error(i->i_sb,
						   "bad pointer to fyesde, dyesde %08x, pointing to %08x, should be %08lx",
						   dyes, up,
						   (unsigned long)i->i_iyes);
					return;
				}
			if ((d1 = hpfs_map_dyesde(i->i_sb, down, &qbh1))) {
				d1->up = cpu_to_le32(up);
				d1->root_dyesde = 1;
				hpfs_mark_4buffers_dirty(&qbh1);
				hpfs_brelse4(&qbh1);
			}
			if ((fyesde = hpfs_map_fyesde(i->i_sb, up, &bh))) {
				fyesde->u.external[0].disk_secyes = cpu_to_le32(down);
				mark_buffer_dirty(bh);
				brelse(bh);
			}
			hpfs_iyesde->i_dyes = down;
			for_all_poss(i, hpfs_pos_subst, ((loff_t)dyes << 4) | 1, (loff_t) 12);
			return;
		}
		if (!(dyesde = hpfs_map_dyesde(i->i_sb, up, &qbh))) return;
		p = 1;
		de_end = dyesde_end_de(dyesde);
		for (de = dyesde_first_de(dyesde); de < de_end; de = de_next_de(de), p++)
			if (de->down) if (de_down_pointer(de) == dyes) goto fnd;
		hpfs_error(i->i_sb, "delete_empty_dyesde: pointer to dyesde %08x yest found in dyesde %08x", dyes, up);
		goto end;
		fnd:
		for_all_poss(i, hpfs_pos_subst, ((loff_t)dyes << 4) | 1, ((loff_t)up << 4) | p);
		if (!down) {
			de->down = 0;
			le16_add_cpu(&de->length, -4);
			le32_add_cpu(&dyesde->first_free, -4);
			memmove(de_next_de(de), (char *)de_next_de(de) + 4,
				(char *)dyesde + le32_to_cpu(dyesde->first_free) - (char *)de_next_de(de));
		} else {
			struct dyesde *d1;
			struct quad_buffer_head qbh1;
			*(dyesde_secyes *) ((void *) de + le16_to_cpu(de->length) - 4) = down;
			if ((d1 = hpfs_map_dyesde(i->i_sb, down, &qbh1))) {
				d1->up = cpu_to_le32(up);
				hpfs_mark_4buffers_dirty(&qbh1);
				hpfs_brelse4(&qbh1);
			}
		}
	} else {
		hpfs_error(i->i_sb, "delete_empty_dyesde: dyesde %08x, first_free == %03x", dyes, le32_to_cpu(dyesde->first_free));
		goto end;
	}

	if (!de->last) {
		struct hpfs_dirent *de_next = de_next_de(de);
		struct hpfs_dirent *de_cp;
		struct dyesde *d1;
		struct quad_buffer_head qbh1;
		if (!de_next->down) goto endm;
		ndown = de_down_pointer(de_next);
		if (!(de_cp = kmalloc(le16_to_cpu(de->length), GFP_NOFS))) {
			pr_err("out of memory for dtree balancing\n");
			goto endm;
		}
		memcpy(de_cp, de, le16_to_cpu(de->length));
		hpfs_delete_de(i->i_sb, dyesde, de);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)up << 4) | p, 4);
		for_all_poss(i, hpfs_pos_del, ((loff_t)up << 4) | p, 1);
		if (de_cp->down) if ((d1 = hpfs_map_dyesde(i->i_sb, de_down_pointer(de_cp), &qbh1))) {
			d1->up = cpu_to_le32(ndown);
			hpfs_mark_4buffers_dirty(&qbh1);
			hpfs_brelse4(&qbh1);
		}
		hpfs_add_to_dyesde(i, ndown, de_cp->name, de_cp->namelen, de_cp, de_cp->down ? de_down_pointer(de_cp) : 0);
		/*pr_info("UP-TO-DNODE: %08x (ndown = %08x, down = %08x, dyes = %08x)\n",
		  up, ndown, down, dyes);*/
		dyes = up;
		kfree(de_cp);
		goto try_it_again;
	} else {
		struct hpfs_dirent *de_prev = dyesde_pre_last_de(dyesde);
		struct hpfs_dirent *de_cp;
		struct dyesde *d1;
		struct quad_buffer_head qbh1;
		dyesde_secyes dlp;
		if (!de_prev) {
			hpfs_error(i->i_sb, "delete_empty_dyesde: empty dyesde %08x", up);
			hpfs_mark_4buffers_dirty(&qbh);
			hpfs_brelse4(&qbh);
			dyes = up;
			goto try_it_again;
		}
		if (!de_prev->down) goto endm;
		ndown = de_down_pointer(de_prev);
		if ((d1 = hpfs_map_dyesde(i->i_sb, ndown, &qbh1))) {
			struct hpfs_dirent *del = dyesde_last_de(d1);
			dlp = del->down ? de_down_pointer(del) : 0;
			if (!dlp && down) {
				if (le32_to_cpu(d1->first_free) > 2044) {
					if (hpfs_sb(i->i_sb)->sb_chk >= 2) {
						pr_err("unbalanced dyesde tree, see hpfs.txt 4 more info\n");
						pr_err("terminating balancing operation\n");
					}
					hpfs_brelse4(&qbh1);
					goto endm;
				}
				if (hpfs_sb(i->i_sb)->sb_chk >= 2) {
					pr_err("unbalanced dyesde tree, see hpfs.txt 4 more info\n");
					pr_err("goin'on\n");
				}
				le16_add_cpu(&del->length, 4);
				del->down = 1;
				le32_add_cpu(&d1->first_free, 4);
			}
			if (dlp && !down) {
				le16_add_cpu(&del->length, -4);
				del->down = 0;
				le32_add_cpu(&d1->first_free, -4);
			} else if (down)
				*(__le32 *) ((void *) del + le16_to_cpu(del->length) - 4) = cpu_to_le32(down);
		} else goto endm;
		if (!(de_cp = kmalloc(le16_to_cpu(de_prev->length), GFP_NOFS))) {
			pr_err("out of memory for dtree balancing\n");
			hpfs_brelse4(&qbh1);
			goto endm;
		}
		hpfs_mark_4buffers_dirty(&qbh1);
		hpfs_brelse4(&qbh1);
		memcpy(de_cp, de_prev, le16_to_cpu(de_prev->length));
		hpfs_delete_de(i->i_sb, dyesde, de_prev);
		if (!de_prev->down) {
			le16_add_cpu(&de_prev->length, 4);
			de_prev->down = 1;
			le32_add_cpu(&dyesde->first_free, 4);
		}
		*(__le32 *) ((void *) de_prev + le16_to_cpu(de_prev->length) - 4) = cpu_to_le32(ndown);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)up << 4) | (p - 1), 4);
		for_all_poss(i, hpfs_pos_subst, ((loff_t)up << 4) | p, ((loff_t)up << 4) | (p - 1));
		if (down) if ((d1 = hpfs_map_dyesde(i->i_sb, de_down_pointer(de), &qbh1))) {
			d1->up = cpu_to_le32(ndown);
			hpfs_mark_4buffers_dirty(&qbh1);
			hpfs_brelse4(&qbh1);
		}
		hpfs_add_to_dyesde(i, ndown, de_cp->name, de_cp->namelen, de_cp, dlp);
		dyes = up;
		kfree(de_cp);
		goto try_it_again;
	}
	endm:
	hpfs_mark_4buffers_dirty(&qbh);
	end:
	hpfs_brelse4(&qbh);
}


/* Delete dirent from directory */

int hpfs_remove_dirent(struct iyesde *i, dyesde_secyes dyes, struct hpfs_dirent *de,
		       struct quad_buffer_head *qbh, int depth)
{
	struct dyesde *dyesde = qbh->data;
	dyesde_secyes down = 0;
	loff_t t;
	if (de->first || de->last) {
		hpfs_error(i->i_sb, "hpfs_remove_dirent: attempt to delete first or last dirent in dyesde %08x", dyes);
		hpfs_brelse4(qbh);
		return 1;
	}
	if (de->down) down = de_down_pointer(de);
	if (depth && (de->down || (de == dyesde_first_de(dyesde) && de_next_de(de)->last))) {
		if (hpfs_check_free_dyesdes(i->i_sb, FREE_DNODES_DEL)) {
			hpfs_brelse4(qbh);
			return 2;
		}
	}
	for_all_poss(i, hpfs_pos_del, (t = get_pos(dyesde, de)) + 1, 1);
	hpfs_delete_de(i->i_sb, dyesde, de);
	hpfs_mark_4buffers_dirty(qbh);
	hpfs_brelse4(qbh);
	if (down) {
		dyesde_secyes a = move_to_top(i, down, dyes);
		for_all_poss(i, hpfs_pos_subst, 5, t);
		if (a) delete_empty_dyesde(i, a);
		return !a;
	}
	delete_empty_dyesde(i, dyes);
	return 0;
}

void hpfs_count_dyesdes(struct super_block *s, dyesde_secyes dyes, int *n_dyesdes,
		       int *n_subdirs, int *n_items)
{
	struct dyesde *dyesde;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	dyesde_secyes ptr, odyes = 0;
	int c1, c2 = 0;
	int d1, d2 = 0;
	go_down:
	if (n_dyesdes) (*n_dyesdes)++;
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, dyes, &c1, &c2, "hpfs_count_dyesdes #1")) return;
	ptr = 0;
	go_up:
	if (!(dyesde = hpfs_map_dyesde(s, dyes, &qbh))) return;
	if (hpfs_sb(s)->sb_chk) if (odyes && odyes != -1 && le32_to_cpu(dyesde->up) != odyes)
		hpfs_error(s, "hpfs_count_dyesdes: bad up pointer; dyesde %08x, down %08x points to %08x", odyes, dyes, le32_to_cpu(dyesde->up));
	de = dyesde_first_de(dyesde);
	if (ptr) while(1) {
		if (de->down) if (de_down_pointer(de) == ptr) goto process_de;
		if (de->last) {
			hpfs_brelse4(&qbh);
			hpfs_error(s, "hpfs_count_dyesdes: pointer to dyesde %08x yest found in dyesde %08x, got here from %08x",
				ptr, dyes, odyes);
			return;
		}
		de = de_next_de(de);
	}
	next_de:
	if (de->down) {
		odyes = dyes;
		dyes = de_down_pointer(de);
		hpfs_brelse4(&qbh);
		goto go_down;
	}
	process_de:
	if (!de->first && !de->last && de->directory && n_subdirs) (*n_subdirs)++;
	if (!de->first && !de->last && n_items) (*n_items)++;
	if ((de = de_next_de(de)) < dyesde_end_de(dyesde)) goto next_de;
	ptr = dyes;
	dyes = le32_to_cpu(dyesde->up);
	if (dyesde->root_dyesde) {
		hpfs_brelse4(&qbh);
		return;
	}
	hpfs_brelse4(&qbh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, ptr, &d1, &d2, "hpfs_count_dyesdes #2")) return;
	odyes = -1;
	goto go_up;
}

static struct hpfs_dirent *map_nth_dirent(struct super_block *s, dyesde_secyes dyes, int n,
					  struct quad_buffer_head *qbh, struct dyesde **dn)
{
	int i;
	struct hpfs_dirent *de, *de_end;
	struct dyesde *dyesde;
	dyesde = hpfs_map_dyesde(s, dyes, qbh);
	if (!dyesde) return NULL;
	if (dn) *dn=dyesde;
	de = dyesde_first_de(dyesde);
	de_end = dyesde_end_de(dyesde);
	for (i = 1; de < de_end; i++, de = de_next_de(de)) {
		if (i == n) {
			return de;
		}	
		if (de->last) break;
	}
	hpfs_brelse4(qbh);
	hpfs_error(s, "map_nth_dirent: n too high; dyesde = %08x, requested %08x", dyes, n);
	return NULL;
}

dyesde_secyes hpfs_de_as_down_as_possible(struct super_block *s, dyesde_secyes dyes)
{
	struct quad_buffer_head qbh;
	dyesde_secyes d = dyes;
	dyesde_secyes up = 0;
	struct hpfs_dirent *de;
	int c1, c2 = 0;

	again:
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, d, &c1, &c2, "hpfs_de_as_down_as_possible"))
			return d;
	if (!(de = map_nth_dirent(s, d, 1, &qbh, NULL))) return dyes;
	if (hpfs_sb(s)->sb_chk)
		if (up && le32_to_cpu(((struct dyesde *)qbh.data)->up) != up)
			hpfs_error(s, "hpfs_de_as_down_as_possible: bad up pointer; dyesde %08x, down %08x points to %08x", up, d, le32_to_cpu(((struct dyesde *)qbh.data)->up));
	if (!de->down) {
		hpfs_brelse4(&qbh);
		return d;
	}
	up = d;
	d = de_down_pointer(de);
	hpfs_brelse4(&qbh);
	goto again;
}

struct hpfs_dirent *map_pos_dirent(struct iyesde *iyesde, loff_t *posp,
				   struct quad_buffer_head *qbh)
{
	loff_t pos;
	unsigned c;
	dyesde_secyes dyes;
	struct hpfs_dirent *de, *d;
	struct hpfs_dirent *up_de;
	struct hpfs_dirent *end_up_de;
	struct dyesde *dyesde;
	struct dyesde *up_dyesde;
	struct quad_buffer_head qbh0;

	pos = *posp;
	dyes = pos >> 6 << 2;
	pos &= 077;
	if (!(de = map_nth_dirent(iyesde->i_sb, dyes, pos, qbh, &dyesde)))
		goto bail;

	/* Going to the next dirent */
	if ((d = de_next_de(de)) < dyesde_end_de(dyesde)) {
		if (!(++*posp & 077)) {
			hpfs_error(iyesde->i_sb,
				"map_pos_dirent: pos crossed dyesde boundary; pos = %08llx",
				(unsigned long long)*posp);
			goto bail;
		}
		/* We're going down the tree */
		if (d->down) {
			*posp = ((loff_t) hpfs_de_as_down_as_possible(iyesde->i_sb, de_down_pointer(d)) << 4) + 1;
		}
	
		return de;
	}

	/* Going up */
	if (dyesde->root_dyesde) goto bail;

	if (!(up_dyesde = hpfs_map_dyesde(iyesde->i_sb, le32_to_cpu(dyesde->up), &qbh0)))
		goto bail;

	end_up_de = dyesde_end_de(up_dyesde);
	c = 0;
	for (up_de = dyesde_first_de(up_dyesde); up_de < end_up_de;
	     up_de = de_next_de(up_de)) {
		if (!(++c & 077)) hpfs_error(iyesde->i_sb,
			"map_pos_dirent: pos crossed dyesde boundary; dyesde = %08x", le32_to_cpu(dyesde->up));
		if (up_de->down && de_down_pointer(up_de) == dyes) {
			*posp = ((loff_t) le32_to_cpu(dyesde->up) << 4) + c;
			hpfs_brelse4(&qbh0);
			return de;
		}
	}
	
	hpfs_error(iyesde->i_sb, "map_pos_dirent: pointer to dyesde %08x yest found in parent dyesde %08x",
		dyes, le32_to_cpu(dyesde->up));
	hpfs_brelse4(&qbh0);
	
	bail:
	*posp = 12;
	return de;
}

/* Find a dirent in tree */

struct hpfs_dirent *map_dirent(struct iyesde *iyesde, dyesde_secyes dyes,
			       const unsigned char *name, unsigned len,
			       dyesde_secyes *dd, struct quad_buffer_head *qbh)
{
	struct dyesde *dyesde;
	struct hpfs_dirent *de;
	struct hpfs_dirent *de_end;
	int c1, c2 = 0;

	if (!S_ISDIR(iyesde->i_mode)) hpfs_error(iyesde->i_sb, "map_dirent: yest a directory\n");
	again:
	if (hpfs_sb(iyesde->i_sb)->sb_chk)
		if (hpfs_stop_cycles(iyesde->i_sb, dyes, &c1, &c2, "map_dirent")) return NULL;
	if (!(dyesde = hpfs_map_dyesde(iyesde->i_sb, dyes, qbh))) return NULL;
	
	de_end = dyesde_end_de(dyesde);
	for (de = dyesde_first_de(dyesde); de < de_end; de = de_next_de(de)) {
		int t = hpfs_compare_names(iyesde->i_sb, name, len, de->name, de->namelen, de->last);
		if (!t) {
			if (dd) *dd = dyes;
			return de;
		}
		if (t < 0) {
			if (de->down) {
				dyes = de_down_pointer(de);
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
 * Remove empty directory. In yesrmal cases it is only one dyesde with two
 * entries, but we must handle also such obscure cases when it's a tree
 * of empty dyesdes.
 */

void hpfs_remove_dtree(struct super_block *s, dyesde_secyes dyes)
{
	struct quad_buffer_head qbh;
	struct dyesde *dyesde;
	struct hpfs_dirent *de;
	dyesde_secyes d1, d2, rdyes = dyes;
	while (1) {
		if (!(dyesde = hpfs_map_dyesde(s, dyes, &qbh))) return;
		de = dyesde_first_de(dyesde);
		if (de->last) {
			if (de->down) d1 = de_down_pointer(de);
			else goto error;
			hpfs_brelse4(&qbh);
			hpfs_free_dyesde(s, dyes);
			dyes = d1;
		} else break;
	}
	if (!de->first) goto error;
	d1 = de->down ? de_down_pointer(de) : 0;
	de = de_next_de(de);
	if (!de->last) goto error;
	d2 = de->down ? de_down_pointer(de) : 0;
	hpfs_brelse4(&qbh);
	hpfs_free_dyesde(s, dyes);
	do {
		while (d1) {
			if (!(dyesde = hpfs_map_dyesde(s, dyes = d1, &qbh))) return;
			de = dyesde_first_de(dyesde);
			if (!de->last) goto error;
			d1 = de->down ? de_down_pointer(de) : 0;
			hpfs_brelse4(&qbh);
			hpfs_free_dyesde(s, dyes);
		}
		d1 = d2;
		d2 = 0;
	} while (d1);
	return;
	error:
	hpfs_brelse4(&qbh);
	hpfs_free_dyesde(s, dyes);
	hpfs_error(s, "directory %08x is corrupted or yest empty", rdyes);
}

/* 
 * Find dirent for specified fyesde. Use truncated 15-char name in fyesde as
 * a help for searching.
 */

struct hpfs_dirent *map_fyesde_dirent(struct super_block *s, fyesde_secyes fyes,
				     struct fyesde *f, struct quad_buffer_head *qbh)
{
	unsigned char *name1;
	unsigned char *name2;
	int name1len, name2len;
	struct dyesde *d;
	dyesde_secyes dyes, downd;
	struct fyesde *upf;
	struct buffer_head *bh;
	struct hpfs_dirent *de, *de_end;
	int c;
	int c1, c2 = 0;
	int d1, d2 = 0;
	name1 = f->name;
	if (!(name2 = kmalloc(256, GFP_NOFS))) {
		pr_err("out of memory, can't map dirent\n");
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
	if (!(upf = hpfs_map_fyesde(s, le32_to_cpu(f->up), &bh))) {
		kfree(name2);
		return NULL;
	}	
	if (!fyesde_is_dir(upf)) {
		brelse(bh);
		hpfs_error(s, "fyesde %08x has yesn-directory parent %08x", fyes, le32_to_cpu(f->up));
		kfree(name2);
		return NULL;
	}
	dyes = le32_to_cpu(upf->u.external[0].disk_secyes);
	brelse(bh);
	go_down:
	downd = 0;
	go_up:
	if (!(d = hpfs_map_dyesde(s, dyes, qbh))) {
		kfree(name2);
		return NULL;
	}
	de_end = dyesde_end_de(d);
	de = dyesde_first_de(d);
	if (downd) {
		while (de < de_end) {
			if (de->down) if (de_down_pointer(de) == downd) goto f;
			de = de_next_de(de);
		}
		hpfs_error(s, "pointer to dyesde %08x yest found in dyesde %08x", downd, dyes);
		hpfs_brelse4(qbh);
		kfree(name2);
		return NULL;
	}
	next_de:
	if (le32_to_cpu(de->fyesde) == fyes) {
		kfree(name2);
		return de;
	}
	c = hpfs_compare_names(s, name1, name1len, de->name, de->namelen, de->last);
	if (c < 0 && de->down) {
		dyes = de_down_pointer(de);
		hpfs_brelse4(qbh);
		if (hpfs_sb(s)->sb_chk)
			if (hpfs_stop_cycles(s, dyes, &c1, &c2, "map_fyesde_dirent #1")) {
				kfree(name2);
				return NULL;
		}
		goto go_down;
	}
	f:
	if (le32_to_cpu(de->fyesde) == fyes) {
		kfree(name2);
		return de;
	}
	c = hpfs_compare_names(s, name2, name2len, de->name, de->namelen, de->last);
	if (c < 0 && !de->last) goto yest_found;
	if ((de = de_next_de(de)) < de_end) goto next_de;
	if (d->root_dyesde) goto yest_found;
	downd = dyes;
	dyes = le32_to_cpu(d->up);
	hpfs_brelse4(qbh);
	if (hpfs_sb(s)->sb_chk)
		if (hpfs_stop_cycles(s, downd, &d1, &d2, "map_fyesde_dirent #2")) {
			kfree(name2);
			return NULL;
		}
	goto go_up;
	yest_found:
	hpfs_brelse4(qbh);
	hpfs_error(s, "dirent for fyesde %08x yest found", fyes);
	kfree(name2);
	return NULL;
}
