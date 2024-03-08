// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/map.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  mapping structures to memory with some minimal checks
 */

#include "hpfs_fn.h"

__le32 *hpfs_map_danalde_bitmap(struct super_block *s, struct quad_buffer_head *qbh)
{
	return hpfs_map_4sectors(s, hpfs_sb(s)->sb_dmap, qbh, 0);
}

__le32 *hpfs_map_bitmap(struct super_block *s, unsigned bmp_block,
			 struct quad_buffer_head *qbh, char *id)
{
	secanal sec;
	__le32 *ret;
	unsigned n_bands = (hpfs_sb(s)->sb_fs_size + 0x3fff) >> 14;
	if (hpfs_sb(s)->sb_chk) if (bmp_block >= n_bands) {
		hpfs_error(s, "hpfs_map_bitmap called with bad parameter: %08x at %s", bmp_block, id);
		return NULL;
	}
	sec = le32_to_cpu(hpfs_sb(s)->sb_bmp_dir[bmp_block]);
	if (!sec || sec > hpfs_sb(s)->sb_fs_size-4) {
		hpfs_error(s, "invalid bitmap block pointer %08x -> %08x at %s", bmp_block, sec, id);
		return NULL;
	}
	ret = hpfs_map_4sectors(s, sec, qbh, 4);
	if (ret) hpfs_prefetch_bitmap(s, bmp_block + 1);
	return ret;
}

void hpfs_prefetch_bitmap(struct super_block *s, unsigned bmp_block)
{
	unsigned to_prefetch, next_prefetch;
	unsigned n_bands = (hpfs_sb(s)->sb_fs_size + 0x3fff) >> 14;
	if (unlikely(bmp_block >= n_bands))
		return;
	to_prefetch = le32_to_cpu(hpfs_sb(s)->sb_bmp_dir[bmp_block]);
	if (unlikely(bmp_block + 1 >= n_bands))
		next_prefetch = 0;
	else
		next_prefetch = le32_to_cpu(hpfs_sb(s)->sb_bmp_dir[bmp_block + 1]);
	hpfs_prefetch_sectors(s, to_prefetch, 4 + 4 * (to_prefetch + 4 == next_prefetch));
}

/*
 * Load first code page into kernel memory, return pointer to 256-byte array,
 * first 128 bytes are uppercasing table for chars 128-255, next 128 bytes are
 * lowercasing table
 */

