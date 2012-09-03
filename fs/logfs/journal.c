/*
 * fs/logfs/journal.c	- journal handling code
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 */
#include "logfs.h"
#include <linux/slab.h>

static void logfs_calc_free(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	u64 reserve, no_segs = super->s_no_segs;
	s64 free;
	int i;

	/* superblock segments */
	no_segs -= 2;
	super->s_no_journal_segs = 0;
	/* journal */
	journal_for_each(i)
		if (super->s_journal_seg[i]) {
			no_segs--;
			super->s_no_journal_segs++;
		}

	/* open segments plus one extra per level for GC */
	no_segs -= 2 * super->s_total_levels;

	free = no_segs * (super->s_segsize - LOGFS_SEGMENT_RESERVE);
	free -= super->s_used_bytes;
	/* just a bit extra */
	free -= super->s_total_levels * 4096;

	/* Bad blocks are 'paid' for with speed reserve - the filesystem
	 * simply gets slower as bad blocks accumulate.  Until the bad blocks
	 * exceed the speed reserve - then the filesystem gets smaller.
	 */
	reserve = super->s_bad_segments + super->s_bad_seg_reserve;
	reserve *= super->s_segsize - LOGFS_SEGMENT_RESERVE;
	reserve = max(reserve, super->s_speed_reserve);
	free -= reserve;
	if (free < 0)
		free = 0;

	super->s_free_bytes = free;
}

static void reserve_sb_and_journal(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct btree_head32 *head = &super->s_reserved_segments;
	int i, err;

	err = btree_insert32(head, seg_no(sb, super->s_sb_ofs[0]), (void *)1,
			GFP_KERNEL);
	BUG_ON(err);

	err = btree_insert32(head, seg_no(sb, super->s_sb_ofs[1]), (void *)1,
			GFP_KERNEL);
	BUG_ON(err);

	journal_for_each(i) {
		if (!super->s_journal_seg[i])
			continue;
		err = btree_insert32(head, super->s_journal_seg[i], (void *)1,
				GFP_KERNEL);
		BUG_ON(err);
	}
}

static void read_dynsb(struct super_block *sb,
		struct logfs_je_dynsb *dynsb)
{
	struct logfs_super *super = logfs_super(sb);

	super->s_gec		= be64_to_cpu(dynsb->ds_gec);
	super->s_sweeper	= be64_to_cpu(dynsb->ds_sweeper);
	super->s_victim_ino	= be64_to_cpu(dynsb->ds_victim_ino);
	super->s_rename_dir	= be64_to_cpu(dynsb->ds_rename_dir);
	super->s_rename_pos	= be64_to_cpu(dynsb->ds_rename_pos);
	super->s_used_bytes	= be64_to_cpu(dynsb->ds_used_bytes);
	super->s_generation	= be32_to_cpu(dynsb->ds_generation);
}

static void read_anchor(struct super_block *sb,
		struct logfs_je_anchor *da)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *inode = super->s_master_inode;
	struct logfs_inode *li = logfs_inode(inode);
	int i;

	super->s_last_ino = be64_to_cpu(da->da_last_ino);
	li->li_flags	= 0;
	li->li_height	= da->da_height;
	i_size_write(inode, be64_to_cpu(da->da_size));
	li->li_used_bytes = be64_to_cpu(da->da_used_bytes);

	for (i = 0; i < LOGFS_EMBEDDED_FIELDS; i++)
		li->li_data[i] = be64_to_cpu(da->da_data[i]);
}

static void read_erasecount(struct super_block *sb,
		struct logfs_je_journal_ec *ec)
{
	struct logfs_super *super = logfs_super(sb);
	int i;

	journal_for_each(i)
		super->s_journal_ec[i] = be32_to_cpu(ec->ec[i]);
}

static int read_area(struct super_block *sb, struct logfs_je_area *a)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area = super->s_area[a->gc_level];
	u64 ofs;
	u32 writemask = ~(super->s_writesize - 1);

	if (a->gc_level >= LOGFS_NO_AREAS)
		return -EIO;
	if (a->vim != VIM_DEFAULT)
		return -EIO; /* TODO: close area and continue */

	area->a_used_bytes = be32_to_cpu(a->used_bytes);
	area->a_written_bytes = area->a_used_bytes & writemask;
	area->a_segno = be32_to_cpu(a->segno);
	if (area->a_segno)
		area->a_is_open = 1;

	ofs = dev_ofs(sb, area->a_segno, area->a_written_bytes);
	if (super->s_writesize > 1)
		return logfs_buf_recover(area, ofs, a + 1, super->s_writesize);
	else
		return logfs_buf_recover(area, ofs, NULL, 0);
}

