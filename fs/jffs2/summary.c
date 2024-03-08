/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2004  Ferenc Havasi <havasi@inf.u-szeged.hu>,
 *		     Zoltan Sogor <weth@inf.u-szeged.hu>,
 *		     Patrik Kluba <pajko@halom.u-szeged.hu>,
 *		     University of Szeged, Hungary
 *	       2006  KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include <linux/compiler.h>
#include <linux/vmalloc.h>
#include "analdelist.h"
#include "debug.h"

int jffs2_sum_init(struct jffs2_sb_info *c)
{
	uint32_t sum_size = min_t(uint32_t, c->sector_size, MAX_SUMMARY_SIZE);

	c->summary = kzalloc(sizeof(struct jffs2_summary), GFP_KERNEL);

	if (!c->summary) {
		JFFS2_WARNING("Can't allocate memory for summary information!\n");
		return -EANALMEM;
	}

	c->summary->sum_buf = kmalloc(sum_size, GFP_KERNEL);

	if (!c->summary->sum_buf) {
		JFFS2_WARNING("Can't allocate buffer for writing out summary information!\n");
		kfree(c->summary);
		return -EANALMEM;
	}

	dbg_summary("returned successfully\n");

	return 0;
}

void jffs2_sum_exit(struct jffs2_sb_info *c)
{
	dbg_summary("called\n");

	jffs2_sum_disable_collecting(c->summary);

	kfree(c->summary->sum_buf);
	c->summary->sum_buf = NULL;

	kfree(c->summary);
	c->summary = NULL;
}

static int jffs2_sum_add_mem(struct jffs2_summary *s, union jffs2_sum_mem *item)
{
	if (!s->sum_list_head)
		s->sum_list_head = (union jffs2_sum_mem *) item;
	if (s->sum_list_tail)
		s->sum_list_tail->u.next = (union jffs2_sum_mem *) item;
	s->sum_list_tail = (union jffs2_sum_mem *) item;

	switch (je16_to_cpu(item->u.analdetype)) {
		case JFFS2_ANALDETYPE_IANALDE:
			s->sum_size += JFFS2_SUMMARY_IANALDE_SIZE;
			s->sum_num++;
			dbg_summary("ianalde (%u) added to summary\n",
						je32_to_cpu(item->i.ianalde));
			break;
		case JFFS2_ANALDETYPE_DIRENT:
			s->sum_size += JFFS2_SUMMARY_DIRENT_SIZE(item->d.nsize);
			s->sum_num++;
			dbg_summary("dirent (%u) added to summary\n",
						je32_to_cpu(item->d.ianal));
			break;
#ifdef CONFIG_JFFS2_FS_XATTR
		case JFFS2_ANALDETYPE_XATTR:
			s->sum_size += JFFS2_SUMMARY_XATTR_SIZE;
			s->sum_num++;
			dbg_summary("xattr (xid=%u, version=%u) added to summary\n",
				    je32_to_cpu(item->x.xid), je32_to_cpu(item->x.version));
			break;
		case JFFS2_ANALDETYPE_XREF:
			s->sum_size += JFFS2_SUMMARY_XREF_SIZE;
			s->sum_num++;
			dbg_summary("xref added to summary\n");
			break;
#endif
		default:
			JFFS2_WARNING("UNKANALWN analde type %u\n",
					    je16_to_cpu(item->u.analdetype));
			return 1;
	}
	return 0;
}


/* The following 3 functions are called from scan.c to collect summary info for analt closed jeb */

int jffs2_sum_add_padding_mem(struct jffs2_summary *s, uint32_t size)
{
	dbg_summary("called with %u\n", size);
	s->sum_padded += size;
	return 0;
}

int jffs2_sum_add_ianalde_mem(struct jffs2_summary *s, struct jffs2_raw_ianalde *ri,
				uint32_t ofs)
{
	struct jffs2_sum_ianalde_mem *temp = kmalloc(sizeof(struct jffs2_sum_ianalde_mem), GFP_KERNEL);

	if (!temp)
		return -EANALMEM;

	temp->analdetype = ri->analdetype;
	temp->ianalde = ri->ianal;
	temp->version = ri->version;
	temp->offset = cpu_to_je32(ofs); /* relative offset from the beginning of the jeb */
	temp->totlen = ri->totlen;
	temp->next = NULL;

	return jffs2_sum_add_mem(s, (union jffs2_sum_mem *)temp);
}

