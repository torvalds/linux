/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2004  Ferenc Havasi <havasi@inf.u-szeged.hu>,
 *                     Zoltan Sogor <weth@inf.u-szeged.hu>,
 *                     Patrik Kluba <pajko@halom.u-szeged.hu>,
 *                     University of Szeged, Hungary
 *               2005  KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: summary.c,v 1.4 2005/09/26 11:37:21 havasi Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include <linux/compiler.h>
#include <linux/vmalloc.h>
#include "nodelist.h"
#include "debug.h"

int jffs2_sum_init(struct jffs2_sb_info *c)
{
	c->summary = kmalloc(sizeof(struct jffs2_summary), GFP_KERNEL);

	if (!c->summary) {
		JFFS2_WARNING("Can't allocate memory for summary information!\n");
		return -ENOMEM;
	}

	memset(c->summary, 0, sizeof(struct jffs2_summary));

	c->summary->sum_buf = vmalloc(c->sector_size);

	if (!c->summary->sum_buf) {
		JFFS2_WARNING("Can't allocate buffer for writing out summary information!\n");
		kfree(c->summary);
		return -ENOMEM;
	}

	dbg_summary("returned succesfully\n");

	return 0;
}

void jffs2_sum_exit(struct jffs2_sb_info *c)
{
	dbg_summary("called\n");

	jffs2_sum_disable_collecting(c->summary);

	vfree(c->summary->sum_buf);
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

	switch (je16_to_cpu(item->u.nodetype)) {
		case JFFS2_NODETYPE_INODE:
			s->sum_size += JFFS2_SUMMARY_INODE_SIZE;
			s->sum_num++;
			dbg_summary("inode (%u) added to summary\n",
						je32_to_cpu(item->i.inode));
			break;
		case JFFS2_NODETYPE_DIRENT:
			s->sum_size += JFFS2_SUMMARY_DIRENT_SIZE(item->d.nsize);
			s->sum_num++;
			dbg_summary("dirent (%u) added to summary\n",
						je32_to_cpu(item->d.ino));
			break;
#ifdef CONFIG_JFFS2_FS_XATTR
		case JFFS2_NODETYPE_XATTR:
			s->sum_size += JFFS2_SUMMARY_XATTR_SIZE;
			s->sum_num++;
			dbg_summary("xattr (xid=%u, version=%u) added to summary\n",
				    je32_to_cpu(item->x.xid), je32_to_cpu(item->x.version));
			break;
		case JFFS2_NODETYPE_XREF:
			s->sum_size += JFFS2_SUMMARY_XREF_SIZE;
			s->sum_num++;
			dbg_summary("xref added to summary\n");
			break;
#endif
		default:
			JFFS2_WARNING("UNKNOWN node type %u\n",
					    je16_to_cpu(item->u.nodetype));
			return 1;
	}
	return 0;
}


/* The following 3 functions are called from scan.c to collect summary info for not closed jeb */

int jffs2_sum_add_padding_mem(struct jffs2_summary *s, uint32_t size)
{
	dbg_summary("called with %u\n", size);
	s->sum_padded += size;
	return 0;
}

int jffs2_sum_add_inode_mem(struct jffs2_summary *s, struct jffs2_raw_inode *ri,
				uint32_t ofs)
{
	struct jffs2_sum_inode_mem *temp = kmalloc(sizeof(struct jffs2_sum_inode_mem), GFP_KERNEL);

	if (!temp)
		return -ENOMEM;

	temp->nodetype = ri->nodetype;
	temp->inode = ri->ino;
	temp->version = ri->version;
	temp->offset = cpu_to_je32(ofs); /* relative offset from the begining of the jeb */
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
		return -ENOMEM;

	temp->nodetype = rd->nodetype;
	temp->totlen = rd->totlen;
	temp->offset = cpu_to_je32(ofs);	/* relative from the begining of the jeb */
	temp->pino = rd->pino;
	temp->version = rd->version;
	temp->ino = rd->ino;
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
		return -ENOMEM;

	temp->nodetype = rx->nodetype;
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
		return -ENOMEM;

	temp->nodetype = rr->nodetype;
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
	s->sum_size = JFFS2_SUMMARY_NOSUM_SIZE;
}