static void *unpack(void *from, void *to)
{
	struct logfs_journal_header *jh = from;
	void *data = from + sizeof(struct logfs_journal_header);
	int err;
	size_t inlen, outlen;

	inlen = be16_to_cpu(jh->h_len);
	outlen = be16_to_cpu(jh->h_datalen);

	if (jh->h_compr == COMPR_NONE)
		memcpy(to, data, inlen);
	else {
		err = logfs_uncompress(data, to, inlen, outlen);
		BUG_ON(err);
	}
	return to;
}

static int __read_je_header(struct super_block *sb, u64 ofs,
		struct logfs_journal_header *jh)
{
	struct logfs_super *super = logfs_super(sb);
	size_t bufsize = max_t(size_t, sb->s_blocksize, super->s_writesize)
		+ MAX_JOURNAL_HEADER;
	u16 type, len, datalen;
	int err;

	/* read header only */
	err = wbuf_read(sb, ofs, sizeof(*jh), jh);
	if (err)
		return err;
	type = be16_to_cpu(jh->h_type);
	len = be16_to_cpu(jh->h_len);
	datalen = be16_to_cpu(jh->h_datalen);
	if (len > sb->s_blocksize)
		return -EIO;
	if ((type < JE_FIRST) || (type > JE_LAST))
		return -EIO;
	if (datalen > bufsize)
		return -EIO;
	return 0;
}

static int __read_je_payload(struct super_block *sb, u64 ofs,
		struct logfs_journal_header *jh)
{
	u16 len;
	int err;

	len = be16_to_cpu(jh->h_len);
	err = wbuf_read(sb, ofs + sizeof(*jh), len, jh + 1);
	if (err)
		return err;
	if (jh->h_crc != logfs_crc32(jh, len + sizeof(*jh), 4)) {
		/* Old code was confused.  It forgot about the header length
		 * and stopped calculating the crc 16 bytes before the end
		 * of data - ick!
		 * FIXME: Remove this hack once the old code is fixed.
		 */
		if (jh->h_crc == logfs_crc32(jh, len, 4))
			WARN_ON_ONCE(1);
		else
			return -EIO;
	}
	return 0;
}

/*
 * jh needs to be large enough to hold the complete entry, not just the header
 */
static int __read_je(struct super_block *sb, u64 ofs,
		struct logfs_journal_header *jh)
{
	int err;

	err = __read_je_header(sb, ofs, jh);
	if (err)
		return err;
	return __read_je_payload(sb, ofs, jh);
}

static int read_je(struct super_block *sb, u64 ofs)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_journal_header *jh = super->s_compressed_je;
	void *scratch = super->s_je;
	u16 type, datalen;
	int err;

	err = __read_je(sb, ofs, jh);
	if (err)
		return err;
	type = be16_to_cpu(jh->h_type);
	datalen = be16_to_cpu(jh->h_datalen);

	switch (type) {
	case JE_DYNSB:
		read_dynsb(sb, unpack(jh, scratch));
		break;
	case JE_ANCHOR:
		read_anchor(sb, unpack(jh, scratch));
		break;
	case JE_ERASECOUNT:
		read_erasecount(sb, unpack(jh, scratch));
		break;
	case JE_AREA:
		err = read_area(sb, unpack(jh, scratch));
		break;
	case JE_OBJ_ALIAS:
		err = logfs_load_object_aliases(sb, unpack(jh, scratch),
				datalen);
		break;
	default:
		WARN_ON_ONCE(1);
		return -EIO;
	}
	return err;
}