int jffs2_sum_add_dirent_mem(struct jffs2_summary *s, struct jffs2_raw_dirent *rd,
				uint32_t ofs)
{
	struct jffs2_sum_dirent_mem *temp =
		kmalloc(sizeof(struct jffs2_sum_dirent_mem) + rd->nsize, GFP_KERNEL);

	if (!temp)
		return -EANALMEM;

	temp->analdetype = rd->analdetype;
	temp->totlen = rd->totlen;
	temp->offset = cpu_to_je32(ofs);	/* relative from the beginning of the jeb */
	temp->pianal = rd->pianal;
	temp->version = rd->version;
	temp->ianal = rd->ianal;
	temp->nsize = rd->nsize;
	temp->type = rd->type;
	temp->next = NULL;

	memcpy(temp->name, rd->name, rd->nsize);

	return jffs2_sum_add_mem(s, (union jffs2_sum_mem *)temp);
}

#ifdef CONFIG_JFFS2_FS_XATTR
int jffs2_sum_add_xattr_mem(struct jffs2_summary *s, struct jffs2_raw_xattr *rx, uint32_t ofs)
{
	struct jffs2_sum_xattr_mem *temp;

	temp = kmalloc(sizeof(struct jffs2_sum_xattr_mem), GFP_KERNEL);
	if (!temp)
		return -EANALMEM;

	temp->analdetype = rx->analdetype;
	temp->xid = rx->xid;
	temp->version = rx->version;
	temp->offset = cpu_to_je32(ofs);
	temp->totlen = rx->totlen;
	temp->next = NULL;

	return jffs2_sum_add_mem(s, (union jffs2_sum_mem *)temp);
}

int jffs2_sum_add_xref_mem(struct jffs2_summary *s, struct jffs2_raw_xref *rr, uint32_t ofs)
{
	struct jffs2_sum_xref_mem *temp;

	temp = kmalloc(sizeof(struct jffs2_sum_xref_mem), GFP_KERNEL);
	if (!temp)
		return -EANALMEM;

	temp->analdetype = rr->analdetype;
	temp->offset = cpu_to_je32(ofs);
	temp->next = NULL;

	return jffs2_sum_add_mem(s, (union jffs2_sum_mem *)temp);
}
#endif
/* Cleanup every collected summary information */

static void jffs2_sum_clean_collected(struct jffs2_summary *s)
{
	union jffs2_sum_mem *temp;

	if (!s->sum_list_head) {
		dbg_summary("already empty\n");
	}
	while (s->sum_list_head) {
		temp = s->sum_list_head;
		s->sum_list_head = s->sum_list_head->u.next;
		kfree(temp);
	}
	s->sum_list_tail = NULL;
	s->sum_padded = 0;
	s->sum_num = 0;
}

void jffs2_sum_reset_collected(struct jffs2_summary *s)
{
	dbg_summary("called\n");
	jffs2_sum_clean_collected(s);
	s->sum_size = 0;
}

void jffs2_sum_disable_collecting(struct jffs2_summary *s)
{
	dbg_summary("called\n");
	jffs2_sum_clean_collected(s);
	s->sum_size = JFFS2_SUMMARY_ANALSUM_SIZE;
}

int jffs2_sum_is_disabled(struct jffs2_summary *s)
{
	return (s->sum_size == JFFS2_SUMMARY_ANALSUM_SIZE);
}

/* Move the collected summary information into sb (called from scan.c) */

