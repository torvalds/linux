/*
 *  linux/fs/hpfs/ea.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  handling extended attributes
 */

#include "hpfs_fn.h"

/* Remove external extended attributes. ano specifies whether a is a 
   direct sector where eas starts or an anode */

void hpfs_ea_ext_remove(struct super_block *s, secno a, int ano, unsigned len)
{
	unsigned pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		struct extended_attribute *ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				ano ? "anode" : "sectors", a, len);
			return;
		}
		if (hpfs_ea_read(s, a, ano, pos, 4, ex)) return;
		if (ea->indirect) {
			if (ea->valuelen != 8) {
				hpfs_error(s, "ea->indirect set while ea->valuelen!=8, %s %08x, pos %08x",
					ano ? "anode" : "sectors", a, pos);
				return;
			}
			if (hpfs_ea_read(s, a, ano, pos + 4, ea->namelen + 9, ex+4))
				return;
			hpfs_ea_remove(s, ea_sec(ea), ea->anode, ea_len(ea));
		}
		pos += ea->namelen + ea->valuelen + 5;
	}
	if (!ano) hpfs_free_sectors(s, a, (len+511) >> 9);
	else {
		struct buffer_head *bh;
		struct anode *anode;
		if ((anode = hpfs_map_anode(s, a, &bh))) {
			hpfs_remove_btree(s, &anode->btree);
			brelse(bh);
			hpfs_free_sectors(s, a, 1);
		}
	}
}

static char *get_indirect_ea(struct super_block *s, int ano, secno a, int size)
{
	char *ret;
	if (!(ret = kmalloc(size + 1, GFP_NOFS))) {
		printk("HPFS: out of memory for EA\n");
		return NULL;
	}
	if (hpfs_ea_read(s, a, ano, 0, size, ret)) {
		kfree(ret);
		return NULL;
	}
	ret[size] = 0;
	return ret;
}

static void set_indirect_ea(struct super_block *s, int ano, secno a, char *data,
			    int size)
{
	hpfs_ea_write(s, a, ano, 0, size, data);
}

/* Read an extended attribute named 'key' into the provided buffer */

int hpfs_read_ea(struct super_block *s, struct fnode *fnode, char *key,
		char *buf, int size)
{
	unsigned pos;
	int ano, len;
	secno a;
	struct extended_attribute *ea;
	struct extended_attribute *ea_end = fnode_end_ea(fnode);
	for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
		if (!strcmp(ea->name, key)) {
			if (ea->indirect)
				goto indirect;
			if (ea->valuelen >= size)
				return -EINVAL;
			memcpy(buf, ea_data(ea), ea->valuelen);
			buf[ea->valuelen] = 0;
			return 0;
		}
	a = fnode->ea_secno;
	len = fnode->ea_size_l;
	ano = fnode->ea_anode;
	pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				ano ? "anode" : "sectors", a, len);
			return -EIO;
		}
		if (hpfs_ea_read(s, a, ano, pos, 4, ex)) return -EIO;
		if (hpfs_ea_read(s, a, ano, pos + 4, ea->namelen + 1 + (ea->indirect ? 8 : 0), ex + 4))
			return -EIO;
		if (!strcmp(ea->name, key)) {
			if (ea->indirect)
				goto indirect;
			if (ea->valuelen >= size)
				return -EINVAL;
			if (hpfs_ea_read(s, a, ano, pos + 4 + ea->namelen + 1, ea->valuelen, buf))
				return -EIO;
			buf[ea->valuelen] = 0;
			return 0;
		}
		pos += ea->namelen + ea->valuelen + 5;
	}
	return -ENOENT;
indirect:
	if (ea_len(ea) >= size)
		return -EINVAL;
	if (hpfs_ea_read(s, ea_sec(ea), ea->anode, 0, ea_len(ea), buf))
		return -EIO;
	buf[ea_len(ea)] = 0;
	return 0;
}