static int logfs_read_segment(struct super_block *sb, u32 segno)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_journal_header *jh = super->s_compressed_je;
	u64 ofs, seg_ofs = dev_ofs(sb, segno, 0);
	u32 h_ofs, last_ofs = 0;
	u16 len, datalen, last_len = 0;
	int i, err;

	/* search for most recent commit */
	for (h_ofs = 0; h_ofs < super->s_segsize; h_ofs += sizeof(*jh)) {
		ofs = seg_ofs + h_ofs;
		err = __read_je_header(sb, ofs, jh);
		if (err)
			continue;
		if (jh->h_type != cpu_to_be16(JE_COMMIT))
			continue;
		err = __read_je_payload(sb, ofs, jh);
		if (err)
			continue;
		len = be16_to_cpu(jh->h_len);
		datalen = be16_to_cpu(jh->h_datalen);
		if ((datalen > sizeof(super->s_je_array)) ||
				(datalen % sizeof(__be64)))
			continue;
		last_ofs = h_ofs;
		last_len = datalen;
		h_ofs += ALIGN(len, sizeof(*jh)) - sizeof(*jh);
	}
	/* read commit */
	if (last_ofs == 0)
		return -ENOENT;
	ofs = seg_ofs + last_ofs;
	log_journal("Read commit from %llx\n", ofs);
	err = __read_je(sb, ofs, jh);
	BUG_ON(err); /* We should have caught it in the scan loop already */
	if (err)
		return err;
	/* uncompress */
	unpack(jh, super->s_je_array);
	super->s_no_je = last_len / sizeof(__be64);
	/* iterate over array */
	for (i = 0; i < super->s_no_je; i++) {
		err = read_je(sb, be64_to_cpu(super->s_je_array[i]));
		if (err)
			return err;
	}
	super->s_journal_area->a_segno = segno;
	return 0;
}

static u64 read_gec(struct super_block *sb, u32 segno)
{
	struct logfs_segment_header sh;
	__be32 crc;
	int err;

	if (!segno)
		return 0;
	err = wbuf_read(sb, dev_ofs(sb, segno, 0), sizeof(sh), &sh);
	if (err)
		return 0;
	crc = logfs_crc32(&sh, sizeof(sh), 4);
	if (crc != sh.crc) {
		WARN_ON(sh.gec != cpu_to_be64(0xffffffffffffffffull));
		/* Most likely it was just erased */
		return 0;
	}
	return be64_to_cpu(sh.gec);
}

static int logfs_read_journal(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	u64 gec[LOGFS_JOURNAL_SEGS], max;
	u32 segno;
	int i, max_i;

	max = 0;
	max_i = -1;
	journal_for_each(i) {
		segno = super->s_journal_seg[i];
		gec[i] = read_gec(sb, super->s_journal_seg[i]);
		if (gec[i] > max) {
			max = gec[i];
			max_i = i;
		}
	}
	if (max_i == -1)
		return -EIO;
	/* FIXME: Try older segments in case of error */
	return logfs_read_segment(sb, super->s_journal_seg[max_i]);
}

/*
 * First search the current segment (outer loop), then pick the next segment
 * in the array, skipping any zero entries (inner loop).
 */
static void journal_get_free_segment(struct logfs_area *area)
{
	struct logfs_super *super = logfs_super(area->a_sb);
	int i;

	journal_for_each(i) {
		if (area->a_segno != super->s_journal_seg[i])
			continue;

		do {
			i++;
			if (i == LOGFS_JOURNAL_SEGS)
				i = 0;
		} while (!super->s_journal_seg[i]);

		area->a_segno = super->s_journal_seg[i];
		area->a_erase_count = ++(super->s_journal_ec[i]);
		log_journal("Journal now at %x (ec %x)\n", area->a_segno,
				area->a_erase_count);
		return;
	}
	BUG();
}

static void journal_get_erase_count(struct logfs_area *area)
{
	/* erase count is stored globally and incremented in
	 * journal_get_free_segment() - nothing to do here */
}

static int journal_erase_segment(struct logfs_area *area)
{
	struct super_block *sb = area->a_sb;
	union {
		struct logfs_segment_header sh;
		unsigned char c[ALIGN(sizeof(struct logfs_segment_header), 16)];
	} u;
	u64 ofs;
	int err;

	err = logfs_erase_segment(sb, area->a_segno, 1);
	if (err)
		return err;

	memset(&u, 0, sizeof(u));
	u.sh.pad = 0;
	u.sh.type = SEG_JOURNAL;
	u.sh.level = 0;
	u.sh.segno = cpu_to_be32(area->a_segno);
	u.sh.ec = cpu_to_be32(area->a_erase_count);
	u.sh.gec = cpu_to_be64(logfs_super(sb)->s_gec);
	u.sh.crc = logfs_crc32(&u.sh, sizeof(u.sh), 4);

	/* This causes a bug in segment.c.  Not yet. */
	//logfs_set_segment_erased(sb, area->a_segno, area->a_erase_count, 0);

	ofs = dev_ofs(sb, area->a_segno, 0);
	area->a_used_bytes = sizeof(u);
	logfs_buf_write(area, ofs, &u, sizeof(u));
	return 0;
}

