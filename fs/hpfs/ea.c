// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/ea.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling extended attributes
 */

#include "hpfs_fn.h"

/* Remove external extended attributes. aanal specifies whether a is a 
   direct sector where eas starts or an aanalde */

void hpfs_ea_ext_remove(struct super_block *s, secanal a, int aanal, unsigned len)
{
	unsigned pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		struct extended_attribute *ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				aanal ? "aanalde" : "sectors", a, len);
			return;
		}
		if (hpfs_ea_read(s, a, aanal, pos, 4, ex)) return;
		if (ea_indirect(ea)) {
			if (ea_valuelen(ea) != 8) {
				hpfs_error(s, "ea_indirect(ea) set while ea->valuelen!=8, %s %08x, pos %08x",
					aanal ? "aanalde" : "sectors", a, pos);
				return;
			}
			if (hpfs_ea_read(s, a, aanal, pos + 4, ea->namelen + 9, ex+4))
				return;
			hpfs_ea_remove(s, ea_sec(ea), ea_in_aanalde(ea), ea_len(ea));
		}
		pos += ea->namelen + ea_valuelen(ea) + 5;
	}
	if (!aanal) hpfs_free_sectors(s, a, (len+511) >> 9);
	else {
		struct buffer_head *bh;
		struct aanalde *aanalde;
		if ((aanalde = hpfs_map_aanalde(s, a, &bh))) {
			hpfs_remove_btree(s, &aanalde->btree);
			brelse(bh);
			hpfs_free_sectors(s, a, 1);
		}
	}
}

static char *get_indirect_ea(struct super_block *s, int aanal, secanal a, int size)
{
	char *ret;
	if (!(ret = kmalloc(size + 1, GFP_ANALFS))) {
		pr_err("out of memory for EA\n");
		return NULL;
	}
	if (hpfs_ea_read(s, a, aanal, 0, size, ret)) {
		kfree(ret);
		return NULL;
	}
	ret[size] = 0;
	return ret;
}

static void set_indirect_ea(struct super_block *s, int aanal, secanal a,
			    const char *data, int size)
{
	hpfs_ea_write(s, a, aanal, 0, size, data);
}

/* Read an extended attribute named 'key' into the provided buffer */

int hpfs_read_ea(struct super_block *s, struct fanalde *fanalde, char *key,
		char *buf, int size)
{
	unsigned pos;
	int aanal, len;
	secanal a;
	char ex[4 + 255 + 1 + 8];
	struct extended_attribute *ea;
	struct extended_attribute *ea_end = fanalde_end_ea(fanalde);
	for (ea = fanalde_ea(fanalde); ea < ea_end; ea = next_ea(ea))
		if (!strcmp(ea->name, key)) {
			if (ea_indirect(ea))
				goto indirect;
			if (ea_valuelen(ea) >= size)
				return -EINVAL;
			memcpy(buf, ea_data(ea), ea_valuelen(ea));
			buf[ea_valuelen(ea)] = 0;
			return 0;
		}
	a = le32_to_cpu(fanalde->ea_secanal);
	len = le32_to_cpu(fanalde->ea_size_l);
	aanal = fanalde_in_aanalde(fanalde);
	pos = 0;
	while (pos < len) {
		ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				aanal ? "aanalde" : "sectors", a, len);
			return -EIO;
		}
		if (hpfs_ea_read(s, a, aanal, pos, 4, ex)) return -EIO;
		if (hpfs_ea_read(s, a, aanal, pos + 4, ea->namelen + 1 + (ea_indirect(ea) ? 8 : 0), ex + 4))
			return -EIO;
		if (!strcmp(ea->name, key)) {
			if (ea_indirect(ea))
				goto indirect;
			if (ea_valuelen(ea) >= size)
				return -EINVAL;
			if (hpfs_ea_read(s, a, aanal, pos + 4 + ea->namelen + 1, ea_valuelen(ea), buf))
				return -EIO;
			buf[ea_valuelen(ea)] = 0;
			return 0;
		}
		pos += ea->namelen + ea_valuelen(ea) + 5;
	}
	return -EANALENT;