/* Read an extended attribute named 'key' */
char *hpfs_get_ea(struct super_block *s, struct fnode *fnode, char *key, int *size)
{
	char *ret;
	unsigned pos;
	int ano, len;
	secno a;
	struct extended_attribute *ea;
	struct extended_attribute *ea_end = fnode_end_ea(fnode);
	for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
		if (!strcmp(ea->name, key)) {
			if (ea->indirect)
				return get_indirect_ea(s, ea->anode, ea_sec(ea), *size = ea_len(ea));
			if (!(ret = kmalloc((*size = ea->valuelen) + 1, GFP_NOFS))) {
				printk("HPFS: out of memory for EA\n");
				return NULL;
			}
			memcpy(ret, ea_data(ea), ea->valuelen);
			ret[ea->valuelen] = 0;
			return ret;
		}
	a = fnode->ea_secno;
	len = fnode->ea_size_l;
	ano = fnode->ea_anode;
	pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				ano ? "anode" : "sectors", a, len);
			return NULL;
		}
		if (hpfs_ea_read(s, a, ano, pos, 4, ex)) return NULL;
		if (hpfs_ea_read(s, a, ano, pos + 4, ea->namelen + 1 + (ea->indirect ? 8 : 0), ex + 4))
			return NULL;
		if (!strcmp(ea->name, key)) {
			if (ea->indirect)
				return get_indirect_ea(s, ea->anode, ea_sec(ea), *size = ea_len(ea));
			if (!(ret = kmalloc((*size = ea->valuelen) + 1, GFP_NOFS))) {
				printk("HPFS: out of memory for EA\n");
				return NULL;
			}
			if (hpfs_ea_read(s, a, ano, pos + 4 + ea->namelen + 1, ea->valuelen, ret)) {
				kfree(ret);
				return NULL;
			}
			ret[ea->valuelen] = 0;
			return ret;
		}
		pos += ea->namelen + ea->valuelen + 5;
	}
	return NULL;
}

/* 
 * Update or create extended attribute 'key' with value 'data'. Note that
 * when this ea exists, it MUST have the same size as size of data.
 * This driver can't change sizes of eas ('cause I just don't need it).
 */

