/*
 *  linux/fs/hpfs/map.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  mapping structures to memory with some minimal checks
 */

#include "hpfs_fn.h"

__le32 *hpfs_map_dnode_bitmap(struct super_block *s, struct quad_buffer_head *qbh)
{
	return hpfs_map_4sectors(s, hpfs_sb(s)->sb_dmap, qbh, 0);
}

__le32 *hpfs_map_bitmap(struct super_block *s, unsigned bmp_block,
			 struct quad_buffer_head *qbh, char *id)
{
	secno sec;
	if (hpfs_sb(s)->sb_chk) if (bmp_block * 16384 > hpfs_sb(s)->sb_fs_size) {
		hpfs_error(s, "hpfs_map_bitmap called with bad parameter: %08x at %s", bmp_block, id);
		return NULL;
	}
	sec = le32_to_cpu(hpfs_sb(s)->sb_bmp_dir[bmp_block]);
	if (!sec || sec > hpfs_sb(s)->sb_fs_size-4) {
		hpfs_error(s, "invalid bitmap block pointer %08x -> %08x at %s", bmp_block, sec, id);
		return NULL;
	}
	return hpfs_map_4sectors(s, sec, qbh, 4);
}

/*
 * Load first code page into kernel memory, return pointer to 256-byte array,
 * first 128 bytes are uppercasing table for chars 128-255, next 128 bytes are
 * lowercasing table
 */

unsigned char *hpfs_load_code_page(struct super_block *s, secno cps)
{
	struct buffer_head *bh;
	secno cpds;
	unsigned cpi;
	unsigned char *ptr;
	unsigned char *cp_table;
	int i;
	struct code_page_data *cpd;
	struct code_page_directory *cp = hpfs_map_sector(s, cps, &bh, 0);
	if (!cp) return NULL;
	if (le32_to_cpu(cp->magic) != CP_DIR_MAGIC) {
		printk("HPFS: Code page directory magic doesn't match (magic = %08x)\n", le32_to_cpu(cp->magic));
		brelse(bh);
		return NULL;
	}
	if (!le32_to_cpu(cp->n_code_pages)) {
		printk("HPFS: n_code_pages == 0\n");
		brelse(bh);
		return NULL;
	}
	cpds = le32_to_cpu(cp->array[0].code_page_data);
	cpi = le16_to_cpu(cp->array[0].index);
	brelse(bh);

	if (cpi >= 3) {
		printk("HPFS: Code page index out of array\n");
		return NULL;
	}
	
	if (!(cpd = hpfs_map_sector(s, cpds, &bh, 0))) return NULL;
	if (le16_to_cpu(cpd->offs[cpi]) > 0x178) {
		printk("HPFS: Code page index out of sector\n");
		brelse(bh);
		return NULL;
	}
	ptr = (unsigned char *)cpd + le16_to_cpu(cpd->offs[cpi]) + 6;
	if (!(cp_table = kmalloc(256, GFP_KERNEL))) {
		printk("HPFS: out of memory for code page table\n");
		brelse(bh);
		return NULL;
	}
	memcpy(cp_table, ptr, 128);
	brelse(bh);

	/* Try to build lowercasing table from uppercasing one */

	for (i=128; i<256; i++) cp_table[i]=i;
	for (i=128; i<256; i++) if (cp_table[i-128]!=i && cp_table[i-128]>=128)
		cp_table[cp_table[i-128]] = i;
	
	return cp_table;
}

__le32 *hpfs_load_bitmap_directory(struct super_block *s, secno bmp)
{
	struct buffer_head *bh;
	int n = (hpfs_sb(s)->sb_fs_size + 0x200000 - 1) >> 21;
	int i;
	__le32 *b;
	if (!(b = kmalloc(n * 512, GFP_KERNEL))) {
		printk("HPFS: can't allocate memory for bitmap directory\n");
		return NULL;
	}	
	for (i=0;i<n;i++) {
		__le32 *d = hpfs_map_sector(s, bmp+i, &bh, n - i - 1);
		if (!d) {
			kfree(b);
			return NULL;
		}
		memcpy((char *)b + 512 * i, d, 512);
		brelse(bh);
	}
	return b;
}