static size_t __logfs_write_header(struct logfs_super *super,
		struct logfs_journal_header *jh, size_t len, size_t datalen,
		u16 type, u8 compr)
{
	jh->h_len	= cpu_to_be16(len);
	jh->h_type	= cpu_to_be16(type);
	jh->h_datalen	= cpu_to_be16(datalen);
	jh->h_compr	= compr;
	jh->h_pad[0]	= 'H';
	jh->h_pad[1]	= 'E';
	jh->h_pad[2]	= 'A';
	jh->h_pad[3]	= 'D';
	jh->h_pad[4]	= 'R';
	jh->h_crc	= logfs_crc32(jh, len + sizeof(*jh), 4);
	return ALIGN(len, 16) + sizeof(*jh);
}

static size_t logfs_write_header(struct logfs_super *super,
		struct logfs_journal_header *jh, size_t datalen, u16 type)
{
	size_t len = datalen;

	return __logfs_write_header(super, jh, len, datalen, type, COMPR_NONE);
}

static inline size_t logfs_journal_erasecount_size(struct logfs_super *super)
{
	return LOGFS_JOURNAL_SEGS * sizeof(__be32);
}

static void *logfs_write_erasecount(struct super_block *sb, void *_ec,
		u16 *type, size_t *len)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_je_journal_ec *ec = _ec;
	int i;

	journal_for_each(i)
		ec->ec[i] = cpu_to_be32(super->s_journal_ec[i]);
	*type = JE_ERASECOUNT;
	*len = logfs_journal_erasecount_size(super);
	return ec;
}

static void account_shadow(void *_shadow, unsigned long _sb, u64 ignore,
		size_t ignore2)
{
	struct logfs_shadow *shadow = _shadow;
	struct super_block *sb = (void *)_sb;
	struct logfs_super *super = logfs_super(sb);

	/* consume new space */
	super->s_free_bytes	  -= shadow->new_len;
	super->s_used_bytes	  += shadow->new_len;
	super->s_dirty_used_bytes -= shadow->new_len;

	/* free up old space */
	super->s_free_bytes	  += shadow->old_len;
	super->s_used_bytes	  -= shadow->old_len;
	super->s_dirty_free_bytes -= shadow->old_len;

	logfs_set_segment_used(sb, shadow->old_ofs, -shadow->old_len);
	logfs_set_segment_used(sb, shadow->new_ofs, shadow->new_len);

	log_journal("account_shadow(%llx, %llx, %x) %llx->%llx %x->%x\n",
			shadow->ino, shadow->bix, shadow->gc_level,
			shadow->old_ofs, shadow->new_ofs,
			shadow->old_len, shadow->new_len);
	mempool_free(shadow, super->s_shadow_pool);
}

static void account_shadows(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *inode = super->s_master_inode;
	struct logfs_inode *li = logfs_inode(inode);
	struct shadow_tree *tree = &super->s_shadow_tree;

	btree_grim_visitor64(&tree->new, (unsigned long)sb, account_shadow);
	btree_grim_visitor64(&tree->old, (unsigned long)sb, account_shadow);
	btree_grim_visitor32(&tree->segment_map, 0, NULL);
	tree->no_shadowed_segments = 0;

	if (li->li_block) {
		/*
		 * We never actually use the structure, when attached to the
		 * master inode.  But it is easier to always free it here than
		 * to have checks in several places elsewhere when allocating
		 * it.
		 */
		li->li_block->ops->free_block(sb, li->li_block);
	}
	BUG_ON((s64)li->li_used_bytes < 0);
}