int jffs2_sum_is_disabled(struct jffs2_summary *s)
{
	return (s->sum_size == JFFS2_SUMMARY_NOSUM_SIZE);
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

/* Called from wbuf.c to collect writed node info */

int jffs2_sum_add_kvec(struct jffs2_sb_info *c, const struct kvec *invecs,
				unsigned long count, uint32_t ofs)
{
	union jffs2_node_union *node;
	struct jffs2_eraseblock *jeb;

	node = invecs[0].iov_base;
	jeb = &c->blocks[ofs / c->sector_size];
	ofs -= jeb->offset;

	switch (je16_to_cpu(node->u.nodetype)) {
		case JFFS2_NODETYPE_INODE: {
			struct jffs2_sum_inode_mem *temp =
				kmalloc(sizeof(struct jffs2_sum_inode_mem), GFP_KERNEL);

			if (!temp)
				goto no_mem;

			temp->nodetype = node->i.nodetype;
			temp->inode = node->i.ino;
			temp->version = node->i.version;
			temp->offset = cpu_to_je32(ofs);
			temp->totlen = node->i.totlen;
			temp->next = NULL;

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}

		case JFFS2_NODETYPE_DIRENT: {
			struct jffs2_sum_dirent_mem *temp =
				kmalloc(sizeof(struct jffs2_sum_dirent_mem) + node->d.nsize, GFP_KERNEL);

			if (!temp)
				goto no_mem;

			temp->nodetype = node->d.nodetype;
			temp->totlen = node->d.totlen;
			temp->offset = cpu_to_je32(ofs);
			temp->pino = node->d.pino;
			temp->version = node->d.version;
			temp->ino = node->d.ino;
			temp->nsize = node->d.nsize;
			temp->type = node->d.type;
			temp->next = NULL;

			switch (count) {
				case 1:
					memcpy(temp->name,node->d.name,node->d.nsize);
					break;

				case 2:
					memcpy(temp->name,invecs[1].iov_base,node->d.nsize);
					break;

				default:
					BUG();	/* impossible count value */
					break;
			}

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}
#ifdef CONFIG_JFFS2_FS_XATTR
		case JFFS2_NODETYPE_XATTR: {
			struct jffs2_sum_xattr_mem *temp;
			if (je32_to_cpu(node->x.version) == 0xffffffff)
				return 0;
			temp = kmalloc(sizeof(struct jffs2_sum_xattr_mem), GFP_KERNEL);
			if (!temp)
				goto no_mem;

			temp->nodetype = node->x.nodetype;
			temp->xid = node->x.xid;
			temp->version = node->x.version;
			temp->totlen = node->x.totlen;
			temp->offset = cpu_to_je32(ofs);
			temp->next = NULL;

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}
		case JFFS2_NODETYPE_XREF: {
			struct jffs2_sum_xref_mem *temp;

			if (je32_to_cpu(node->r.ino) == 0xffffffff
			    && je32_to_cpu(node->r.xid) == 0xffffffff)
				return 0;
			temp = kmalloc(sizeof(struct jffs2_sum_xref_mem), GFP_KERNEL);
			if (!temp)
				goto no_mem;
			temp->nodetype = node->r.nodetype;
			temp->offset = cpu_to_je32(ofs);
			temp->next = NULL;

			return jffs2_sum_add_mem(c->summary, (union jffs2_sum_mem *)temp);
		}
#endif
		case JFFS2_NODETYPE_PADDING:
			dbg_summary("node PADDING\n");
			c->summary->sum_padded += je32_to_cpu(node->u.totlen);
			break;

		case JFFS2_NODETYPE_CLEANMARKER:
			dbg_summary("node CLEANMARKER\n");
			break;

		case JFFS2_NODETYPE_SUMMARY:
			dbg_summary("node SUMMARY\n");
			break;

		default:
			/* If you implement a new node type you should also implement
			   summary support for it or disable summary.
			*/
			BUG();
			break;
	}

	return 0;

no_mem:
	JFFS2_WARNING("MEMORY ALLOCATION ERROR!");
	return -ENOMEM;
}

static struct jffs2_raw_node_ref *sum_link_node_ref(struct jffs2_sb_info *c,
						    struct jffs2_eraseblock *jeb,
						    uint32_t ofs, uint32_t len,
						    struct jffs2_inode_cache *ic)
{
	/* If there was a gap, mark it dirty */
	if ((ofs & ~3) > c->sector_size - jeb->free_size) {
		/* Ew. Summary doesn't actually tell us explicitly about dirty space */
		jffs2_scan_dirty_space(c, jeb, (ofs & ~3) - (c->sector_size - jeb->free_size));
	}

	return jffs2_link_node_ref(c, jeb, jeb->offset + ofs, len, ic);
}

/* Process the stored summary information - helper function for jffs2_sum_scan_sumnode() */

static int jffs2_sum_process_sum_data(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				struct jffs2_raw_summary *summary, uint32_t *pseudo_random)
{
	struct jffs2_inode_cache *ic;
	struct jffs2_full_dirent *fd;
	void *sp;
	int i, ino;
	int err;

	sp = summary->sum;

	for (i=0; i<je32_to_cpu(summary->sum_num); i++) {
		dbg_summary("processing summary index %d\n", i);

		/* Make sure there's a spare ref for dirty space */
		err = jffs2_prealloc_raw_node_refs(c, jeb, 2);
		if (err)
			return err;

		switch (je16_to_cpu(((struct jffs2_sum_unknown_flash *)sp)->nodetype)) {
			case JFFS2_NODETYPE_INODE: {
				struct jffs2_sum_inode_flash *spi;
				spi = sp;

				ino = je32_to_cpu(spi->inode);

				dbg_summary("Inode at 0x%08x-0x%08x\n",
					    jeb->offset + je32_to_cpu(spi->offset),
					    jeb->offset + je32_to_cpu(spi->offset) + je32_to_cpu(spi->totlen));

				ic = jffs2_scan_make_ino_cache(c, ino);
				if (!ic) {
					JFFS2_NOTICE("scan_make_ino_cache failed\n");
					return -ENOMEM;
				}

				sum_link_node_ref(c, jeb, je32_to_cpu(spi->offset) | REF_UNCHECKED,
						  PAD(je32_to_cpu(spi->totlen)), ic);

				*pseudo_random += je32_to_cpu(spi->version);

				sp += JFFS2_SUMMARY_INODE_SIZE;

				break;
			}

			case JFFS2_NODETYPE_DIRENT: {
				struct jffs2_sum_dirent_flash *spd;
				spd = sp;

				dbg_summary("Dirent at 0x%08x-0x%08x\n",
					    jeb->offset + je32_to_cpu(spd->offset),
					    jeb->offset + je32_to_cpu(spd->offset) + je32_to_cpu(spd->totlen));


				fd = jffs2_alloc_full_dirent(spd->nsize+1);
				if (!fd)
					return -ENOMEM;

				memcpy(&fd->name, spd->name, spd->nsize);
				fd->name[spd->nsize] = 0;

				ic = jffs2_scan_make_ino_cache(c, je32_to_cpu(spd->pino));
				if (!ic) {
					jffs2_free_full_dirent(fd);
					return -ENOMEM;
				}

				fd->raw = sum_link_node_ref(c, jeb,  je32_to_cpu(spd->offset) | REF_PRISTINE,
							    PAD(je32_to_cpu(spd->totlen)), ic);

				fd->next = NULL;
				fd->version = je32_to_cpu(spd->version);
				fd->ino = je32_to_cpu(spd->ino);
				fd->nhash = full_name_hash(fd->name, spd->nsize);
				fd->type = spd->type;

				jffs2_add_fd_to_list(c, fd, &ic->scan_dents);

				*pseudo_random += je32_to_cpu(spd->version);

				sp += JFFS2_SUMMARY_DIRENT_SIZE(spd->nsize);

				break;
			}
#ifdef CONFIG_JFFS2_FS_XATTR
			case JFFS2_NODETYPE_XATTR: {
				struct jffs2_xattr_datum *xd;
				struct jffs2_sum_xattr_flash *spx;

				spx = (struct jffs2_sum_xattr_flash *)sp;
				dbg_summary("xattr at %#08x-%#08x (xid=%u, version=%u)\n", 
					    jeb->offset + je32_to_cpu(spx->offset),
					    jeb->offset + je32_to_cpu(spx->offset) + je32_to_cpu(spx->totlen),
					    je32_to_cpu(spx->xid), je32_to_cpu(spx->version));

				xd = jffs2_setup_xattr_datum(c, je32_to_cpu(spx->xid),
								je32_to_cpu(spx->version));
				if (IS_ERR(xd)) {
					if (PTR_ERR(xd) == -EEXIST) {
						/* a newer version of xd exists */
						if ((err = jffs2_scan_dirty_space(c, jeb, je32_to_cpu(spx->totlen))))
							return err;
						sp += JFFS2_SUMMARY_XATTR_SIZE;
						break;
					}
					JFFS2_NOTICE("allocation of xattr_datum failed\n");
					return PTR_ERR(xd);
				}

				xd->node = sum_link_node_ref(c, jeb, je32_to_cpu(spx->offset) | REF_UNCHECKED,
							     PAD(je32_to_cpu(spx->totlen)), NULL);
				/* FIXME */ xd->node->next_in_ino = (void *)xd;

				*pseudo_random += je32_to_cpu(spx->xid);
				sp += JFFS2_SUMMARY_XATTR_SIZE;

				break;
			}
			case JFFS2_NODETYPE_XREF: {
				struct jffs2_xattr_ref *ref;
				struct jffs2_sum_xref_flash *spr;

				spr = (struct jffs2_sum_xref_flash *)sp;
				dbg_summary("xref at %#08x-%#08x\n",
					    jeb->offset + je32_to_cpu(spr->offset),
					    jeb->offset + je32_to_cpu(spr->offset) + 
					    (uint32_t)PAD(sizeof(struct jffs2_raw_xref)));

				ref = jffs2_alloc_xattr_ref();
				if (!ref) {
					JFFS2_NOTICE("allocation of xattr_datum failed\n");
					return -ENOMEM;
				}
				ref->ino = 0xfffffffe;
				ref->xid = 0xfffffffd;
				ref->next = c->xref_temp;
				c->xref_temp = ref;

				ref->node = sum_link_node_ref(c, jeb, je32_to_cpu(spr->offset) | REF_UNCHECKED,
							      PAD(sizeof(struct jffs2_raw_xref)), NULL);
				/* FIXME */ ref->node->next_in_ino = (void *)ref;

				*pseudo_random += ref->node->flash_offset;
				sp += JFFS2_SUMMARY_XREF_SIZE;

				break;
			}
#endif
			default : {
				uint16_t nodetype = je16_to_cpu(((struct jffs2_sum_unknown_flash *)sp)->nodetype);
				JFFS2_WARNING("Unsupported node type %x found in summary! Exiting...\n", nodetype);
				if ((nodetype & JFFS2_COMPAT_MASK) == JFFS2_FEATURE_INCOMPAT)
					return -EIO;

				/* For compatible node types, just fall back to the full scan */
				c->wasted_size -= jeb->wasted_size;
				c->free_size += c->sector_size - jeb->free_size;
				c->used_size -= jeb->used_size;
				c->dirty_size -= jeb->dirty_size;
				jeb->wasted_size = jeb->used_size = jeb->dirty_size = 0;
				jeb->free_size = c->sector_size;

				jffs2_free_jeb_node_refs(c, jeb);
				return -ENOTRECOVERABLE;
			}
		}
	}
	return 0;
}

/* Process the summary node - called from jffs2_scan_eraseblock() */
int jffs2_sum_scan_sumnode(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
			   struct jffs2_raw_summary *summary, uint32_t sumsize,
			   uint32_t *pseudo_random)
{
	struct jffs2_unknown_node crcnode;
	int ret, ofs;
	uint32_t crc;
	int err;

	ofs = c->sector_size - sumsize;

	dbg_summary("summary found for 0x%08x at 0x%08x (0x%x bytes)\n",
		    jeb->offset, jeb->offset + ofs, sumsize);

	/* OK, now check for node validity and CRC */
	crcnode.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	crcnode.nodetype = cpu_to_je16(JFFS2_NODETYPE_SUMMARY);
	crcnode.totlen = summary->totlen;
	crc = crc32(0, &crcnode, sizeof(crcnode)-4);

	if (je32_to_cpu(summary->hdr_crc) != crc) {
		dbg_summary("Summary node header is corrupt (bad CRC or "
				"no summary at all)\n");
		goto crc_err;
	}

	if (je32_to_cpu(summary->totlen) != sumsize) {
		dbg_summary("Summary node is corrupt (wrong erasesize?)\n");
		goto crc_err;
	}

	crc = crc32(0, summary, sizeof(struct jffs2_raw_summary)-8);

	if (je32_to_cpu(summary->node_crc) != crc) {
		dbg_summary("Summary node is corrupt (bad CRC)\n");
		goto crc_err;
	}

	crc = crc32(0, summary->sum, sumsize - sizeof(struct jffs2_raw_summary));

	if (je32_to_cpu(summary->sum_crc) != crc) {
		dbg_summary("Summary node data is corrupt (bad CRC)\n");
		goto crc_err;
	}

	if ( je32_to_cpu(summary->cln_mkr) ) {

		dbg_summary("Summary : CLEANMARKER node \n");

		if (je32_to_cpu(summary->cln_mkr) != c->cleanmarker_size) {
			dbg_summary("CLEANMARKER node has totlen 0x%x != normal 0x%x\n",
				je32_to_cpu(summary->cln_mkr), c->cleanmarker_size);
			if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(summary->cln_mkr)))))
				return err;
		} else if (jeb->first_node) {
			dbg_summary("CLEANMARKER node not first node in block "
					"(0x%08x)\n", jeb->offset);
			if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(summary->cln_mkr)))))
				return err;
		} else {
			jffs2_link_node_ref(c, jeb, jeb->offset | REF_NORMAL,
					    je32_to_cpu(summary->cln_mkr), NULL);
		}
	}

	ret = jffs2_sum_process_sum_data(c, jeb, summary, pseudo_random);
	/* -ENOTRECOVERABLE isn't a fatal error -- it means we should do a full
	   scan of this eraseblock. So return zero */
	if (ret == -ENOTRECOVERABLE)
		return 0;
	if (ret)
		return ret;		/* real error */

	/* for PARANOIA_CHECK */
	ret = jffs2_prealloc_raw_node_refs(c, jeb, 2);
	if (ret)
		return ret;

	sum_link_node_ref(c, jeb, ofs | REF_NORMAL, sumsize, NULL);

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
	JFFS2_WARNING("Summary node crc error, skipping summary information.\n");

	return 0;
}

