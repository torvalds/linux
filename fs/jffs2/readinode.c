/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: readinode.c,v 1.143 2005/11/07 11:14:41 gleixner Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include "nodelist.h"

/*
 * Put a new tmp_dnode_info into the temporaty RB-tree, keeping the list in
 * order of increasing version.
 */
static void jffs2_add_tn_to_tree(struct jffs2_tmp_dnode_info *tn, struct rb_root *list)
{
	struct rb_node **p = &list->rb_node;
	struct rb_node * parent = NULL;
	struct jffs2_tmp_dnode_info *this;

	while (*p) {
		parent = *p;
		this = rb_entry(parent, struct jffs2_tmp_dnode_info, rb);

		/* There may actually be a collision here, but it doesn't
		   actually matter. As long as the two nodes with the same
		   version are together, it's all fine. */
		if (tn->version > this->version)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&tn->rb, parent, p);
	rb_insert_color(&tn->rb, list);
}

static void jffs2_free_tmp_dnode_info_list(struct rb_root *list)
{
	struct rb_node *this;
	struct jffs2_tmp_dnode_info *tn;

	this = list->rb_node;

	/* Now at bottom of tree */
	while (this) {
		if (this->rb_left)
			this = this->rb_left;
		else if (this->rb_right)
			this = this->rb_right;
		else {
			tn = rb_entry(this, struct jffs2_tmp_dnode_info, rb);
			jffs2_free_full_dnode(tn->fn);
			jffs2_free_tmp_dnode_info(tn);

			this = this->rb_parent;
			if (!this)
				break;

			if (this->rb_left == &tn->rb)
				this->rb_left = NULL;
			else if (this->rb_right == &tn->rb)
				this->rb_right = NULL;
			else BUG();
		}
	}
	list->rb_node = NULL;
}

static void jffs2_free_full_dirent_list(struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent *next;

	while (fd) {
		next = fd->next;
		jffs2_free_full_dirent(fd);
		fd = next;
	}
}