static void *__logfs_write_anchor(struct super_block *sb, void *_da,
		u16 *type, size_t *len)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_je_anchor *da = _da;
	struct inode *inode = super->s_master_inode;
	struct logfs_inode *li = logfs_inode(inode);
	int i;

	da->da_height	= li->li_height;
	da->da_last_ino = cpu_to_be64(super->s_last_ino);
	da->da_size	= cpu_to_be64(i_size_read(inode));
	da->da_used_bytes = cpu_to_be64(li->li_used_bytes);
	for (i = 0; i < LOGFS_EMBEDDED_FIELDS; i++)
		da->da_data[i] = cpu_to_be64(li->li_data[i]);
	*type = JE_ANCHOR;
	*len = sizeof(*da);
	return da;
}

static void *logfs_write_dynsb(struct super_block *sb, void *_dynsb,
		u16 *type, size_t *len)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_je_dynsb *dynsb = _dynsb;

	dynsb->ds_gec		= cpu_to_be64(super->s_gec);
	dynsb->ds_sweeper	= cpu_to_be64(super->s_sweeper);
	dynsb->ds_victim_ino	= cpu_to_be64(super->s_victim_ino);
	dynsb->ds_rename_dir	= cpu_to_be64(super->s_rename_dir);
	dynsb->ds_rename_pos	= cpu_to_be64(super->s_rename_pos);
	dynsb->ds_used_bytes	= cpu_to_be64(super->s_used_bytes);
	dynsb->ds_generation	= cpu_to_be32(super->s_generation);
	*type = JE_DYNSB;
	*len = sizeof(*dynsb);
	return dynsb;
}

static void write_wbuf(struct super_block *sb, struct logfs_area *area,
		void *wbuf)
{
	struct logfs_super *super = logfs_super(sb);
	struct address_space *mapping = super->s_mapping_inode->i_mapping;
	u64 ofs;
	pgoff_t index;
	int page_ofs;
	struct page *page;

	ofs = dev_ofs(sb, area->a_segno,
			area->a_used_bytes & ~(super->s_writesize - 1));
	index = ofs >> PAGE_SHIFT;
	page_ofs = ofs & (PAGE_SIZE - 1);

	page = find_or_create_page(mapping, index, GFP_NOFS);
	BUG_ON(!page);
	memcpy(wbuf, page_address(page) + page_ofs, super->s_writesize);
	unlock_page(page);
}

static void *logfs_write_area(struct super_block *sb, void *_a,
		u16 *type, size_t *len)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area = super->s_area[super->s_sum_index];
	struct logfs_je_area *a = _a;

	a->vim = VIM_DEFAULT;
	a->gc_level = super->s_sum_index;
	a->used_bytes = cpu_to_be32(area->a_used_bytes);
	a->segno = cpu_to_be32(area->a_segno);
	if (super->s_writesize > 1)
		write_wbuf(sb, area, a + 1);

	*type = JE_AREA;
	*len = sizeof(*a) + super->s_writesize;
	return a;
}

static void *logfs_write_commit(struct super_block *sb, void *h,
		u16 *type, size_t *len)
{
	struct logfs_super *super = logfs_super(sb);

	*type = JE_COMMIT;
	*len = super->s_no_je * sizeof(__be64);
	return super->s_je_array;
}

static size_t __logfs_write_je(struct super_block *sb, void *buf, u16 type,
		size_t len)
{
	struct logfs_super *super = logfs_super(sb);
	void *header = super->s_compressed_je;
	void *data = header + sizeof(struct logfs_journal_header);
	ssize_t compr_len, pad_len;
	u8 compr = COMPR_ZLIB;

	if (len == 0)
		return logfs_write_header(super, header, 0, type);

	compr_len = logfs_compress(buf, data, len, sb->s_blocksize);
	if (compr_len < 0 || type == JE_ANCHOR) {
		memcpy(data, buf, len);
		compr_len = len;
		compr = COMPR_NONE;
	}

	pad_len = ALIGN(compr_len, 16);
	memset(data + compr_len, 0, pad_len - compr_len);

	return __logfs_write_header(super, header, compr_len, len, type, compr);
}

static s64 logfs_get_free_bytes(struct logfs_area *area, size_t *bytes,
		int must_pad)
{
	u32 writesize = logfs_super(area->a_sb)->s_writesize;
	s32 ofs;
	int ret;

	ret = logfs_open_area(area, *bytes);
	if (ret)
		return -EAGAIN;

	ofs = area->a_used_bytes;
	area->a_used_bytes += *bytes;

	if (must_pad) {
		area->a_used_bytes = ALIGN(area->a_used_bytes, writesize);
		*bytes = area->a_used_bytes - ofs;
	}

	return dev_ofs(area->a_sb, area->a_segno, ofs);
}