/*
 * Load fnode to memory
 */

struct fnode *hpfs_map_fnode(struct super_block *s, ino_t ino, struct buffer_head **bhp)
{
	struct fnode *fnode;
	if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, ino, 1, "fnode")) {
		return NULL;
	}
	if ((fnode = hpfs_map_sector(s, ino, bhp, FNODE_RD_AHEAD))) {
		if (hpfs_sb(s)->sb_chk) {
			struct extended_attribute *ea;
			struct extended_attribute *ea_end;
			if (le32_to_cpu(fnode->magic) != FNODE_MAGIC) {
				hpfs_error(s, "bad magic on fnode %08lx",
					(unsigned long)ino);
				goto bail;
			}
			if (!fnode_is_dir(fnode)) {
				if ((unsigned)fnode->btree.n_used_nodes + (unsigned)fnode->btree.n_free_nodes !=
				    (bp_internal(&fnode->btree) ? 12 : 8)) {
					hpfs_error(s,
					   "bad number of nodes in fnode %08lx",
					    (unsigned long)ino);
					goto bail;
				}
				if (le16_to_cpu(fnode->btree.first_free) !=
				    8 + fnode->btree.n_used_nodes * (bp_internal(&fnode->btree) ? 8 : 12)) {
					hpfs_error(s,
					    "bad first_free pointer in fnode %08lx",
					    (unsigned long)ino);
					goto bail;
				}
			}
			if (le16_to_cpu(fnode->ea_size_s) && (le16_to_cpu(fnode->ea_offs) < 0xc4 ||
			   le16_to_cpu(fnode->ea_offs) + le16_to_cpu(fnode->acl_size_s) + le16_to_cpu(fnode->ea_size_s) > 0x200)) {
				hpfs_error(s,
					"bad EA info in fnode %08lx: ea_offs == %04x ea_size_s == %04x",
					(unsigned long)ino,
					le16_to_cpu(fnode->ea_offs), le16_to_cpu(fnode->ea_size_s));
				goto bail;
			}
			ea = fnode_ea(fnode);
			ea_end = fnode_end_ea(fnode);
			while (ea != ea_end) {
				if (ea > ea_end) {
					hpfs_error(s, "bad EA in fnode %08lx",
						(unsigned long)ino);
					goto bail;
				}
				ea = next_ea(ea);
			}
		}
	}
	return fnode;
	bail:
	brelse(*bhp);
	return NULL;
}

struct anode *hpfs_map_anode(struct super_block *s, anode_secno ano, struct buffer_head **bhp)
{
	struct anode *anode;
	if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, ano, 1, "anode")) return NULL;
	if ((anode = hpfs_map_sector(s, ano, bhp, ANODE_RD_AHEAD)))
		if (hpfs_sb(s)->sb_chk) {
			if (le32_to_cpu(anode->magic) != ANODE_MAGIC) {
				hpfs_error(s, "bad magic on anode %08x", ano);
				goto bail;
			}
			if (le32_to_cpu(anode->self) != ano) {
				hpfs_error(s, "self pointer invalid on anode %08x", ano);
				goto bail;
			}
			if ((unsigned)anode->btree.n_used_nodes + (unsigned)anode->btree.n_free_nodes !=
			    (bp_internal(&anode->btree) ? 60 : 40)) {
				hpfs_error(s, "bad number of nodes in anode %08x", ano);
				goto bail;
			}
			if (le16_to_cpu(anode->btree.first_free) !=
			    8 + anode->btree.n_used_nodes * (bp_internal(&anode->btree) ? 8 : 12)) {
				hpfs_error(s, "bad first_free pointer in anode %08x", ano);
				goto bail;
			}
		}
	return anode;
	bail:
	brelse(*bhp);
	return NULL;
}