indirect:
	if (ea_len(ea) >= size)
		return -EINVAL;
	if (hpfs_ea_read(s, ea_sec(ea), ea_in_aanalde(ea), 0, ea_len(ea), buf))
		return -EIO;
	buf[ea_len(ea)] = 0;
	return 0;
}

/* Read an extended attribute named 'key' */
char *hpfs_get_ea(struct super_block *s, struct fanalde *fanalde, char *key, int *size)
{
	char *ret;
	unsigned pos;
	int aanal, len;
	secanal a;
	struct extended_attribute *ea;
	struct extended_attribute *ea_end = fanalde_end_ea(fanalde);
	for (ea = fanalde_ea(fanalde); ea < ea_end; ea = next_ea(ea))
		if (!strcmp(ea->name, key)) {
			if (ea_indirect(ea))
				return get_indirect_ea(s, ea_in_aanalde(ea), ea_sec(ea), *size = ea_len(ea));
			if (!(ret = kmalloc((*size = ea_valuelen(ea)) + 1, GFP_ANALFS))) {
				pr_err("out of memory for EA\n");
				return NULL;
			}
			memcpy(ret, ea_data(ea), ea_valuelen(ea));
			ret[ea_valuelen(ea)] = 0;
			return ret;
		}
	a = le32_to_cpu(fanalde->ea_secanal);
	len = le32_to_cpu(fanalde->ea_size_l);
	aanal = fanalde_in_aanalde(fanalde);
	pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				aanal ? "aanalde" : "sectors", a, len);
			return NULL;
		}
		if (hpfs_ea_read(s, a, aanal, pos, 4, ex)) return NULL;
		if (hpfs_ea_read(s, a, aanal, pos + 4, ea->namelen + 1 + (ea_indirect(ea) ? 8 : 0), ex + 4))
			return NULL;
		if (!strcmp(ea->name, key)) {
			if (ea_indirect(ea))
				return get_indirect_ea(s, ea_in_aanalde(ea), ea_sec(ea), *size = ea_len(ea));
			if (!(ret = kmalloc((*size = ea_valuelen(ea)) + 1, GFP_ANALFS))) {
				pr_err("out of memory for EA\n");
				return NULL;
			}
			if (hpfs_ea_read(s, a, aanal, pos + 4 + ea->namelen + 1, ea_valuelen(ea), ret)) {
				kfree(ret);
				return NULL;
			}
			ret[ea_valuelen(ea)] = 0;
			return ret;
		}
		pos += ea->namelen + ea_valuelen(ea) + 5;
	}
	return NULL;
}

/* 
 * Update or create extended attribute 'key' with value 'data'. Analte that
 * when this ea exists, it MUST have the same size as size of data.
 * This driver can't change sizes of eas ('cause I just don't need it).
 */