static int logfs_write_je_buf(struct super_block *sb, void *buf, u16 type,
		size_t buf_len)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area = super->s_journal_area;
	struct logfs_journal_header *jh = super->s_compressed_je;
	size_t len;
	int must_pad = 0;
	s64 ofs;

	len = __logfs_write_je(sb, buf, type, buf_len);
	if (jh->h_type == cpu_to_be16(JE_COMMIT))
		must_pad = 1;

	ofs = logfs_get_free_bytes(area, &len, must_pad);
	if (ofs < 0)
		return ofs;
	logfs_buf_write(area, ofs, super->s_compressed_je, len);
	BUG_ON(super->s_no_je >= MAX_JOURNAL_ENTRIES);
	super->s_je_array[super->s_no_je++] = cpu_to_be64(ofs);
	return 0;
}

static int logfs_write_je(struct super_block *sb,
		void* (*write)(struct super_block *sb, void *scratch,
			u16 *type, size_t *len))
{
	void *buf;
	size_t len;
	u16 type;

	buf = write(sb, logfs_super(sb)->s_je, &type, &len);
	return logfs_write_je_buf(sb, buf, type, len);
}

int write_alias_journal(struct super_block *sb, u64 ino, u64 bix,
		level_t level, int child_no, __be64 val)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_obj_alias *oa = super->s_je;
	int err = 0, fill = super->s_je_fill;

	log_aliases("logfs_write_obj_aliases #%x(%llx, %llx, %x, %x) %llx\n",
			fill, ino, bix, level, child_no, be64_to_cpu(val));
	oa[fill].ino = cpu_to_be64(ino);
	oa[fill].bix = cpu_to_be64(bix);
	oa[fill].val = val;
	oa[fill].level = (__force u8)level;
	oa[fill].child_no = cpu_to_be16(child_no);
	fill++;
	if (fill >= sb->s_blocksize / sizeof(*oa)) {
		err = logfs_write_je_buf(sb, oa, JE_OBJ_ALIAS, sb->s_blocksize);
		fill = 0;
	}

	super->s_je_fill = fill;
	return err;
}

static int logfs_write_obj_aliases(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int err;

	log_journal("logfs_write_obj_aliases: %d aliases to write\n",
			super->s_no_object_aliases);
	super->s_je_fill = 0;
	err = logfs_write_obj_aliases_pagecache(sb);
	if (err)
		return err;

	if (super->s_je_fill)
		err = logfs_write_je_buf(sb, super->s_je, JE_OBJ_ALIAS,
				super->s_je_fill
				* sizeof(struct logfs_obj_alias));
	return err;
}

/*
 * Write all journal entries.  The goto logic ensures that all journal entries
 * are written whenever a new segment is used.  It is ugly and potentially a
 * bit wasteful, but robustness is more important.  With this we can *always*
 * erase all journal segments except the one containing the most recent commit.
 */