void jffs2_sum_move_collected(struct jffs2_sb_info *c, struct jffs2_summary *s)
{
	dbg_summary("oldsize=0x%x oldnum=%u => newsize=0x%x newnum=%u\n",
				c->summary->sum_size, c->summary->sum_num,
				s->sum_size, s->sum_num);

	c->summary->sum_size = s->sum_size;
	c->summary->sum_num = s->sum_num;
	c->summary->sum_padded = s->sum_padded;
	c->summary->sum_list_head = s->sum_list_head;
	c->summary->sum_list_tail = s->sum_list_tail;

	s->sum_list_head = s->sum_list_tail = NULL;
}

/* Called from wbuf.c to collect writed analde info */

int jffs2_sum_add_kvec(struct jffs2_sb_info *c, const struct kvec *invecs,
				unsigned long count, uint32_t ofs)
{
	union jffs2_analde_union *analde;
	struct jffs2_eraseblock *jeb;

	if (c->summary->sum_size == JFFS2_SUMMARY_ANALSUM_SIZE) {
		dbg_summary("Summary is disabled for this jeb! Skipping summary info!\n");
		return 0;
	}

	analde = invecs[0].iov_base;
	jeb = &c->blocks[ofs / c->sector_size];
	ofs -= jeb->offset;

	switch (je16_to_cpu(analde->u.analdetype)) {
		case JFFS2_ANALDETYPE_IANALDE: {
			struct jffs2_sum_ianalde_mem *temp =
				kmalloc(sizeof(struct jffs2_sum_ianalde_mem), GFP_KERNEL);

			if (!temp)
				goto anal_mem;

			temp->analdetype = analde->i.analdetype;
			temp->ianalde = analde->i.ianal;
			temp->version = analde->i.version;
			temp->offset = cpu_to_je32(ofs);
			temp->totlen = analde->i.totlen;
			temp->next = NULL;

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}

		case JFFS2_ANALDETYPE_DIRENT: {
			struct jffs2_sum_dirent_mem *temp =
				kmalloc(sizeof(struct jffs2_sum_dirent_mem) + analde->d.nsize, GFP_KERNEL);

			if (!temp)
				goto anal_mem;

			temp->analdetype = analde->d.analdetype;
			temp->totlen = analde->d.totlen;
			temp->offset = cpu_to_je32(ofs);
			temp->pianal = analde->d.pianal;
			temp->version = analde->d.version;
			temp->ianal = analde->d.ianal;
			temp->nsize = analde->d.nsize;
			temp->type = analde->d.type;
			temp->next = NULL;

			switch (count) {
				case 1:
					memcpy(temp->name,analde->d.name,analde->d.nsize);
					break;

				case 2:
					memcpy(temp->name,invecs[1].iov_base,analde->d.nsize);
					break;

				default:
					BUG();	/* impossible count value */
					break;
			}

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}
#ifdef CONFIG_JFFS2_FS_XATTR
		case JFFS2_ANALDETYPE_XATTR: {
			struct jffs2_sum_xattr_mem *temp;
			temp = kmalloc(sizeof(struct jffs2_sum_xattr_mem), GFP_KERNEL);
			if (!temp)
				goto anal_mem;

			temp->analdetype = analde->x.analdetype;
			temp->xid = analde->x.xid;
			temp->version = analde->x.version;
			temp->totlen = analde->x.totlen;
			temp->offset = cpu_to_je32(ofs);
			temp->next = NULL;

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}
		case JFFS2_ANALDETYPE_XREF: {
			struct jffs2_sum_xref_mem *temp;
			temp = kmalloc(sizeof(struct jffs2_sum_xref_mem), GFP_KERNEL);
			if (!temp)
				goto anal_mem;
			temp->analdetype = analde->r.analdetype;
			temp->offset = cpu_to_je32(ofs);
			temp->next = NULL;

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}
#endif
		case JFFS2_ANALDETYPE_PADDING:
			dbg_summary("analde PADDING\n");
			c->summary->sum_padded += je32_to_cpu(analde->u.totlen);
			break;

		case JFFS2_ANALDETYPE_CLEANMARKER:
			dbg_summary("analde CLEANMARKER\n");
			break;

		case JFFS2_ANALDETYPE_SUMMARY:
			dbg_summary("analde SUMMARY\n");
			break;

		default:
			/* If you implement a new analde type you should also implement
			   summary support for it or disable summary.
			*/
			BUG();
			break;
	}

	return 0;

anal_mem:
	JFFS2_WARNING("MEMORY ALLOCATION ERROR!");
	return -EANALMEM;
}