void hpfs_set_ea(struct inode *inode, struct fnode *fnode, char *key, char *data, int size)
{
	fnode_secno fno = inode->i_ino;
	struct super_block *s = inode->i_sb;
	unsigned pos;
	int ano, len;
	secno a;
	unsigned char h[4];
	struct extended_attribute *ea;
	struct extended_attribute *ea_end = fnode_end_ea(fnode);
	for (ea = fnode_ea(fnode); ea < ea_end; ea = next_ea(ea))
		if (!strcmp(ea->name, key)) {
			if (ea->indirect) {
				if (ea_len(ea) == size)
					set_indirect_ea(s, ea->anode, ea_sec(ea), data, size);
			} else if (ea->valuelen == size) {
				memcpy(ea_data(ea), data, size);
			}
			return;
		}
	a = fnode->ea_secno;
	len = fnode->ea_size_l;
	ano = fnode->ea_anode;
	pos = 0;
	while (pos < len) {
		char ex[4 + 255 + 1 + 8];
		ea = (struct extended_attribute *)ex;
		if (pos + 4 > len) {
			hpfs_error(s, "EAs don't end correctly, %s %08x, len %08x",
				ano ? "anode" : "sectors", a, len);
			return;
		}
		if (hpfs_ea_read(s, a, ano, pos, 4, ex)) return;
		if (hpfs_ea_read(s, a, ano, pos + 4, ea->namelen + 1 + (ea->indirect ? 8 : 0), ex + 4))
			return;
		if (!strcmp(ea->name, key)) {
			if (ea->indirect) {
				if (ea_len(ea) == size)
					set_indirect_ea(s, ea->anode, ea_sec(ea), data, size);
			}
			else {
				if (ea->valuelen == size)
					hpfs_ea_write(s, a, ano, pos + 4 + ea->namelen + 1, size, data);
			}
			return;
		}
		pos += ea->namelen + ea->valuelen + 5;
	}
	if (!fnode->ea_offs) {
		/*if (fnode->ea_size_s) {
			hpfs_error(s, "fnode %08x: ea_size_s == %03x, ea_offs == 0",
				inode->i_ino, fnode->ea_size_s);
			return;
		}*/
		fnode->ea_offs = 0xc4;
	}
	if (fnode->ea_offs < 0xc4 || fnode->ea_offs + fnode->acl_size_s + fnode->ea_size_s > 0x200) {
		hpfs_error(s, "fnode %08x: ea_offs == %03x, ea_size_s == %03x",
			inode->i_ino, fnode->ea_offs, fnode->ea_size_s);
		return;
	}
	if ((fnode->ea_size_s || !fnode->ea_size_l) &&
	     fnode->ea_offs + fnode->acl_size_s + fnode->ea_size_s + strlen(key) + size + 5 <= 0x200) {
		ea = fnode_end_ea(fnode);
		*(char *)ea = 0;
		ea->namelen = strlen(key);
		ea->valuelen = size;
		strcpy(ea->name, key);
		memcpy(ea_data(ea), data, size);
		fnode->ea_size_s += strlen(key) + size + 5;
		goto ret;
	}
	/* Most the code here is 99.9993422% unused. I hope there are no bugs.
	   But what .. HPFS.IFS has also bugs in ea management. */
	if (fnode->ea_size_s && !fnode->ea_size_l) {
		secno n;
		struct buffer_head *bh;
		char *data;
		if (!(n = hpfs_alloc_sector(s, fno, 1, 0, 1))) return;
		if (!(data = hpfs_get_sector(s, n, &bh))) {
			hpfs_free_sectors(s, n, 1);
			return;
		}
		memcpy(data, fnode_ea(fnode), fnode->ea_size_s);
		fnode->ea_size_l = fnode->ea_size_s;
		fnode->ea_size_s = 0;
		fnode->ea_secno = n;
		fnode->ea_anode = 0;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	pos = fnode->ea_size_l + 5 + strlen(key) + size;
	len = (fnode->ea_size_l + 511) >> 9;
	if (pos >= 30000) goto bail;
	while (((pos + 511) >> 9) > len) {
		if (!len) {
			if (!(fnode->ea_secno = hpfs_alloc_sector(s, fno, 1, 0, 1)))
				goto bail;
			fnode->ea_anode = 0;
			len++;
		} else if (!fnode->ea_anode) {
			if (hpfs_alloc_if_possible(s, fnode->ea_secno + len)) {
				len++;
			} else {
				/* Aargh... don't know how to create ea anodes :-( */
				/*struct buffer_head *bh;
				struct anode *anode;
				anode_secno a_s;
				if (!(anode = hpfs_alloc_anode(s, fno, &a_s, &bh)))
					goto bail;
				anode->up = fno;
				anode->btree.fnode_parent = 1;
				anode->btree.n_free_nodes--;
				anode->btree.n_used_nodes++;
				anode->btree.first_free += 12;
				anode->u.external[0].disk_secno = fnode->ea_secno;
				anode->u.external[0].file_secno = 0;
				anode->u.external[0].length = len;
				mark_buffer_dirty(bh);
				brelse(bh);
				fnode->ea_anode = 1;
				fnode->ea_secno = a_s;*/
				secno new_sec;
				int i;
				if (!(new_sec = hpfs_alloc_sector(s, fno, 1, 1 - ((pos + 511) >> 9), 1)))
					goto bail;
				for (i = 0; i < len; i++) {
					struct buffer_head *bh1, *bh2;
					void *b1, *b2;
					if (!(b1 = hpfs_map_sector(s, fnode->ea_secno + i, &bh1, len - i - 1))) {
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
				hpfs_free_sectors(s, fnode->ea_secno, len);
				fnode->ea_secno = new_sec;
				len = (pos + 511) >> 9;
			}
		}
		if (fnode->ea_anode) {
			if (hpfs_add_sector_to_btree(s, fnode->ea_secno,
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
	if (hpfs_ea_write(s, fnode->ea_secno, fnode->ea_anode, fnode->ea_size_l, 4, h)) goto bail;
	if (hpfs_ea_write(s, fnode->ea_secno, fnode->ea_anode, fnode->ea_size_l + 4, h[1] + 1, key)) goto bail;
	if (hpfs_ea_write(s, fnode->ea_secno, fnode->ea_anode, fnode->ea_size_l + 5 + h[1], size, data)) goto bail;
	fnode->ea_size_l = pos;
	ret:
	hpfs_i(inode)->i_ea_size += 5 + strlen(key) + size;
	return;
	bail:
	if (fnode->ea_secno)
		if (fnode->ea_anode) hpfs_truncate_btree(s, fnode->ea_secno, 1, (fnode->ea_size_l + 511) >> 9);
		else hpfs_free_sectors(s, fnode->ea_secno + ((fnode->ea_size_l + 511) >> 9), len - ((fnode->ea_size_l + 511) >> 9));
	else fnode->ea_secno = fnode->ea_size_l = 0;
}
	