/* Write summary data to flash - helper function for jffs2_sum_write_sumnode() */

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

	memset(c->summary->sum_buf, 0xff, datasize);
	memset(&isum, 0, sizeof(isum));

	isum.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	isum.nodetype = cpu_to_je16(JFFS2_NODETYPE_SUMMARY);
	isum.totlen = cpu_to_je32(infosize);
	isum.hdr_crc = cpu_to_je32(crc32(0, &isum, sizeof(struct jffs2_unknown_node) - 4));
	isum.padded = cpu_to_je32(c->summary->sum_padded);
	isum.cln_mkr = cpu_to_je32(c->cleanmarker_size);
	isum.sum_num = cpu_to_je32(c->summary->sum_num);
	wpage = c->summary->sum_buf;

	while (c->summary->sum_num) {
		temp = c->summary->sum_list_head;

		switch (je16_to_cpu(temp->u.nodetype)) {
			case JFFS2_NODETYPE_INODE: {
				struct jffs2_sum_inode_flash *sino_ptr = wpage;

				sino_ptr->nodetype = temp->i.nodetype;
				sino_ptr->inode = temp->i.inode;
				sino_ptr->version = temp->i.version;
				sino_ptr->offset = temp->i.offset;
				sino_ptr->totlen = temp->i.totlen;

				wpage += JFFS2_SUMMARY_INODE_SIZE;

				break;
			}

			case JFFS2_NODETYPE_DIRENT: {
				struct jffs2_sum_dirent_flash *sdrnt_ptr = wpage;

				sdrnt_ptr->nodetype = temp->d.nodetype;
				sdrnt_ptr->totlen = temp->d.totlen;
				sdrnt_ptr->offset = temp->d.offset;
				sdrnt_ptr->pino = temp->d.pino;
				sdrnt_ptr->version = temp->d.version;
				sdrnt_ptr->ino = temp->d.ino;
				sdrnt_ptr->nsize = temp->d.nsize;
				sdrnt_ptr->type = temp->d.type;

				memcpy(sdrnt_ptr->name, temp->d.name,
							temp->d.nsize);

				wpage += JFFS2_SUMMARY_DIRENT_SIZE(temp->d.nsize);

				break;
			}
#ifdef CONFIG_JFFS2_FS_XATTR
			case JFFS2_NODETYPE_XATTR: {
				struct jffs2_sum_xattr_flash *sxattr_ptr = wpage;

				temp = c->summary->sum_list_head;
				sxattr_ptr->nodetype = temp->x.nodetype;
				sxattr_ptr->xid = temp->x.xid;
				sxattr_ptr->version = temp->x.version;
				sxattr_ptr->offset = temp->x.offset;
				sxattr_ptr->totlen = temp->x.totlen;

				wpage += JFFS2_SUMMARY_XATTR_SIZE;
				break;
			}
			case JFFS2_NODETYPE_XREF: {
				struct jffs2_sum_xref_flash *sxref_ptr = wpage;

				temp = c->summary->sum_list_head;
				sxref_ptr->nodetype = temp->r.nodetype;
				sxref_ptr->offset = temp->r.offset;

				wpage += JFFS2_SUMMARY_XREF_SIZE;
				break;
			}
#endif
			default : {
				if ((je16_to_cpu(temp->u.nodetype) & JFFS2_COMPAT_MASK)
				    == JFFS2_FEATURE_RWCOMPAT_COPY) {
					dbg_summary("Writing unknown RWCOMPAT_COPY node type %x\n",
						    je16_to_cpu(temp->u.nodetype));
					jffs2_sum_disable_collecting(c->summary);
				} else {
					BUG();	/* unknown node in summary information */
				}
			}
		}

		c->summary->sum_list_head = temp->u.next;
		kfree(temp);

		c->summary->sum_num--;
	}

	jffs2_sum_reset_collected(c->summary);

	wpage += padsize;

	sm = wpage;
	sm->offset = cpu_to_je32(c->sector_size - jeb->free_size);
	sm->magic = cpu_to_je32(JFFS2_SUM_MAGIC);

	isum.sum_crc = cpu_to_je32(crc32(0, c->summary->sum_buf, datasize));
	isum.node_crc = cpu_to_je32(crc32(0, &isum, sizeof(isum) - 8));

	vecs[0].iov_base = &isum;
	vecs[0].iov_len = sizeof(isum);
	vecs[1].iov_base = c->summary->sum_buf;
	vecs[1].iov_len = datasize;

	sum_ofs = jeb->offset + c->sector_size - jeb->free_size;

	dbg_summary("JFFS2: writing out data to flash to pos : 0x%08x\n",
		    sum_ofs);

	ret = jffs2_flash_writev(c, vecs, 2, sum_ofs, &retlen, 0);

	if (ret || (retlen != infosize)) {

		JFFS2_WARNING("Write of %u bytes at 0x%08x failed. returned %d, retlen %zd\n",
			      infosize, sum_ofs, ret, retlen);

		if (retlen) {
			/* Waste remaining space */
			spin_lock(&c->erase_completion_lock);
			jffs2_link_node_ref(c, jeb, sum_ofs | REF_OBSOLETE, infosize, NULL);
			spin_unlock(&c->erase_completion_lock);
		}

		c->summary->sum_size = JFFS2_SUMMARY_NOSUM_SIZE;

		return 0;
	}

	spin_lock(&c->erase_completion_lock);
	jffs2_link_node_ref(c, jeb, sum_ofs | REF_NORMAL, infosize, NULL);
	spin_unlock(&c->erase_completion_lock);

	return 0;
}