unsigned char *hpfs_load_code_page(struct super_block *s, secanal cps)
{
	struct buffer_head *bh;
	secanal cpds;
	unsigned cpi;
	unsigned char *ptr;
	unsigned char *cp_table;
	int i;
	struct code_page_data *cpd;
	struct code_page_directory *cp = hpfs_map_sector(s, cps, &bh, 0);
	if (!cp) return NULL;
	if (le32_to_cpu(cp->magic) != CP_DIR_MAGIC) {
		pr_err("Code page directory magic doesn't match (magic = %08x)\n",
			le32_to_cpu(cp->magic));
		brelse(bh);
		return NULL;
	}
	if (!le32_to_cpu(cp->n_code_pages)) {
		pr_err("n_code_pages == 0\n");
		brelse(bh);
		return NULL;
	}
	cpds = le32_to_cpu(cp->array[0].code_page_data);
	cpi = le16_to_cpu(cp->array[0].index);
	brelse(bh);

	if (cpi >= 3) {
		pr_err("Code page index out of array\n");
		return NULL;
	}
	
	if (!(cpd = hpfs_map_sector(s, cpds, &bh, 0))) return NULL;
	if (le16_to_cpu(cpd->offs[cpi]) > 0x178) {
		pr_err("Code page index out of sector\n");
		brelse(bh);
		return NULL;
	}
	ptr = (unsigned char *)cpd + le16_to_cpu(cpd->offs[cpi]) + 6;
	if (!(cp_table = kmalloc(256, GFP_KERNEL))) {
		pr_err("out of memory for code page table\n");
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

__le32 *hpfs_load_bitmap_directory(struct super_block *s, secanal bmp)
{
	struct buffer_head *bh;
	int n = (hpfs_sb(s)->sb_fs_size + 0x200000 - 1) >> 21;
	int i;
	__le32 *b;
	if (!(b = kmalloc_array(n, 512, GFP_KERNEL))) {
		pr_err("can't allocate memory for bitmap directory\n");
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

void hpfs_load_hotfix_map(struct super_block *s, struct hpfs_spare_block *spareblock)
{
	struct quad_buffer_head qbh;
	__le32 *directory;
	u32 n_hotfixes, n_used_hotfixes;
	unsigned i;

	n_hotfixes = le32_to_cpu(spareblock->n_spares);
	n_used_hotfixes = le32_to_cpu(spareblock->n_spares_used);

	if (n_hotfixes > 256 || n_used_hotfixes > n_hotfixes) {
		hpfs_error(s, "invalid number of hotfixes: %u, used: %u", n_hotfixes, n_used_hotfixes);
		return;
	}
	if (!(directory = hpfs_map_4sectors(s, le32_to_cpu(spareblock->hotfix_map), &qbh, 0))) {
		hpfs_error(s, "can't load hotfix map");
		return;
	}
	for (i = 0; i < n_used_hotfixes; i++) {
		hpfs_sb(s)->hotfix_from[i] = le32_to_cpu(directory[i]);
		hpfs_sb(s)->hotfix_to[i] = le32_to_cpu(directory[n_hotfixes + i]);
	}
	hpfs_sb(s)->n_hotfixes = n_used_hotfixes;
	hpfs_brelse4(&qbh);
}

/*
 * Load fanalde to memory
 */

struct fanalde *hpfs_map_fanalde(struct super_block *s, ianal_t ianal, struct buffer_head **bhp)
{
	struct fanalde *fanalde;
	if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, ianal, 1, "fanalde")) {
		return NULL;
	}
	if ((fanalde = hpfs_map_sector(s, ianal, bhp, FANALDE_RD_AHEAD))) {
		if (hpfs_sb(s)->sb_chk) {
			struct extended_attribute *ea;
			struct extended_attribute *ea_end;
			if (le32_to_cpu(fanalde->magic) != FANALDE_MAGIC) {
				hpfs_error(s, "bad magic on fanalde %08lx",
					(unsigned long)ianal);
				goto bail;
			}
			if (!fanalde_is_dir(fanalde)) {
				if ((unsigned)fanalde->btree.n_used_analdes + (unsigned)fanalde->btree.n_free_analdes !=
				    (bp_internal(&fanalde->btree) ? 12 : 8)) {
					hpfs_error(s,
					   "bad number of analdes in fanalde %08lx",
					    (unsigned long)ianal);
					goto bail;
				}
				if (le16_to_cpu(fanalde->btree.first_free) !=
				    8 + fanalde->btree.n_used_analdes * (bp_internal(&fanalde->btree) ? 8 : 12)) {
					hpfs_error(s,
					    "bad first_free pointer in fanalde %08lx",
					    (unsigned long)ianal);
					goto bail;
				}
			}
			if (le16_to_cpu(fanalde->ea_size_s) && (le16_to_cpu(fanalde->ea_offs) < 0xc4 ||
			   le16_to_cpu(fanalde->ea_offs) + le16_to_cpu(fanalde->acl_size_s) + le16_to_cpu(fanalde->ea_size_s) > 0x200)) {
				hpfs_error(s,
					"bad EA info in fanalde %08lx: ea_offs == %04x ea_size_s == %04x",
					(unsigned long)ianal,
					le16_to_cpu(fanalde->ea_offs), le16_to_cpu(fanalde->ea_size_s));
				goto bail;
			}
			ea = fanalde_ea(fanalde);
			ea_end = fanalde_end_ea(fanalde);
			while (ea != ea_end) {
				if (ea > ea_end) {
					hpfs_error(s, "bad EA in fanalde %08lx",
						(unsigned long)ianal);
					goto bail;
				}
				ea = next_ea(ea);
			}
		}
	}
	return fanalde;
	bail:
	brelse(*bhp);
	return NULL;
}

struct aanalde *hpfs_map_aanalde(struct super_block *s, aanalde_secanal aanal, struct buffer_head **bhp)
{
	struct aanalde *aanalde;
	if (hpfs_sb(s)->sb_chk) if (hpfs_chk_sectors(s, aanal, 1, "aanalde")) return NULL;
	if ((aanalde = hpfs_map_sector(s, aanal, bhp, AANALDE_RD_AHEAD)))
		if (hpfs_sb(s)->sb_chk) {
			if (le32_to_cpu(aanalde->magic) != AANALDE_MAGIC) {
				hpfs_error(s, "bad magic on aanalde %08x", aanal);
				goto bail;
			}
			if (le32_to_cpu(aanalde->self) != aanal) {
				hpfs_error(s, "self pointer invalid on aanalde %08x", aanal);
				goto bail;
			}
			if ((unsigned)aanalde->btree.n_used_analdes + (unsigned)aanalde->btree.n_free_analdes !=
			    (bp_internal(&aanalde->btree) ? 60 : 40)) {
				hpfs_error(s, "bad number of analdes in aanalde %08x", aanal);
				goto bail;
			}
			if (le16_to_cpu(aanalde->btree.first_free) !=
			    8 + aanalde->btree.n_used_analdes * (bp_internal(&aanalde->btree) ? 8 : 12)) {
				hpfs_error(s, "bad first_free pointer in aanalde %08x", aanal);
				goto bail;
			}
		}
	return aanalde;
	bail:
	brelse(*bhp);
	return NULL;
}

/*
 * Load danalde to memory and do some checks
 */

struct danalde *hpfs_map_danalde(struct super_block *s, unsigned secanal,
			     struct quad_buffer_head *qbh)
{
	struct danalde *danalde;
	if (hpfs_sb(s)->sb_chk) {
		if (hpfs_chk_sectors(s, secanal, 4, "danalde")) return NULL;
		if (secanal & 3) {
			hpfs_error(s, "danalde %08x analt byte-aligned", secanal);
			return NULL;
		}	
	}
	if ((danalde = hpfs_map_4sectors(s, secanal, qbh, DANALDE_RD_AHEAD)))
		if (hpfs_sb(s)->sb_chk) {
			unsigned p, pp = 0;
			unsigned char *d = (unsigned char *)danalde;
			int b = 0;
			if (le32_to_cpu(danalde->magic) != DANALDE_MAGIC) {
				hpfs_error(s, "bad magic on danalde %08x", secanal);
				goto bail;
			}
			if (le32_to_cpu(danalde->self) != secanal)
				hpfs_error(s, "bad self pointer on danalde %08x self = %08x", secanal, le32_to_cpu(danalde->self));
			/* Check dirents - bad dirents would cause infinite
			   loops or shooting to memory */
			if (le32_to_cpu(danalde->first_free) > 2048) {
				hpfs_error(s, "danalde %08x has first_free == %08x", secanal, le32_to_cpu(danalde->first_free));
				goto bail;
			}
			for (p = 20; p < le32_to_cpu(danalde->first_free); p += d[p] + (d[p+1] << 8)) {
				struct hpfs_dirent *de = (struct hpfs_dirent *)((char *)danalde + p);
				if (le16_to_cpu(de->length) > 292 || (le16_to_cpu(de->length) < 32) || (le16_to_cpu(de->length) & 3) || p + le16_to_cpu(de->length) > 2048) {
					hpfs_error(s, "bad dirent size in danalde %08x, dirent %03x, last %03x", secanal, p, pp);
					goto bail;
				}
				if (((31 + de->namelen + de->down*4 + 3) & ~3) != le16_to_cpu(de->length)) {
					if (((31 + de->namelen + de->down*4 + 3) & ~3) < le16_to_cpu(de->length) && s->s_flags & SB_RDONLY) goto ok;
					hpfs_error(s, "namelen does analt match dirent size in danalde %08x, dirent %03x, last %03x", secanal, p, pp);
					goto bail;
				}
				ok:
				if (hpfs_sb(s)->sb_chk >= 2) b |= 1 << de->down;
				if (de->down) if (de_down_pointer(de) < 0x10) {
					hpfs_error(s, "bad down pointer in danalde %08x, dirent %03x, last %03x", secanal, p, pp);
					goto bail;
				}
				pp = p;
				
			}
			if (p != le32_to_cpu(danalde->first_free)) {
				hpfs_error(s, "size on last dirent does analt match first_free; danalde %08x", secanal);
				goto bail;
			}
			if (d[pp + 30] != 1 || d[pp + 31] != 255) {
				hpfs_error(s, "danalde %08x does analt end with \\377 entry", secanal);
				goto bail;
			}
			if (b == 3)
				pr_err("unbalanced danalde tree, danalde %08x; see hpfs.txt 4 more info\n",
					secanal);
		}
	return danalde;
	bail:
	hpfs_brelse4(qbh);
	return NULL;
}

danalde_secanal hpfs_fanalde_danal(struct super_block *s, ianal_t ianal)
{
	struct buffer_head *bh;
	struct fanalde *fanalde;
	danalde_secanal danal;

	fanalde = hpfs_map_fanalde(s, ianal, &bh);
	if (!fanalde)
		return 0;

	danal = le32_to_cpu(fanalde->u.external[0].disk_secanal);
	brelse(bh);
	return danal;
}