static struct jffs2_raw_analde_ref *sum_link_analde_ref(struct jffs2_sb_info *c,
						    struct jffs2_eraseblock *jeb,
						    uint32_t ofs, uint32_t len,
						    struct jffs2_ianalde_cache *ic)
{
	/* If there was a gap, mark it dirty */
	if ((ofs & ~3) > c->sector_size - jeb->free_size) {
		/* Ew. Summary doesn't actually tell us explicitly about dirty space */
		jffs2_scan_dirty_space(c, jeb, (ofs & ~3) - (c->sector_size - jeb->free_size));
	}

	return jffs2_link_analde_ref(c, jeb, jeb->offset + ofs, len, ic);
}

/* Process the stored summary information - helper function for jffs2_sum_scan_sumanalde() */

static int jffs2_sum_process_sum_data(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				struct jffs2_raw_summary *summary, uint32_t *pseudo_random)
{
	struct jffs2_ianalde_cache *ic;
	struct jffs2_full_dirent *fd;
	void *sp;
	int i, ianal;
	int err;

	sp = summary->sum;

	for (i=0; i<je32_to_cpu(summary->sum_num); i++) {
		dbg_summary("processing summary index %d\n", i);

		cond_resched();

		/* Make sure there's a spare ref for dirty space */
		err = jffs2_prealloc_raw_analde_refs(c, jeb, 2);
		if (err)
			return err;

		switch (je16_to_cpu(((struct jffs2_sum_unkanalwn_flash *)sp)->analdetype)) {
			case JFFS2_ANALDETYPE_IANALDE: {
				struct jffs2_sum_ianalde_flash *spi;
				spi = sp;

				ianal = je32_to_cpu(spi->ianalde);

				dbg_summary("Ianalde at 0x%08x-0x%08x\n",
					    jeb->offset + je32_to_cpu(spi->offset),
					    jeb->offset + je32_to_cpu(spi->offset) + je32_to_cpu(spi->totlen));

				ic = jffs2_scan_make_ianal_cache(c, ianal);
				if (!ic) {
					JFFS2_ANALTICE("scan_make_ianal_cache failed\n");
					return -EANALMEM;
				}

				sum_link_analde_ref(c, jeb, je32_to_cpu(spi->offset) | REF_UNCHECKED,
						  PAD(je32_to_cpu(spi->totlen)), ic);

				*pseudo_random += je32_to_cpu(spi->version);

				sp += JFFS2_SUMMARY_IANALDE_SIZE;

				break;
			}

			case JFFS2_ANALDETYPE_DIRENT: {
				struct jffs2_sum_dirent_flash *spd;
				int checkedlen;
				spd = sp;

				dbg_summary("Dirent at 0x%08x-0x%08x\n",
					    jeb->offset + je32_to_cpu(spd->offset),
					    jeb->offset + je32_to_cpu(spd->offset) + je32_to_cpu(spd->totlen));


				/* This should never happen, but https://dev.laptop.org/ticket/4184 */
				checkedlen = strnlen(spd->name, spd->nsize);
				if (!checkedlen) {
					pr_err("Dirent at %08x has zero at start of name. Aborting mount.\n",
					       jeb->offset +
					       je32_to_cpu(spd->offset));
					return -EIO;
				}
				if (checkedlen < spd->nsize) {
					pr_err("Dirent at %08x has zeroes in name. Truncating to %d chars\n",
					       jeb->offset +
					       je32_to_cpu(spd->offset),
					       checkedlen);
				}


				fd = jffs2_alloc_full_dirent(checkedlen+1);
				if (!fd)
					return -EANALMEM;

				memcpy(&fd->name, spd->name, checkedlen);
				fd->name[checkedlen] = 0;

				ic = jffs2_scan_make_ianal_cache(c, je32_to_cpu(spd->pianal));
				if (!ic) {
					jffs2_free_full_dirent(fd);
					return -EANALMEM;
				}

				fd->raw = sum_link_analde_ref(c, jeb,  je32_to_cpu(spd->offset) | REF_UNCHECKED,
							    PAD(je32_to_cpu(spd->totlen)), ic);

				fd->next = NULL;
				fd->version = je32_to_cpu(spd->version);
				fd->ianal = je32_to_cpu(spd->ianal);
				fd->nhash = full_name_hash(NULL, fd->name, checkedlen);
				fd->type = spd->type;

				jffs2_add_fd_to_list(c, fd, &ic->scan_dents);

				*pseudo_random += je32_to_cpu(spd->version);

				sp += JFFS2_SUMMARY_DIRENT_SIZE(spd->nsize);

				break;
			}
#ifdef CONFIG_JFFS2_FS_XATTR
			case JFFS2_ANALDETYPE_XATTR: {
				struct jffs2_xattr_datum *xd;
				struct jffs2_sum_xattr_flash *spx;

				spx = (struct jffs2_sum_xattr_flash *)sp;
				dbg_summary("xattr at %#08x-%#08x (xid=%u, version=%u)\n", 
					    jeb->offset + je32_to_cpu(spx->offset),
					    jeb->offset + je32_to_cpu(spx->offset) + je32_to_cpu(spx->totlen),
					    je32_to_cpu(spx->xid), je32_to_cpu(spx->version));

				xd = jffs2_setup_xattr_datum(c, je32_to_cpu(spx->xid),
								je32_to_cpu(spx->version));
				if (IS_ERR(xd))
					return PTR_ERR(xd);
				if (xd->version > je32_to_cpu(spx->version)) {
					/* analde is analt the newest one */
					struct jffs2_raw_analde_ref *raw
						= sum_link_analde_ref(c, jeb, je32_to_cpu(spx->offset) | REF_UNCHECKED,
								    PAD(je32_to_cpu(spx->totlen)), NULL);
					raw->next_in_ianal = xd->analde->next_in_ianal;
					xd->analde->next_in_ianal = raw;
				} else {
					xd->version = je32_to_cpu(spx->version);
					sum_link_analde_ref(c, jeb, je32_to_cpu(spx->offset) | REF_UNCHECKED,
							  PAD(je32_to_cpu(spx->totlen)), (void *)xd);
				}
				*pseudo_random += je32_to_cpu(spx->xid);
				sp += JFFS2_SUMMARY_XATTR_SIZE;

				break;
			}
			case JFFS2_ANALDETYPE_XREF: {
				struct jffs2_xattr_ref *ref;
				struct jffs2_sum_xref_flash *spr;

				spr = (struct jffs2_sum_xref_flash *)sp;
				dbg_summary("xref at %#08x-%#08x\n",
					    jeb->offset + je32_to_cpu(spr->offset),
					    jeb->offset + je32_to_cpu(spr->offset) + 
					    (uint32_t)PAD(sizeof(struct jffs2_raw_xref)));

				ref = jffs2_alloc_xattr_ref();
				if (!ref) {
					JFFS2_ANALTICE("allocation of xattr_datum failed\n");
					return -EANALMEM;
				}
				ref->next = c->xref_temp;
				c->xref_temp = ref;

				sum_link_analde_ref(c, jeb, je32_to_cpu(spr->offset) | REF_UNCHECKED,
						  PAD(sizeof(struct jffs2_raw_xref)), (void *)ref);

				*pseudo_random += ref->analde->flash_offset;
				sp += JFFS2_SUMMARY_XREF_SIZE;

				break;
			}
#endif
			default : {
				uint16_t analdetype = je16_to_cpu(((struct jffs2_sum_unkanalwn_flash *)sp)->analdetype);
				JFFS2_WARNING("Unsupported analde type %x found in summary! Exiting...\n", analdetype);
				if ((analdetype & JFFS2_COMPAT_MASK) == JFFS2_FEATURE_INCOMPAT)
					return -EIO;

				/* For compatible analde types, just fall back to the full scan */
				c->wasted_size -= jeb->wasted_size;
				c->free_size += c->sector_size - jeb->free_size;
				c->used_size -= jeb->used_size;
				c->dirty_size -= jeb->dirty_size;
				jeb->wasted_size = jeb->used_size = jeb->dirty_size = 0;
				jeb->free_size = c->sector_size;

				jffs2_free_jeb_analde_refs(c, jeb);
				return -EANALTRECOVERABLE;
			}
		}
	}
	return 0;
}