/* Returns first valid node after 'ref'. May return 'ref' */
static struct jffs2_raw_node_ref *jffs2_first_valid_node(struct jffs2_raw_node_ref *ref)
{
	while (ref && ref->next_in_ino) {
		if (!ref_obsolete(ref))
			return ref;
		dbg_noderef("node at 0x%08x is obsoleted. Ignoring.\n", ref_offset(ref));
		ref = ref->next_in_ino;
	}
	return NULL;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * It is called every time an directory entry node is found.
 *
 * Returns: 0 on succes;
 * 	    1 if the node should be marked obsolete;
 * 	    negative error code on failure.
 */
static inline int read_direntry(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *ref,
				struct jffs2_raw_dirent *rd, size_t read, struct jffs2_full_dirent **fdp,
				uint32_t *latest_mctime, uint32_t *mctime_ver)
{
	struct jffs2_full_dirent *fd;

	/* The direntry nodes are checked during the flash scanning */
	BUG_ON(ref_flags(ref) == REF_UNCHECKED);
	/* Obsoleted. This cannot happen, surely? dwmw2 20020308 */
	BUG_ON(ref_obsolete(ref));

	/* Sanity check */
	if (unlikely(PAD((rd->nsize + sizeof(*rd))) != PAD(je32_to_cpu(rd->totlen)))) {
		JFFS2_ERROR("illegal nsize in node at %#08x: nsize %#02x, totlen %#04x\n",
		       ref_offset(ref), rd->nsize, je32_to_cpu(rd->totlen));
		return 1;
	}

	fd = jffs2_alloc_full_dirent(rd->nsize + 1);
	if (unlikely(!fd))
		return -ENOMEM;

	fd->raw = ref;
	fd->version = je32_to_cpu(rd->version);
	fd->ino = je32_to_cpu(rd->ino);
	fd->type = rd->type;

	/* Pick out the mctime of the latest dirent */
	if(fd->version > *mctime_ver && je32_to_cpu(rd->mctime)) {
		*mctime_ver = fd->version;
		*latest_mctime = je32_to_cpu(rd->mctime);
	}

	/*
	 * Copy as much of the name as possible from the raw
	 * dirent we've already read from the flash.
	 */
	if (read > sizeof(*rd))
		memcpy(&fd->name[0], &rd->name[0],
		       min_t(uint32_t, rd->nsize, (read - sizeof(*rd)) ));

	/* Do we need to copy any more of the name directly from the flash? */
	if (rd->nsize + sizeof(*rd) > read) {
		/* FIXME: point() */
		int err;
		int already = read - sizeof(*rd);

		err = jffs2_flash_read(c, (ref_offset(ref)) + read,
				rd->nsize - already, &read, &fd->name[already]);
		if (unlikely(read != rd->nsize - already) && likely(!err))
			return -EIO;

		if (unlikely(err)) {
			JFFS2_ERROR("read remainder of name: error %d\n", err);
			jffs2_free_full_dirent(fd);
			return -EIO;
		}
	}

	fd->nhash = full_name_hash(fd->name, rd->nsize);
	fd->next = NULL;
	fd->name[rd->nsize] = '\0';

	/*
	 * Wheee. We now have a complete jffs2_full_dirent structure, with
	 * the name in it and everything. Link it into the list
	 */
	jffs2_add_fd_to_list(c, fd, fdp);

	return 0;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * It is called every time an inode node is found.
 *
 * Returns: 0 on succes;
 * 	    1 if the node should be marked obsolete;
 * 	    negative error code on failure.
 */
static inline int read_dnode(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *ref,
			     struct jffs2_raw_inode *rd, struct rb_root *tnp, int rdlen,
			     uint32_t *latest_mctime, uint32_t *mctime_ver)
{
	struct jffs2_tmp_dnode_info *tn;
	uint32_t len, csize;
	int ret = 1;

	/* Obsoleted. This cannot happen, surely? dwmw2 20020308 */
	BUG_ON(ref_obsolete(ref));

	tn = jffs2_alloc_tmp_dnode_info();
	if (!tn) {
		JFFS2_ERROR("failed to allocate tn (%zu bytes).\n", sizeof(*tn));
		return -ENOMEM;
	}

	tn->partial_crc = 0;
	csize = je32_to_cpu(rd->csize);

	/* If we've never checked the CRCs on this node, check them now */
	if (ref_flags(ref) == REF_UNCHECKED) {
		uint32_t crc;

		crc = crc32(0, rd, sizeof(*rd) - 8);
		if (unlikely(crc != je32_to_cpu(rd->node_crc))) {
			JFFS2_NOTICE("header CRC failed on node at %#08x: read %#08x, calculated %#08x\n",
					ref_offset(ref), je32_to_cpu(rd->node_crc), crc);
			goto free_out;
		}

		/* Sanity checks */
		if (unlikely(je32_to_cpu(rd->offset) > je32_to_cpu(rd->isize)) ||
		    unlikely(PAD(je32_to_cpu(rd->csize) + sizeof(*rd)) != PAD(je32_to_cpu(rd->totlen)))) {
				JFFS2_WARNING("inode node header CRC is corrupted at %#08x\n", ref_offset(ref));
				jffs2_dbg_dump_node(c, ref_offset(ref));
			goto free_out;
		}

		if (jffs2_is_writebuffered(c) && csize != 0) {
			/* At this point we are supposed to check the data CRC
			 * of our unchecked node. But thus far, we do not
			 * know whether the node is valid or obsolete. To
			 * figure this out, we need to walk all the nodes of
			 * the inode and build the inode fragtree. We don't
			 * want to spend time checking data of nodes which may
			 * later be found to be obsolete. So we put off the full
			 * data CRC checking until we have read all the inode
			 * nodes and have started building the fragtree.
			 *
			 * The fragtree is being built starting with nodes
			 * having the highest version number, so we'll be able
			 * to detect whether a node is valid (i.e., it is not
			 * overlapped by a node with higher version) or not.
			 * And we'll be able to check only those nodes, which
			 * are not obsolete.
			 *
			 * Of course, this optimization only makes sense in case
			 * of NAND flashes (or other flashes whith
			 * !jffs2_can_mark_obsolete()), since on NOR flashes
			 * nodes are marked obsolete physically.
			 *
			 * Since NAND flashes (or other flashes with
			 * jffs2_is_writebuffered(c)) are anyway read by
			 * fractions of c->wbuf_pagesize, and we have just read
			 * the node header, it is likely that the starting part
			 * of the node data is also read when we read the
			 * header. So we don't mind to check the CRC of the
			 * starting part of the data of the node now, and check
			 * the second part later (in jffs2_check_node_data()).
			 * Of course, we will not need to re-read and re-check
			 * the NAND page which we have just read. This is why we
			 * read the whole NAND page at jffs2_get_inode_nodes(),
			 * while we needed only the node header.
			 */
			unsigned char *buf;

			/* 'buf' will point to the start of data */
			buf = (unsigned char *)rd + sizeof(*rd);
			/* len will be the read data length */
			len = min_t(uint32_t, rdlen - sizeof(*rd), csize);
			tn->partial_crc = crc32(0, buf, len);

			dbg_readinode("Calculates CRC (%#08x) for %d bytes, csize %d\n", tn->partial_crc, len, csize);

			/* If we actually calculated the whole data CRC
			 * and it is wrong, drop the node. */
			if (len >= csize && unlikely(tn->partial_crc != je32_to_cpu(rd->data_crc))) {
				JFFS2_NOTICE("wrong data CRC in data node at 0x%08x: read %#08x, calculated %#08x.\n",
					ref_offset(ref), tn->partial_crc, je32_to_cpu(rd->data_crc));
				goto free_out;
			}

		} else if (csize == 0) {
			/*
			 * We checked the header CRC. If the node has no data, adjust
			 * the space accounting now. For other nodes this will be done
			 * later either when the node is marked obsolete or when its
			 * data is checked.
			 */
			struct jffs2_eraseblock *jeb;

			dbg_readinode("the node has no data.\n");
			jeb = &c->blocks[ref->flash_offset / c->sector_size];
			len = ref_totlen(c, jeb, ref);

			spin_lock(&c->erase_completion_lock);
			jeb->used_size += len;
			jeb->unchecked_size -= len;
			c->used_size += len;
			c->unchecked_size -= len;
			ref->flash_offset = ref_offset(ref) | REF_NORMAL;
			spin_unlock(&c->erase_completion_lock);
		}
	}

	tn->fn = jffs2_alloc_full_dnode();
	if (!tn->fn) {
		JFFS2_ERROR("alloc fn failed\n");
		ret = -ENOMEM;
		goto free_out;
	}

	tn->version = je32_to_cpu(rd->version);
	tn->fn->ofs = je32_to_cpu(rd->offset);
	tn->data_crc = je32_to_cpu(rd->data_crc);
	tn->csize = csize;
	tn->fn->raw = ref;

	/* There was a bug where we wrote hole nodes out with
	   csize/dsize swapped. Deal with it */
	if (rd->compr == JFFS2_COMPR_ZERO && !je32_to_cpu(rd->dsize) && csize)
		tn->fn->size = csize;
	else // normal case...
		tn->fn->size = je32_to_cpu(rd->dsize);

	dbg_readinode("dnode @%08x: ver %u, offset %#04x, dsize %#04x, csize %#04x\n",
		  ref_offset(ref), je32_to_cpu(rd->version), je32_to_cpu(rd->offset), je32_to_cpu(rd->dsize), csize);

	jffs2_add_tn_to_tree(tn, tnp);

	return 0;

free_out:
	jffs2_free_tmp_dnode_info(tn);
	return ret;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * It is called every time an unknown node is found.
 *
 * Returns: 0 on succes;
 * 	    1 if the node should be marked obsolete;
 * 	    negative error code on failure.
 */
static inline int read_unknown(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *ref, struct jffs2_unknown_node *un)
{
	/* We don't mark unknown nodes as REF_UNCHECKED */
	BUG_ON(ref_flags(ref) == REF_UNCHECKED);

	un->nodetype = cpu_to_je16(JFFS2_NODE_ACCURATE | je16_to_cpu(un->nodetype));

	if (crc32(0, un, sizeof(struct jffs2_unknown_node) - 4) != je32_to_cpu(un->hdr_crc)) {
		/* Hmmm. This should have been caught at scan time. */
		JFFS2_NOTICE("node header CRC failed at %#08x. But it must have been OK earlier.\n", ref_offset(ref));
		jffs2_dbg_dump_node(c, ref_offset(ref));
		return 1;
	} else {
		switch(je16_to_cpu(un->nodetype) & JFFS2_COMPAT_MASK) {

		case JFFS2_FEATURE_INCOMPAT:
			JFFS2_ERROR("unknown INCOMPAT nodetype %#04X at %#08x\n",
				je16_to_cpu(un->nodetype), ref_offset(ref));
			/* EEP */
			BUG();
			break;

		case JFFS2_FEATURE_ROCOMPAT:
			JFFS2_ERROR("unknown ROCOMPAT nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			BUG_ON(!(c->flags & JFFS2_SB_FLAG_RO));
			break;

		case JFFS2_FEATURE_RWCOMPAT_COPY:
			JFFS2_NOTICE("unknown RWCOMPAT_COPY nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			break;

		case JFFS2_FEATURE_RWCOMPAT_DELETE:
			JFFS2_NOTICE("unknown RWCOMPAT_DELETE nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			return 1;
		}
	}

	return 0;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * The function detects whether more data should be read and reads it if yes.
 *
 * Returns: 0 on succes;
 * 	    negative error code on failure.
 */
static int read_more(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *ref,
		     int right_size, int *rdlen, unsigned char *buf, unsigned char *bufstart)
{
	int right_len, err, len;
	size_t retlen;
	uint32_t offs;

	if (jffs2_is_writebuffered(c)) {
		right_len = c->wbuf_pagesize - (bufstart - buf);
		if (right_size + (int)(bufstart - buf) > c->wbuf_pagesize)
			right_len += c->wbuf_pagesize;
	} else
		right_len = right_size;

	if (*rdlen == right_len)
		return 0;

	/* We need to read more data */
	offs = ref_offset(ref) + *rdlen;
	if (jffs2_is_writebuffered(c)) {
		bufstart = buf + c->wbuf_pagesize;
		len = c->wbuf_pagesize;
	} else {
		bufstart = buf + *rdlen;
		len = right_size - *rdlen;
	}

	dbg_readinode("read more %d bytes\n", len);

	err = jffs2_flash_read(c, offs, len, &retlen, bufstart);
	if (err) {
		JFFS2_ERROR("can not read %d bytes from 0x%08x, "
			"error code: %d.\n", len, offs, err);
		return err;
	}

	if (retlen < len) {
		JFFS2_ERROR("short read at %#08x: %zu instead of %d.\n",
				offs, retlen, len);
		return -EIO;
	}

	*rdlen = right_len;

	return 0;
}

/* Get tmp_dnode_info and full_dirent for all non-obsolete nodes associated
   with this ino, returning the former in order of version */
static int jffs2_get_inode_nodes(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
				 struct rb_root *tnp, struct jffs2_full_dirent **fdp,
				 uint32_t *highest_version, uint32_t *latest_mctime,
				 uint32_t *mctime_ver)
{
	struct jffs2_raw_node_ref *ref, *valid_ref;
	struct rb_root ret_tn = RB_ROOT;
	struct jffs2_full_dirent *ret_fd = NULL;
	unsigned char *buf = NULL;
	union jffs2_node_union *node;
	size_t retlen;
	int len, err;

	*mctime_ver = 0;

	dbg_readinode("ino #%u\n", f->inocache->ino);

	if (jffs2_is_writebuffered(c)) {
		/*
		 * If we have the write buffer, we assume the minimal I/O unit
		 * is c->wbuf_pagesize. We implement some optimizations which in
		 * this case and we need a temporary buffer of size =
		 * 2*c->wbuf_pagesize bytes (see comments in read_dnode()).
		 * Basically, we want to read not only the node header, but the
		 * whole wbuf (NAND page in case of NAND) or 2, if the node
		 * header overlaps the border between the 2 wbufs.
		 */
		len = 2*c->wbuf_pagesize;
	} else {
		/*
		 * When there is no write buffer, the size of the temporary
		 * buffer is the size of the larges node header.
		 */
		len = sizeof(union jffs2_node_union);
	}

	/* FIXME: in case of NOR and available ->point() this
	 * needs to be fixed. */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spin_lock(&c->erase_completion_lock);
	valid_ref = jffs2_first_valid_node(f->inocache->nodes);
	if (!valid_ref && f->inocache->ino != 1)
		JFFS2_WARNING("Eep. No valid nodes for ino #%u.\n", f->inocache->ino);
	while (valid_ref) {
		unsigned char *bufstart;

		/* We can hold a pointer to a non-obsolete node without the spinlock,
		   but _obsolete_ nodes may disappear at any time, if the block
		   they're in gets erased. So if we mark 'ref' obsolete while we're
		   not holding the lock, it can go away immediately. For that reason,
		   we find the next valid node first, before processing 'ref'.
		*/
		ref = valid_ref;
		valid_ref = jffs2_first_valid_node(ref->next_in_ino);
		spin_unlock(&c->erase_completion_lock);

		cond_resched();

		/*
		 * At this point we don't know the type of the node we're going
		 * to read, so we do not know the size of its header. In order
		 * to minimize the amount of flash IO we assume the node has
		 * size = JFFS2_MIN_NODE_HEADER.
		 */
		if (jffs2_is_writebuffered(c)) {
			/*
			 * We treat 'buf' as 2 adjacent wbufs. We want to
			 * adjust bufstart such as it points to the
			 * beginning of the node within this wbuf.
			 */
			bufstart = buf + (ref_offset(ref) % c->wbuf_pagesize);
			/* We will read either one wbuf or 2 wbufs. */
			len = c->wbuf_pagesize - (bufstart - buf);
			if (JFFS2_MIN_NODE_HEADER + (int)(bufstart - buf) > c->wbuf_pagesize) {
				/* The header spans the border of the first wbuf */
				len += c->wbuf_pagesize;
			}
		} else {
			bufstart = buf;
			len = JFFS2_MIN_NODE_HEADER;
		}

		dbg_readinode("read %d bytes at %#08x(%d).\n", len, ref_offset(ref), ref_flags(ref));

		/* FIXME: point() */
		err = jffs2_flash_read(c, ref_offset(ref), len,
				       &retlen, bufstart);
		if (err) {
			JFFS2_ERROR("can not read %d bytes from 0x%08x, " "error code: %d.\n", len, ref_offset(ref), err);
			goto free_out;
		}

		if (retlen < len) {
			JFFS2_ERROR("short read at %#08x: %zu instead of %d.\n", ref_offset(ref), retlen, len);
			err = -EIO;
			goto free_out;
		}

		node = (union jffs2_node_union *)bufstart;

		switch (je16_to_cpu(node->u.nodetype)) {

		case JFFS2_NODETYPE_DIRENT:

			if (JFFS2_MIN_NODE_HEADER < sizeof(struct jffs2_raw_dirent)) {
				err = read_more(c, ref, sizeof(struct jffs2_raw_dirent), &len, buf, bufstart);
				if (unlikely(err))
					goto free_out;
			}

			err = read_direntry(c, ref, &node->d, retlen, &ret_fd, latest_mctime, mctime_ver);
			if (err == 1) {
				jffs2_mark_node_obsolete(c, ref);
				break;
			} else if (unlikely(err))
				goto free_out;

			if (je32_to_cpu(node->d.version) > *highest_version)
				*highest_version = je32_to_cpu(node->d.version);

			break;

		case JFFS2_NODETYPE_INODE:

			if (JFFS2_MIN_NODE_HEADER < sizeof(struct jffs2_raw_inode)) {
				err = read_more(c, ref, sizeof(struct jffs2_raw_inode), &len, buf, bufstart);
				if (unlikely(err))
					goto free_out;
			}

			err = read_dnode(c, ref, &node->i, &ret_tn, len, latest_mctime, mctime_ver);
			if (err == 1) {
				jffs2_mark_node_obsolete(c, ref);
				break;
			} else if (unlikely(err))
				goto free_out;

			if (je32_to_cpu(node->i.version) > *highest_version)
				*highest_version = je32_to_cpu(node->i.version);

			break;

		default:
			if (JFFS2_MIN_NODE_HEADER < sizeof(struct jffs2_unknown_node)) {
				err = read_more(c, ref, sizeof(struct jffs2_unknown_node), &len, buf, bufstart);
				if (unlikely(err))
					goto free_out;
			}

			err = read_unknown(c, ref, &node->u);
			if (err == 1) {
				jffs2_mark_node_obsolete(c, ref);
				break;
			} else if (unlikely(err))
				goto free_out;

		}
		spin_lock(&c->erase_completion_lock);
	}

	spin_unlock(&c->erase_completion_lock);
	*tnp = ret_tn;
	*fdp = ret_fd;
	kfree(buf);

	dbg_readinode("nodes of inode #%u were read, the highest version is %u, latest_mctime %u, mctime_ver %u.\n",
			f->inocache->ino, *highest_version, *latest_mctime, *mctime_ver);
	return 0;

 free_out:
	jffs2_free_tmp_dnode_info_list(&ret_tn);
	jffs2_free_full_dirent_list(ret_fd);
	kfree(buf);
	return err;
}

static int jffs2_do_read_inode_internal(struct jffs2_sb_info *c,
					struct jffs2_inode_info *f,
					struct jffs2_raw_inode *latest_node)
{
	struct jffs2_tmp_dnode_info *tn;
	struct rb_root tn_list;
	struct rb_node *rb, *repl_rb;
	struct jffs2_full_dirent *fd_list;
	struct jffs2_full_dnode *fn, *first_fn = NULL;
	uint32_t crc;
	uint32_t latest_mctime, mctime_ver;
	size_t retlen;
	int ret;

	dbg_readinode("ino #%u nlink is %d\n", f->inocache->ino, f->inocache->nlink);

	/* Grab all nodes relevant to this ino */
	ret = jffs2_get_inode_nodes(c, f, &tn_list, &fd_list, &f->highest_version, &latest_mctime, &mctime_ver);

	if (ret) {
		JFFS2_ERROR("cannot read nodes for ino %u, returned error is %d\n", f->inocache->ino, ret);
		if (f->inocache->state == INO_STATE_READING)
			jffs2_set_inocache_state(c, f->inocache, INO_STATE_CHECKEDABSENT);
		return ret;
	}
	f->dents = fd_list;

	rb = rb_first(&tn_list);

	while (rb) {
		cond_resched();
		tn = rb_entry(rb, struct jffs2_tmp_dnode_info, rb);
		fn = tn->fn;
		ret = 1;
		dbg_readinode("consider node ver %u, phys offset "
			"%#08x(%d), range %u-%u.\n", tn->version,
			ref_offset(fn->raw), ref_flags(fn->raw),
			fn->ofs, fn->ofs + fn->size);

		if (fn->size) {
			ret = jffs2_add_older_frag_to_fragtree(c, f, tn);
			/* TODO: the error code isn't checked, check it */
			jffs2_dbg_fragtree_paranoia_check_nolock(f);
			BUG_ON(ret < 0);
			if (!first_fn && ret == 0)
				first_fn = fn;
		} else if (!first_fn) {
			first_fn = fn;
			f->metadata = fn;
			ret = 0; /* Prevent freeing the metadata update node */
		} else
			jffs2_mark_node_obsolete(c, fn->raw);

		BUG_ON(rb->rb_left);
		if (rb->rb_parent && rb->rb_parent->rb_left == rb) {
			/* We were then left-hand child of our parent. We need
			 * to move our own right-hand child into our place. */
			repl_rb = rb->rb_right;
			if (repl_rb)
				repl_rb->rb_parent = rb->rb_parent;
		} else
			repl_rb = NULL;

		rb = rb_next(rb);

		/* Remove the spent tn from the tree; don't bother rebalancing
		 * but put our right-hand child in our own place. */
		if (tn->rb.rb_parent) {
			if (tn->rb.rb_parent->rb_left == &tn->rb)
				tn->rb.rb_parent->rb_left = repl_rb;
			else if (tn->rb.rb_parent->rb_right == &tn->rb)
				tn->rb.rb_parent->rb_right = repl_rb;
			else BUG();
		} else if (tn->rb.rb_right)
			tn->rb.rb_right->rb_parent = NULL;

		jffs2_free_tmp_dnode_info(tn);
		if (ret) {
			dbg_readinode("delete dnode %u-%u.\n",
				fn->ofs, fn->ofs + fn->size);
			jffs2_free_full_dnode(fn);
		}
	}
	jffs2_dbg_fragtree_paranoia_check_nolock(f);

	BUG_ON(first_fn && ref_obsolete(first_fn->raw));

	fn = first_fn;
	if (unlikely(!first_fn)) {
		/* No data nodes for this inode. */
		if (f->inocache->ino != 1) {
			JFFS2_WARNING("no data nodes found for ino #%u\n", f->inocache->ino);
			if (!fd_list) {
				if (f->inocache->state == INO_STATE_READING)
					jffs2_set_inocache_state(c, f->inocache, INO_STATE_CHECKEDABSENT);
				return -EIO;
			}
			JFFS2_NOTICE("but it has children so we fake some modes for it\n");
		}
		latest_node->mode = cpu_to_jemode(S_IFDIR|S_IRUGO|S_IWUSR|S_IXUGO);
		latest_node->version = cpu_to_je32(0);
		latest_node->atime = latest_node->ctime = latest_node->mtime = cpu_to_je32(0);
		latest_node->isize = cpu_to_je32(0);
		latest_node->gid = cpu_to_je16(0);
		latest_node->uid = cpu_to_je16(0);
		if (f->inocache->state == INO_STATE_READING)
			jffs2_set_inocache_state(c, f->inocache, INO_STATE_PRESENT);
		return 0;
	}

	ret = jffs2_flash_read(c, ref_offset(fn->raw), sizeof(*latest_node), &retlen, (void *)latest_node);
	if (ret || retlen != sizeof(*latest_node)) {
		JFFS2_ERROR("failed to read from flash: error %d, %zd of %zd bytes read\n",
			ret, retlen, sizeof(*latest_node));
		/* FIXME: If this fails, there seems to be a memory leak. Find it. */
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
		return ret?ret:-EIO;
	}

	crc = crc32(0, latest_node, sizeof(*latest_node)-8);
	if (crc != je32_to_cpu(latest_node->node_crc)) {
		JFFS2_ERROR("CRC failed for read_inode of inode %u at physical location 0x%x\n",
			f->inocache->ino, ref_offset(fn->raw));
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
		return -EIO;
	}

	switch(jemode_to_cpu(latest_node->mode) & S_IFMT) {
	case S_IFDIR:
		if (mctime_ver > je32_to_cpu(latest_node->version)) {
			/* The times in the latest_node are actually older than
			   mctime in the latest dirent. Cheat. */
			latest_node->ctime = latest_node->mtime = cpu_to_je32(latest_mctime);
		}
		break;


	case S_IFREG:
		/* If it was a regular file, truncate it to the latest node's isize */
		jffs2_truncate_fragtree(c, &f->fragtree, je32_to_cpu(latest_node->isize));
		break;

	case S_IFLNK:
		/* Hack to work around broken isize in old symlink code.
		   Remove this when dwmw2 comes to his senses and stops
		   symlinks from being an entirely gratuitous special
		   case. */
		if (!je32_to_cpu(latest_node->isize))
			latest_node->isize = latest_node->dsize;

		if (f->inocache->state != INO_STATE_CHECKING) {
			/* Symlink's inode data is the target path. Read it and
			 * keep in RAM to facilitate quick follow symlink
			 * operation. */
			f->target = kmalloc(je32_to_cpu(latest_node->csize) + 1, GFP_KERNEL);
			if (!f->target) {
				JFFS2_ERROR("can't allocate %d bytes of memory for the symlink target path cache\n", je32_to_cpu(latest_node->csize));
				up(&f->sem);
				jffs2_do_clear_inode(c, f);
				return -ENOMEM;
			}

			ret = jffs2_flash_read(c, ref_offset(fn->raw) + sizeof(*latest_node),
						je32_to_cpu(latest_node->csize), &retlen, (char *)f->target);

			if (ret  || retlen != je32_to_cpu(latest_node->csize)) {
				if (retlen != je32_to_cpu(latest_node->csize))
					ret = -EIO;
				kfree(f->target);
				f->target = NULL;
				up(&f->sem);
				jffs2_do_clear_inode(c, f);
				return -ret;
			}

			f->target[je32_to_cpu(latest_node->csize)] = '\0';
			dbg_readinode("symlink's target '%s' cached\n", f->target);
		}

		/* fall through... */

	case S_IFBLK:
	case S_IFCHR:
		/* Certain inode types should have only one data node, and it's
		   kept as the metadata node */
		if (f->metadata) {
			JFFS2_ERROR("Argh. Special inode #%u with mode 0%o had metadata node\n",
			       f->inocache->ino, jemode_to_cpu(latest_node->mode));
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		if (!frag_first(&f->fragtree)) {
			JFFS2_ERROR("Argh. Special inode #%u with mode 0%o has no fragments\n",
			       f->inocache->ino, jemode_to_cpu(latest_node->mode));
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* ASSERT: f->fraglist != NULL */
		if (frag_next(frag_first(&f->fragtree))) {
			JFFS2_ERROR("Argh. Special inode #%u with mode 0x%x had more than one node\n",
			       f->inocache->ino, jemode_to_cpu(latest_node->mode));
			/* FIXME: Deal with it - check crc32, check for duplicate node, check times and discard the older one */
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* OK. We're happy */
		f->metadata = frag_first(&f->fragtree)->node;
		jffs2_free_node_frag(frag_first(&f->fragtree));
		f->fragtree = RB_ROOT;
		break;
	}
	if (f->inocache->state == INO_STATE_READING)
		jffs2_set_inocache_state(c, f->inocache, INO_STATE_PRESENT);

	return 0;
}

/* Scan the list of all nodes present for this ino, build map of versions, etc. */
int jffs2_do_read_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			uint32_t ino, struct jffs2_raw_inode *latest_node)
{
	dbg_readinode("read inode #%u\n", ino);

 retry_inocache:
	spin_lock(&c->inocache_lock);
	f->inocache = jffs2_get_ino_cache(c, ino);

	if (f->inocache) {
		/* Check its state. We may need to wait before we can use it */
		switch(f->inocache->state) {
		case INO_STATE_UNCHECKED:
		case INO_STATE_CHECKEDABSENT:
			f->inocache->state = INO_STATE_READING;
			break;

		case INO_STATE_CHECKING:
		case INO_STATE_GC:
			/* If it's in either of these states, we need
			   to wait for whoever's got it to finish and
			   put it back. */
			dbg_readinode("waiting for ino #%u in state %d\n", ino, f->inocache->state);
			sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			goto retry_inocache;

		case INO_STATE_READING:
		case INO_STATE_PRESENT:
			/* Eep. This should never happen. It can
			happen if Linux calls read_inode() again
			before clear_inode() has finished though. */
			JFFS2_ERROR("Eep. Trying to read_inode #%u when it's already in state %d!\n", ino, f->inocache->state);
			/* Fail. That's probably better than allowing it to succeed */
			f->inocache = NULL;
			break;

		default:
			BUG();
		}
	}
	spin_unlock(&c->inocache_lock);

	if (!f->inocache && ino == 1) {
		/* Special case - no root inode on medium */
		f->inocache = jffs2_alloc_inode_cache();
		if (!f->inocache) {
			JFFS2_ERROR("cannot allocate inocache for root inode\n");
			return -ENOMEM;
		}
		dbg_readinode("creating inocache for root inode\n");
		memset(f->inocache, 0, sizeof(struct jffs2_inode_cache));
		f->inocache->ino = f->inocache->nlink = 1;
		f->inocache->nodes = (struct jffs2_raw_node_ref *)f->inocache;
		f->inocache->state = INO_STATE_READING;
		init_xattr_inode_cache(f->inocache);
		jffs2_add_ino_cache(c, f->inocache);
	}
	if (!f->inocache) {
		JFFS2_ERROR("requestied to read an nonexistent ino %u\n", ino);
		return -ENOENT;
	}

	return jffs2_do_read_inode_internal(c, f, latest_node);
}

int jffs2_do_crccheck_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	struct jffs2_raw_inode n;
	struct jffs2_inode_info *f = kmalloc(sizeof(*f), GFP_KERNEL);
	int ret;

	if (!f)
		return -ENOMEM;

	memset(f, 0, sizeof(*f));
	init_MUTEX_LOCKED(&f->sem);
	f->inocache = ic;

	ret = jffs2_do_read_inode_internal(c, f, &n);
	if (!ret) {
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
	}
	kfree (f);
	return ret;
}

void jffs2_do_clear_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	struct jffs2_full_dirent *fd, *fds;
	int deleted;

	down(&f->sem);
	deleted = f->inocache && !f->inocache->nlink;

	if (f->inocache && f->inocache->state != INO_STATE_CHECKING)
		jffs2_set_inocache_state(c, f->inocache, INO_STATE_CLEARING);

	if (f->metadata) {
		if (deleted)
			jffs2_mark_node_obsolete(c, f->metadata->raw);
		jffs2_free_full_dnode(f->metadata);
	}

	jffs2_kill_fragtree(&f->fragtree, deleted?c:NULL);

	if (f->target) {
		kfree(f->target);
		f->target = NULL;
	}

	fds = f->dents;
	while(fds) {
		fd = fds;
		fds = fd->next;
		jffs2_free_full_dirent(fd);
	}

	if (f->inocache && f->inocache->state != INO_STATE_CHECKING) {
		jffs2_set_inocache_state(c, f->inocache, INO_STATE_CHECKEDABSENT);
		if (f->inocache->nodes == (void *)f->inocache)
			jffs2_del_ino_cache(c, f->inocache);
	}

	up(&f->sem);
}