/* Write out summary information - called from jffs2_do_reserve_space */

int jffs2_sum_write_sumnode(struct jffs2_sb_info *c)
{
	int datasize, infosize, padsize;
	struct jffs2_eraseblock *jeb;
	int ret;

	dbg_summary("called\n");

	spin_unlock(&c->erase_completion_lock);

	jeb = c->nextblock;
	jffs2_prealloc_raw_node_refs(c, jeb, 1);

	if (!c->summary->sum_num || !c->summary->sum_list_head) {
		JFFS2_WARNING("Empty summary info!!!\n");
		BUG();
	}

	datasize = c->summary->sum_size + sizeof(struct jffs2_sum_marker);
	infosize = sizeof(struct jffs2_raw_summary) + datasize;
	padsize = jeb->free_size - infosize;
	infosize += padsize;
	datasize += padsize;

	/* Is there enough space for summary? */
	if (padsize < 0) {
		/* don't try to write out summary for this jeb */
		jffs2_sum_disable_collecting(c->summary);

		JFFS2_WARNING("Not enough space for summary, padsize = %d\n", padsize);
		spin_lock(&c->erase_completion_lock);
		return 0;
	}

	ret = jffs2_sum_write_data(c, jeb, infosize, datasize, padsize);
	spin_lock(&c->erase_completion_lock);
	return ret;
}