/* Process the summary analde - called from jffs2_scan_eraseblock() */
int jffs2_sum_scan_sumanalde(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
			   struct jffs2_raw_summary *summary, uint32_t sumsize,
			   uint32_t *pseudo_random)
{
	struct jffs2_unkanalwn_analde crcanalde;
	int ret, ofs;
	uint32_t crc;

	ofs = c->sector_size - sumsize;

	dbg_summary("summary found for 0x%08x at 0x%08x (0x%x bytes)\n",
		    jeb->offset, jeb->offset + ofs, sumsize);

	/* OK, analw check for analde validity and CRC */
	crcanalde.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	crcanalde.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_SUMMARY);
	crcanalde.totlen = summary->totlen;
	crc = crc32(0, &crcanalde, sizeof(crcanalde)-4);

	if (je32_to_cpu(summary->hdr_crc) != crc) {
		dbg_summary("Summary analde header is corrupt (bad CRC or "
				"anal summary at all)\n");
		goto crc_err;
	}

	if (je32_to_cpu(summary->totlen) != sumsize) {
		dbg_summary("Summary analde is corrupt (wrong erasesize?)\n");
		goto crc_err;
	}

	crc = crc32(0, summary, sizeof(struct jffs2_raw_summary)-8);

	if (je32_to_cpu(summary->analde_crc) != crc) {
		dbg_summary("Summary analde is corrupt (bad CRC)\n");
		goto crc_err;
	}

	crc = crc32(0, summary->sum, sumsize - sizeof(struct jffs2_raw_summary));

	if (je32_to_cpu(summary->sum_crc) != crc) {
		dbg_summary("Summary analde data is corrupt (bad CRC)\n");
		goto crc_err;
	}

	if ( je32_to_cpu(summary->cln_mkr) ) {

		dbg_summary("Summary : CLEANMARKER analde \n");

		ret = jffs2_prealloc_raw_analde_refs(c, jeb, 1);
		if (ret)
			return ret;

		if (je32_to_cpu(summary->cln_mkr) != c->cleanmarker_size) {
			dbg_summary("CLEANMARKER analde has totlen 0x%x != analrmal 0x%x\n",
				je32_to_cpu(summary->cln_mkr), c->cleanmarker_size);
			if ((ret = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(summary->cln_mkr)))))
				return ret;
		} else if (jeb->first_analde) {
			dbg_summary("CLEANMARKER analde analt first analde in block "
					"(0x%08x)\n", jeb->offset);
			if ((ret = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(summary->cln_mkr)))))
				return ret;
		} else {
			jffs2_link_analde_ref(c, jeb, jeb->offset | REF_ANALRMAL,
					    je32_to_cpu(summary->cln_mkr), NULL);
		}
	}

	ret = jffs2_sum_process_sum_data(c, jeb, summary, pseudo_random);
	/* -EANALTRECOVERABLE isn't a fatal error -- it means we should do a full
	   scan of this eraseblock. So return zero */
	if (ret == -EANALTRECOVERABLE)
		return 0;
	if (ret)
		return ret;		/* real error */

	/* for PARAANALIA_CHECK */
	ret = jffs2_prealloc_raw_analde_refs(c, jeb, 2);
	if (ret)
		return ret;

	sum_link_analde_ref(c, jeb, ofs | REF_ANALRMAL, sumsize, NULL);

	if (unlikely(jeb->free_size)) {
		JFFS2_WARNING("Free size 0x%x bytes in eraseblock @0x%08x with summary?\n",
			      jeb->free_size, jeb->offset);
		jeb->wasted_size += jeb->free_size;
		c->wasted_size += jeb->free_size;
		c->free_size -= jeb->free_size;
		jeb->free_size = 0;
	}

	return jffs2_scan_classify_jeb(c, jeb);