/*
 * Load dnode to memory and do some checks
 */

struct dnode *hpfs_map_dnode(struct super_block *s, unsigned secno,
			     struct quad_buffer_head *qbh)
{
	struct dnode *dnode;
	if (hpfs_sb(s)->sb_chk) {
		if (hpfs_chk_sectors(s, secno, 4, "dnode")) return NULL;
		if (secno & 3) {
			hpfs_error(s, "dnode %08x not byte-aligned", secno);
			return NULL;
		}	
	}
	if ((dnode = hpfs_map_4sectors(s, secno, qbh, DNODE_RD_AHEAD)))
		if (hpfs_sb(s)->sb_chk) {
			unsigned p, pp = 0;
			unsigned char *d = (unsigned char *)dnode;
			int b = 0;
			if (le32_to_cpu(dnode->magic) != DNODE_MAGIC) {
				hpfs_error(s, "bad magic on dnode %08x", secno);
				goto bail;
			}
			if (le32_to_cpu(dnode->self) != secno)
				hpfs_error(s, "bad self pointer on dnode %08x self = %08x", secno, le32_to_cpu(dnode->self));
			/* Check dirents - bad dirents would cause infinite
			   loops or shooting to memory */
			if (le32_to_cpu(dnode->first_free) > 2048) {
				hpfs_error(s, "dnode %08x has first_free == %08x", secno, le32_to_cpu(dnode->first_free));
				goto bail;
			}
			for (p = 20; p < le32_to_cpu(dnode->first_free); p += d[p] + (d[p+1] << 8)) {
				struct hpfs_dirent *de = (struct hpfs_dirent *)((char *)dnode + p);
				if (le16_to_cpu(de->length) > 292 || (le16_to_cpu(de->length) < 32) || (le16_to_cpu(de->length) & 3) || p + le16_to_cpu(de->length) > 2048) {
					hpfs_error(s, "bad dirent size in dnode %08x, dirent %03x, last %03x", secno, p, pp);
					goto bail;
				}
				if (((31 + de->namelen + de->down*4 + 3) & ~3) != le16_to_cpu(de->length)) {
					if (((31 + de->namelen + de->down*4 + 3) & ~3) < le16_to_cpu(de->length) && s->s_flags & MS_RDONLY) goto ok;
					hpfs_error(s, "namelen does not match dirent size in dnode %08x, dirent %03x, last %03x", secno, p, pp);
					goto bail;
				}
				ok:
				if (hpfs_sb(s)->sb_chk >= 2) b |= 1 << de->down;
				if (de->down) if (de_down_pointer(de) < 0x10) {
					hpfs_error(s, "bad down pointer in dnode %08x, dirent %03x, last %03x", secno, p, pp);
					goto bail;
				}
				pp = p;
				
			}
			if (p != le32_to_cpu(dnode->first_free)) {
				hpfs_error(s, "size on last dirent does not match first_free; dnode %08x", secno);
				goto bail;
			}
			if (d[pp + 30] != 1 || d[pp + 31] != 255) {
				hpfs_error(s, "dnode %08x does not end with \\377 entry", secno);
				goto bail;
			}
			if (b == 3) printk("HPFS: warning: unbalanced dnode tree, dnode %08x; see hpfs.txt 4 more info\n", secno);
		}
	return dnode;
	bail:
	hpfs_brelse4(qbh);
	return NULL;
}

dnode_secno hpfs_fnode_dno(struct super_block *s, ino_t ino)
{
	struct buffer_head *bh;
	struct fnode *fnode;
	dnode_secno dno;

	fnode = hpfs_map_fnode(s, ino, &bh);
	if (!fnode)
		return 0;

	dno = le32_to_cpu(fnode->u.external[0].disk_secno);
	brelse(bh);
	return dno;
}