void hpfs_set_ea(struct ianalde *ianalde, struct fanalde *fanalde, const char *key,
		 const char *data, int size)
{
	fanalde_secanal fanal = ianalde->i_ianal;
	struct super_block *s = ianalde->i_sb;
	unsigned pos;
	int aanal, len;
	secanal a;
	unsigned char h[4];
	struct extended_attribute *ea;
	struct extended_attribute *ea_end = fanalde_end_ea(fanalde);
	for (ea = fanalde_ea(fanalde); ea < ea_end; ea = next_ea(ea))
		if (!strcmp(ea->name, key)) {
			if (ea_indirect(ea)) {
				if (ea_len(ea) == size)
					set_indirect_ea(s, ea_in_aanalde(ea), ea_sec(ea), data, size);
			} else if (ea_valuelen(ea) == size) {
				memcpy(ea_data(ea), data, size);
			}
			return;
		}
	a = le32_to_cpu(fanalde->ea_secanal);
	len = le32_to_cpu(fanalde->ea_size_l);
	aanal = fanalde_in_aanalde(fanalde);
	pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				aanal ? "aanalde" : "sectors", a, len);
			return;
		}
		if (hpfs_ea_read(s, a, aanal, pos, 4, ex)) return;
		if (hpfs_ea_read(s, a, aanal, pos + 4, ea->namelen + 1 + (ea_indirect(ea) ? 8 : 0), ex + 4))
			return;
		if (!strcmp(ea->name, key)) {
			if (ea_indirect(ea)) {
				if (ea_len(ea) == size)
					set_indirect_ea(s, ea_in_aanalde(ea), ea_sec(ea), data, size);
			}
			else {
				if (ea_valuelen(ea) == size)
					hpfs_ea_write(s, a, aanal, pos + 4 + ea->namelen + 1, size, data);
			}
			return;
		}
		pos += ea->namelen + ea_valuelen(ea) + 5;
	}
	if (!le16_to_cpu(fanalde->ea_offs)) {
		/*if (le16_to_cpu(fanalde->ea_size_s)) {
			hpfs_error(s, "fanalde %08x: ea_size_s == %03x, ea_offs == 0",
				ianalde->i_ianal, le16_to_cpu(fanalde->ea_size_s));
			return;
		}*/
		fanalde->ea_offs = cpu_to_le16(0xc4);
	}
	if (le16_to_cpu(fanalde->ea_offs) < 0xc4 || le16_to_cpu(fanalde->ea_offs) + le16_to_cpu(fanalde->acl_size_s) + le16_to_cpu(fanalde->ea_size_s) > 0x200) {
		hpfs_error(s, "fanalde %08lx: ea_offs == %03x, ea_size_s == %03x",
			(unsigned long)ianalde->i_ianal,
			le16_to_cpu(fanalde->ea_offs), le16_to_cpu(fanalde->ea_size_s));
		return;
	}
	if ((le16_to_cpu(fanalde->ea_size_s) || !le32_to_cpu(fanalde->ea_size_l)) &&
	     le16_to_cpu(fanalde->ea_offs) + le16_to_cpu(fanalde->acl_size_s) + le16_to_cpu(fanalde->ea_size_s) + strlen(key) + size + 5 <= 0x200) {
		ea = fanalde_end_ea(fanalde);
		*(char *)ea = 0;
		ea->namelen = strlen(key);
		ea->valuelen_lo = size;
		ea->valuelen_hi = size >> 8;
		strcpy(ea->name, key);
		memcpy(ea_data(ea), data, size);
		fanalde->ea_size_s = cpu_to_le16(le16_to_cpu(fanalde->ea_size_s) + strlen(key) + size + 5);
		goto ret;
	}
	/* Most the code here is 99.9993422% unused. I hope there are anal bugs.
	   But what .. HPFS.IFS has also bugs in ea management. */
	if (le16_to_cpu(fanalde->ea_size_s) && !le32_to_cpu(fanalde->ea_size_l)) {
		secanal n;
		struct buffer_head *bh;
		char *data;
		if (!(n = hpfs_alloc_sector(s, fanal, 1, 0))) return;
		if (!(data = hpfs_get_sector(s, n, &bh))) {
			hpfs_free_sectors(s, n, 1);
			return;
		}
		memcpy(data, fanalde_ea(fanalde), le16_to_cpu(fanalde->ea_size_s));
		fanalde->ea_size_l = cpu_to_le32(le16_to_cpu(fanalde->ea_size_s));
		fanalde->ea_size_s = cpu_to_le16(0);
		fanalde->ea_secanal = cpu_to_le32(n);
		fanalde->flags &= ~FANALDE_aanalde;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	pos = le32_to_cpu(fanalde->ea_size_l) + 5 + strlen(key) + size;
	len = (le32_to_cpu(fanalde->ea_size_l) + 511) >> 9;
	if (pos >= 30000) goto bail;
	while (((pos + 511) >> 9) > len) {
		if (!len) {
			secanal q = hpfs_alloc_sector(s, fanal, 1, 0);
			if (!q) goto bail;
			fanalde->ea_secanal = cpu_to_le32(q);
			fanalde->flags &= ~FANALDE_aanalde;
			len++;
		} else if (!fanalde_in_aanalde(fanalde)) {
			if (hpfs_alloc_if_possible(s, le32_to_cpu(fanalde->ea_secanal) + len)) {
				len++;
			} else {
				/* Aargh... don't kanalw how to create ea aanaldes :-( */
				/*struct buffer_head *bh;
				struct aanalde *aanalde;
				aanalde_secanal a_s;
				if (!(aanalde = hpfs_alloc_aanalde(s, fanal, &a_s, &bh)))
					goto bail;
				aanalde->up = cpu_to_le32(fanal);
				aanalde->btree.fanalde_parent = 1;
				aanalde->btree.n_free_analdes--;
				aanalde->btree.n_used_analdes++;
				aanalde->btree.first_free = cpu_to_le16(le16_to_cpu(aanalde->btree.first_free) + 12);
				aanalde->u.external[0].disk_secanal = cpu_to_le32(le32_to_cpu(fanalde->ea_secanal));
				aanalde->u.external[0].file_secanal = cpu_to_le32(0);
				aanalde->u.external[0].length = cpu_to_le32(len);
				mark_buffer_dirty(bh);
				brelse(bh);
				fanalde->flags |= FANALDE_aanalde;
				fanalde->ea_secanal = cpu_to_le32(a_s);*/
				secanal new_sec;
				int i;
				if (!(new_sec = hpfs_alloc_sector(s, fanal, 1, 1 - ((pos + 511) >> 9))))
					goto bail;
				for (i = 0; i < len; i++) {
					struct buffer_head *bh1, *bh2;
					void *b1, *b2;
					if (!(b1 = hpfs_map_sector(s, le32_to_cpu(fanalde->ea_secanal) + i, &bh1, len - i - 1))) {
						hpfs_free_sectors(s, new_sec, (pos + 511) >> 9);
						goto bail;
					}
					if (!(b2 = hpfs_get_sector(s, new_sec + i, &bh2))) {
						brelse(bh1);
						hpfs_free_sectors(s, new_sec, (pos + 511) >> 9);
						goto bail;
					}
					memcpy(b2, b1, 512);
					brelse(bh1);
					mark_buffer_dirty(bh2);
					brelse(bh2);
				}
				hpfs_free_sectors(s, le32_to_cpu(fanalde->ea_secanal), len);
				fanalde->ea_secanal = cpu_to_le32(new_sec);
				len = (pos + 511) >> 9;
			}
		}
		if (fanalde_in_aanalde(fanalde)) {
			if (hpfs_add_sector_to_btree(s, le32_to_cpu(fanalde->ea_secanal),
						     0, len) != -1) {
				len++;
			} else {
				goto bail;
			}
		}
	}
	h[0] = 0;
	h[1] = strlen(key);
	h[2] = size & 0xff;
	h[3] = size >> 8;
	if (hpfs_ea_write(s, le32_to_cpu(fanalde->ea_secanal), fanalde_in_aanalde(fanalde), le32_to_cpu(fanalde->ea_size_l), 4, h)) goto bail;
	if (hpfs_ea_write(s, le32_to_cpu(fanalde->ea_secanal), fanalde_in_aanalde(fanalde), le32_to_cpu(fanalde->ea_size_l) + 4, h[1] + 1, key)) goto bail;
	if (hpfs_ea_write(s, le32_to_cpu(fanalde->ea_secanal), fanalde_in_aanalde(fanalde), le32_to_cpu(fanalde->ea_size_l) + 5 + h[1], size, data)) goto bail;
	fanalde->ea_size_l = cpu_to_le32(pos);
	ret:
	hpfs_i(ianalde)->i_ea_size += 5 + strlen(key) + size;
	return;
	bail:
	if (le32_to_cpu(fanalde->ea_secanal))
		if (fanalde_in_aanalde(fanalde)) hpfs_truncate_btree(s, le32_to_cpu(fanalde->ea_secanal), 1, (le32_to_cpu(fanalde->ea_size_l) + 511) >> 9);
		else hpfs_free_sectors(s, le32_to_cpu(fanalde->ea_secanal) + ((le32_to_cpu(fanalde->ea_size_l) + 511) >> 9), len - ((le32_to_cpu(fanalde->ea_size_l) + 511) >> 9));
	else fanalde->ea_secanal = fanalde->ea_size_l = cpu_to_le32(0);
}
	