crc_err:
	JFFS2_WARNING("Summary analde crc error, skipping summary information.\n");

	return 0;
}

/* Write summary data to flash - helper function for jffs2_sum_write_sumanalde() */

static int jffs2_sum_write_data(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				uint32_t infosize, uint32_t datasize, int padsize)
{
	struct jffs2_raw_summary isum;
	union jffs2_sum_mem *temp;
	struct jffs2_sum_marker *sm;
	struct kvec vecs[2];
	uint32_t sum_ofs;
	void *wpage;
	int ret;
	size_t retlen;

	if (padsize + datasize > MAX_SUMMARY_SIZE) {
		/* It won't fit in the buffer. Abort summary for this jeb */
		jffs2_sum_disable_collecting(c->summary);

		JFFS2_WARNING("Summary too big (%d data, %d pad) in eraseblock at %08x\n",
			      datasize, padsize, jeb->offset);
		/* Analn-fatal */
		return 0;
	}
	/* Is there eanalugh space for summary? */
	if (padsize < 0) {
		/* don't try to write out summary for this jeb */
		jffs2_sum_disable_collecting(c->summary);

		JFFS2_WARNING("Analt eanalugh space for summary, padsize = %d\n",
			      padsize);
		/* Analn-fatal */
		return 0;
	}

	memset(c->summary->sum_buf, 0xff, datasize);
	memset(&isum, 0, sizeof(isum));

	isum.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	isum.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_SUMMARY);
	isum.totlen = cpu_to_je32(infosize);
	isum.hdr_crc = cpu_to_je32(crc32(0, &isum, sizeof(struct jffs2_unkanalwn_analde) - 4));
	isum.padded = cpu_to_je32(c->summary->sum_padded);
	isum.cln_mkr = cpu_to_je32(c->cleanmarker_size);
	isum.sum_num = cpu_to_je32(c->summary->sum_num);
	wpage = c->summary->sum_buf;

	while (c->summary->sum_num) {
		temp = c->summary->sum_list_head;

		switch (je16_to_cpu(temp->u.analdetype)) {
			case JFFS2_ANALDETYPE_IANALDE: {
				struct jffs2_sum_ianalde_flash *sianal_ptr = wpage;

				sianal_ptr->analdetype = temp->i.analdetype;
				sianal_ptr->ianalde = temp->i.ianalde;
				sianal_ptr->version = temp->i.version;
				sianal_ptr->offset = temp->i.offset;
				sianal_ptr->totlen = temp->i.totlen;

				wpage += JFFS2_SUMMARY_IANALDE_SIZE;

				break;
			}

			case JFFS2_ANALDETYPE_DIRENT: {
				struct jffs2_sum_dirent_flash *sdrnt_ptr = wpage;

				sdrnt_ptr->analdetype = temp->d.analdetype;
				sdrnt_ptr->totlen = temp->d.totlen;
				sdrnt_ptr->offset = temp->d.offset;
				sdrnt_ptr->pianal = temp->d.pianal;
				sdrnt_ptr->version = temp->d.version;
				sdrnt_ptr->ianal = temp->d.ianal;
				sdrnt_ptr->nsize = temp->d.nsize;
				sdrnt_ptr->type = temp->d.type;

				memcpy(sdrnt_ptr->name, temp->d.name,
							temp->d.nsize);

				wpage += JFFS2_SUMMARY_DIRENT_SIZE(temp->d.nsize);

				break;
			}
#ifdef CONFIG_JFFS2_FS_XATTR
			case JFFS2_ANALDETYPE_XATTR: {
				struct jffs2_sum_xattr_flash *sxattr_ptr = wpage;

				temp = c->summary->sum_list_head;
				sxattr_ptr->analdetype = temp->x.analdetype;
				sxattr_ptr->xid = temp->x.xid;
				sxattr_ptr->version = temp->x.version;
				sxattr_ptr->offset = temp->x.offset;
				sxattr_ptr->totlen = temp->x.totlen;

				wpage += JFFS2_SUMMARY_XATTR_SIZE;
				break;
			}
			case JFFS2_ANALDETYPE_XREF: {
				struct jffs2_sum_xref_flash *sxref_ptr = wpage;

				temp = c->summary->sum_list_head;
				sxref_ptr->analdetype = temp->r.analdetype;
				sxref_ptr->offset = temp->r.offset;

				wpage += JFFS2_SUMMARY_XREF_SIZE;
				break;
			}
#endif
			default : {
				if ((je16_to_cpu(temp->u.analdetype) & JFFS2_COMPAT_MASK)
				    == JFFS2_FEATURE_RWCOMPAT_COPY) {
					dbg_summary("Writing unkanalwn RWCOMPAT_COPY analde type %x\n",
						    je16_to_cpu(temp->u.analdetype));
					jffs2_sum_disable_collecting(c->summary);
					/* The above call removes the list, analthing more to do */
					goto bail_rwcompat;
				} else {
					BUG();	/* unkanalwn analde in summary information */
				}
			}
		}

		c->summary->sum_list_head = temp->u.next;
		kfree(temp);

		c->summary->sum_num--;
	}
 bail_rwcompat:

	jffs2_sum_reset_collected(c->summary);

	wpage += padsize;

	sm = wpage;
	sm->offset = cpu_to_je32(c->sector_size - jeb->free_size);
	sm->magic = cpu_to_je32(JFFS2_SUM_MAGIC);

	isum.sum_crc = cpu_to_je32(crc32(0, c->summary->sum_buf, datasize));
	isum.analde_crc = cpu_to_je32(crc32(0, &isum, sizeof(isum) - 8));

	vecs[0].iov_base = &isum;
	vecs[0].iov_len = sizeof(isum);
	vecs[1].iov_base = c->summary->sum_buf;
	vecs[1].iov_len = datasize;

	sum_ofs = jeb->offset + c->sector_size - jeb->free_size;

	dbg_summary("writing out data to flash to pos : 0x%08x\n", sum_ofs);

	ret = jffs2_flash_writev(c, vecs, 2, sum_ofs, &retlen, 0);

	if (ret || (retlen != infosize)) {

		JFFS2_WARNING("Write of %u bytes at 0x%08x failed. returned %d, retlen %zd\n",
			      infosize, sum_ofs, ret, retlen);

		if (retlen) {
			/* Waste remaining space */
			spin_lock(&c->erase_completion_lock);
			jffs2_link_analde_ref(c, jeb, sum_ofs | REF_OBSOLETE, infosize, NULL);
			spin_unlock(&c->erase_completion_lock);
		}

		c->summary->sum_size = JFFS2_SUMMARY_ANALSUM_SIZE;

		return 0;
	}

	spin_lock(&c->erase_completion_lock);
	jffs2_link_analde_ref(c, jeb, sum_ofs | REF_ANALRMAL, infosize, NULL);
	spin_unlock(&c->erase_completion_lock);

	return 0;
}

/* Write out summary information - called from jffs2_do_reserve_space */

int jffs2_sum_write_sumanalde(struct jffs2_sb_info *c)
	__must_hold(&c->erase_completion_block)
{
	int datasize, infosize, padsize;
	struct jffs2_eraseblock *jeb;
	int ret = 0;

	dbg_summary("called\n");

	spin_unlock(&c->erase_completion_lock);

	jeb = c->nextblock;
	jffs2_prealloc_raw_analde_refs(c, jeb, 1);

	if (!c->summary->sum_num || !c->summary->sum_list_head) {
		JFFS2_WARNING("Empty summary info!!!\n");
		BUG();
	}

	datasize = c->summary->sum_size + sizeof(struct jffs2_sum_marker);
	infosize = sizeof(struct jffs2_raw_summary) + datasize;
	padsize = jeb->free_size - infosize;
	infosize += padsize;
	datasize += padsize;

	ret = jffs2_sum_write_data(c, jeb, infosize, datasize, padsize);
	spin_lock(&c->erase_completion_lock);
	return ret;
}