void logfs_write_anchor(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area = super->s_journal_area;
	int i, err;

	if (!(super->s_flags & LOGFS_SB_FLAG_DIRTY))
		return;
	super->s_flags &= ~LOGFS_SB_FLAG_DIRTY;

	BUG_ON(super->s_flags & LOGFS_SB_FLAG_SHUTDOWN);
	mutex_lock(&super->s_journal_mutex);

	/* Do this first or suffer corruption */
	logfs_sync_segments(sb);
	account_shadows(sb);

again:
	super->s_no_je = 0;
	for_each_area(i) {
		if (!super->s_area[i]->a_is_open)
			continue;
		super->s_sum_index = i;
		err = logfs_write_je(sb, logfs_write_area);
		if (err)
			goto again;
	}
	err = logfs_write_obj_aliases(sb);
	if (err)
		goto again;
	err = logfs_write_je(sb, logfs_write_erasecount);
	if (err)
		goto again;
	err = logfs_write_je(sb, __logfs_write_anchor);
	if (err)
		goto again;
	err = logfs_write_je(sb, logfs_write_dynsb);
	if (err)
		goto again;
	/*
	 * Order is imperative.  First we sync all writes, including the
	 * non-committed journal writes.  Then we write the final commit and
	 * sync the current journal segment.
	 * There is a theoretical bug here.  Syncing the journal segment will
	 * write a number of journal entries and the final commit.  All these
	 * are written in a single operation.  If the device layer writes the
	 * data back-to-front, the commit will precede the other journal
	 * entries, leaving a race window.
	 * Two fixes are possible.  Preferred is to fix the device layer to
	 * ensure writes happen front-to-back.  Alternatively we can insert
	 * another logfs_sync_area() super->s_devops->sync() combo before
	 * writing the commit.
	 */
	/*
	 * On another subject, super->s_devops->sync is usually not necessary.
	 * Unless called from sys_sync or friends, a barrier would suffice.
	 */
	super->s_devops->sync(sb);
	err = logfs_write_je(sb, logfs_write_commit);
	if (err)
		goto again;
	log_journal("Write commit to %llx\n",
			be64_to_cpu(super->s_je_array[super->s_no_je - 1]));
	logfs_sync_area(area);
	BUG_ON(area->a_used_bytes != area->a_written_bytes);
	super->s_devops->sync(sb);

	mutex_unlock(&super->s_journal_mutex);
	return;
}

void do_logfs_journal_wl_pass(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area = super->s_journal_area;
	struct btree_head32 *head = &super->s_reserved_segments;
	u32 segno, ec;
	int i, err;

	log_journal("Journal requires wear-leveling.\n");
	/* Drop old segments */
	journal_for_each(i)
		if (super->s_journal_seg[i]) {
			btree_remove32(head, super->s_journal_seg[i]);
			logfs_set_segment_unreserved(sb,
					super->s_journal_seg[i],
					super->s_journal_ec[i]);
			super->s_journal_seg[i] = 0;
			super->s_journal_ec[i] = 0;
		}
	/* Get new segments */
	for (i = 0; i < super->s_no_journal_segs; i++) {
		segno = get_best_cand(sb, &super->s_reserve_list, &ec);
		super->s_journal_seg[i] = segno;
		super->s_journal_ec[i] = ec;
		logfs_set_segment_reserved(sb, segno);
		err = btree_insert32(head, segno, (void *)1, GFP_NOFS);
		BUG_ON(err); /* mempool should prevent this */
		err = logfs_erase_segment(sb, segno, 1);
		BUG_ON(err); /* FIXME: remount-ro would be nicer */
	}
	/* Manually move journal_area */
	freeseg(sb, area->a_segno);
	area->a_segno = super->s_journal_seg[0];
	area->a_is_open = 0;
	area->a_used_bytes = 0;
	/* Write journal */
	logfs_write_anchor(sb);
	/* Write superblocks */
	err = logfs_write_sb(sb);
	BUG_ON(err);
}

static const struct logfs_area_ops journal_area_ops = {
	.get_free_segment	= journal_get_free_segment,
	.get_erase_count	= journal_get_erase_count,
	.erase_segment		= journal_erase_segment,
};

int logfs_init_journal(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	size_t bufsize = max_t(size_t, sb->s_blocksize, super->s_writesize)
		+ MAX_JOURNAL_HEADER;
	int ret = -ENOMEM;

	mutex_init(&super->s_journal_mutex);
	btree_init_mempool32(&super->s_reserved_segments, super->s_btree_pool);

	super->s_je = kzalloc(bufsize, GFP_KERNEL);
	if (!super->s_je)
		return ret;

	super->s_compressed_je = kzalloc(bufsize, GFP_KERNEL);
	if (!super->s_compressed_je)
		return ret;

	super->s_master_inode = logfs_new_meta_inode(sb, LOGFS_INO_MASTER);
	if (IS_ERR(super->s_master_inode))
		return PTR_ERR(super->s_master_inode);

	ret = logfs_read_journal(sb);
	if (ret)
		return -EIO;

	reserve_sb_and_journal(sb);
	logfs_calc_free(sb);

	super->s_journal_area->a_ops = &journal_area_ops;
	return 0;
}

void logfs_cleanup_journal(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	btree_grim_visitor32(&super->s_reserved_segments, 0, NULL);

	kfree(super->s_compressed_je);
	kfree(super->s_je);
}
