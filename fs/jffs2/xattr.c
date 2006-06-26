/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2006  NEC Corporation
 *
 * Created by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/crc32.h>
#include <linux/jffs2.h>
#include <linux/xattr.h>
#include <linux/mtd/mtd.h>
#include "nodelist.h"
/* -------- xdatum related functions ----------------
 * xattr_datum_hashkey(xprefix, xname, xvalue, xsize)
 *   is used to calcurate xdatum hashkey. The reminder of hashkey into XATTRINDEX_HASHSIZE is
 *   the index of the xattr name/value pair cache (c->xattrindex).
 * unload_xattr_datum(c, xd)
 *   is used to release xattr name/value pair and detach from c->xattrindex.
 * reclaim_xattr_datum(c)
 *   is used to reclaim xattr name/value pairs on the xattr name/value pair cache when
 *   memory usage by cache is over c->xdatum_mem_threshold. Currentry, this threshold 
 *   is hard coded as 32KiB.
 * delete_xattr_datum_node(c, xd)
 *   is used to delete a jffs2 node is dominated by xdatum. When EBS(Erase Block Summary) is
 *   enabled, it overwrites the obsolete node by myself.
 * delete_xattr_datum(c, xd)
 *   is used to delete jffs2_xattr_datum object. It must be called with 0-value of reference
 *   counter. (It means how many jffs2_xattr_ref object refers this xdatum.)
 * do_verify_xattr_datum(c, xd)
 *   is used to load the xdatum informations without name/value pair from the medium.
 *   It's necessary once, because those informations are not collected during mounting
 *   process when EBS is enabled.
 *   0 will be returned, if success. An negative return value means recoverable error, and
 *   positive return value means unrecoverable error. Thus, caller must remove this xdatum
 *   and xref when it returned positive value.
 * do_load_xattr_datum(c, xd)
 *   is used to load name/value pair from the medium.
 *   The meanings of return value is same as do_verify_xattr_datum().
 * load_xattr_datum(c, xd)
 *   is used to be as a wrapper of do_verify_xattr_datum() and do_load_xattr_datum().
 *   If xd need to call do_verify_xattr_datum() at first, it's called before calling
 *   do_load_xattr_datum(). The meanings of return value is same as do_verify_xattr_datum().
 * save_xattr_datum(c, xd)
 *   is used to write xdatum to medium. xd->version will be incremented.
 * create_xattr_datum(c, xprefix, xname, xvalue, xsize)
 *   is used to create new xdatum and write to medium.
 * -------------------------------------------------- */

static uint32_t xattr_datum_hashkey(int xprefix, const char *xname, const char *xvalue, int xsize)
{
	int name_len = strlen(xname);

	return crc32(xprefix, xname, name_len) ^ crc32(xprefix, xvalue, xsize);
}

static void unload_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	D1(dbg_xattr("%s: xid=%u, version=%u\n", __FUNCTION__, xd->xid, xd->version));
	if (xd->xname) {
		c->xdatum_mem_usage -= (xd->name_len + 1 + xd->value_len);
		kfree(xd->xname);
	}

	list_del_init(&xd->xindex);
	xd->hashkey = 0;
	xd->xname = NULL;
	xd->xvalue = NULL;
}

static void reclaim_xattr_datum(struct jffs2_sb_info *c)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_xattr_datum *xd, *_xd;
	uint32_t target, before;
	static int index = 0;
	int count;

	if (c->xdatum_mem_threshold > c->xdatum_mem_usage)
		return;

	before = c->xdatum_mem_usage;
	target = c->xdatum_mem_usage * 4 / 5; /* 20% reduction */
	for (count = 0; count < XATTRINDEX_HASHSIZE; count++) {
		list_for_each_entry_safe(xd, _xd, &c->xattrindex[index], xindex) {
			if (xd->flags & JFFS2_XFLAGS_HOT) {
				xd->flags &= ~JFFS2_XFLAGS_HOT;
			} else if (!(xd->flags & JFFS2_XFLAGS_BIND)) {
				unload_xattr_datum(c, xd);
			}
			if (c->xdatum_mem_usage <= target)
				goto out;
		}
		index = (index+1) % XATTRINDEX_HASHSIZE;
	}
 out:
	JFFS2_NOTICE("xdatum_mem_usage from %u byte to %u byte (%u byte reclaimed)\n",
		     before, c->xdatum_mem_usage, before - c->xdatum_mem_usage);
}

static void delete_xattr_datum_node(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_raw_xattr rx;
	size_t length;
	int rc;

	if (!xd->node) {
		JFFS2_WARNING("xdatum (xid=%u) is removed twice.\n", xd->xid);
		return;
	}
	if (jffs2_sum_active()) {
		memset(&rx, 0xff, sizeof(struct jffs2_raw_xattr));
		rc = jffs2_flash_read(c, ref_offset(xd->node),
				      sizeof(struct jffs2_unknown_node),
				      &length, (char *)&rx);
		if (rc || length != sizeof(struct jffs2_unknown_node)) {
			JFFS2_ERROR("jffs2_flash_read()=%d, req=%zu, read=%zu at %#08x\n",
				    rc, sizeof(struct jffs2_unknown_node),
				    length, ref_offset(xd->node));
		}
		rc = jffs2_flash_write(c, ref_offset(xd->node), sizeof(rx),
				       &length, (char *)&rx);
		if (rc || length != sizeof(struct jffs2_raw_xattr)) {
			JFFS2_ERROR("jffs2_flash_write()=%d, req=%zu, wrote=%zu ar %#08x\n",
				    rc, sizeof(rx), length, ref_offset(xd->node));
		}
	}
	spin_lock(&c->erase_completion_lock);
	xd->node->next_in_ino = NULL;
	spin_unlock(&c->erase_completion_lock);
	jffs2_mark_node_obsolete(c, xd->node);
	xd->node = NULL;
}

static void delete_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	BUG_ON(xd->refcnt);

	unload_xattr_datum(c, xd);
	if (xd->node) {
		delete_xattr_datum_node(c, xd);
		xd->node = NULL;
	}
	jffs2_free_xattr_datum(xd);
}

static int do_verify_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_eraseblock *jeb;
	struct jffs2_raw_xattr rx;
	size_t readlen;
	uint32_t crc, totlen;
	int rc;

	BUG_ON(!xd->node);
	BUG_ON(ref_flags(xd->node) != REF_UNCHECKED);

	rc = jffs2_flash_read(c, ref_offset(xd->node), sizeof(rx), &readlen, (char *)&rx);
	if (rc || readlen != sizeof(rx)) {
		JFFS2_WARNING("jffs2_flash_read()=%d, req=%zu, read=%zu at %#08x\n",
			      rc, sizeof(rx), readlen, ref_offset(xd->node));
		return rc ? rc : -EIO;
	}
	crc = crc32(0, &rx, sizeof(rx) - 4);
	if (crc != je32_to_cpu(rx.node_crc)) {
		if (je32_to_cpu(rx.node_crc) != 0xffffffff)
			JFFS2_ERROR("node CRC failed at %#08x, read=%#08x, calc=%#08x\n",
				    ref_offset(xd->node), je32_to_cpu(rx.hdr_crc), crc);
		return EIO;
	}
	totlen = PAD(sizeof(rx) + rx.name_len + 1 + je16_to_cpu(rx.value_len));
	if (je16_to_cpu(rx.magic) != JFFS2_MAGIC_BITMASK
	    || je16_to_cpu(rx.nodetype) != JFFS2_NODETYPE_XATTR
	    || je32_to_cpu(rx.totlen) != totlen
	    || je32_to_cpu(rx.xid) != xd->xid
	    || je32_to_cpu(rx.version) != xd->version) {
		JFFS2_ERROR("inconsistent xdatum at %#08x, magic=%#04x/%#04x, "
			    "nodetype=%#04x/%#04x, totlen=%u/%u, xid=%u/%u, version=%u/%u\n",
			    ref_offset(xd->node), je16_to_cpu(rx.magic), JFFS2_MAGIC_BITMASK,
			    je16_to_cpu(rx.nodetype), JFFS2_NODETYPE_XATTR,
			    je32_to_cpu(rx.totlen), totlen,
			    je32_to_cpu(rx.xid), xd->xid,
			    je32_to_cpu(rx.version), xd->version);
		return EIO;
	}
	xd->xprefix = rx.xprefix;
	xd->name_len = rx.name_len;
	xd->value_len = je16_to_cpu(rx.value_len);
	xd->data_crc = je32_to_cpu(rx.data_crc);

	/* This JFFS2_NODETYPE_XATTR node is checked */
	jeb = &c->blocks[ref_offset(xd->node) / c->sector_size];
	totlen = PAD(je32_to_cpu(rx.totlen));

	spin_lock(&c->erase_completion_lock);
	c->unchecked_size -= totlen; c->used_size += totlen;
	jeb->unchecked_size -= totlen; jeb->used_size += totlen;
	xd->node->flash_offset = ref_offset(xd->node) | REF_PRISTINE;
	spin_unlock(&c->erase_completion_lock);

	/* unchecked xdatum is chained with c->xattr_unchecked */
	list_del_init(&xd->xindex);

	dbg_xattr("success on verfying xdatum (xid=%u, version=%u)\n",
		  xd->xid, xd->version);

	return 0;
}

static int do_load_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	char *data;
	size_t readlen;
	uint32_t crc, length;
	int i, ret, retry = 0;

	BUG_ON(!xd->node);
	BUG_ON(ref_flags(xd->node) != REF_PRISTINE);
	BUG_ON(!list_empty(&xd->xindex));
 retry:
	length = xd->name_len + 1 + xd->value_len;
	data = kmalloc(length, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = jffs2_flash_read(c, ref_offset(xd->node)+sizeof(struct jffs2_raw_xattr),
			       length, &readlen, data);

	if (ret || length!=readlen) {
		JFFS2_WARNING("jffs2_flash_read() returned %d, request=%d, readlen=%zu, at %#08x\n",
			      ret, length, readlen, ref_offset(xd->node));
		kfree(data);
		return ret ? ret : -EIO;
	}

	data[xd->name_len] = '\0';
	crc = crc32(0, data, length);
	if (crc != xd->data_crc) {
		JFFS2_WARNING("node CRC failed (JFFS2_NODETYPE_XREF)"
			      " at %#08x, read: 0x%08x calculated: 0x%08x\n",
			      ref_offset(xd->node), xd->data_crc, crc);
		kfree(data);
		return EIO;
	}

	xd->flags |= JFFS2_XFLAGS_HOT;
	xd->xname = data;
	xd->xvalue = data + xd->name_len+1;

	c->xdatum_mem_usage += length;

	xd->hashkey = xattr_datum_hashkey(xd->xprefix, xd->xname, xd->xvalue, xd->value_len);
	i = xd->hashkey % XATTRINDEX_HASHSIZE;
	list_add(&xd->xindex, &c->xattrindex[i]);
	if (!retry) {
		retry = 1;
		reclaim_xattr_datum(c);
		if (!xd->xname)
			goto retry;
	}

	dbg_xattr("success on loading xdatum (xid=%u, xprefix=%u, xname='%s')\n",
		  xd->xid, xd->xprefix, xd->xname);

	return 0;
}

static int load_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem);
	 * rc < 0 : recoverable error, try again
	 * rc = 0 : success
	 * rc > 0 : Unrecoverable error, this node should be deleted.
	 */
	int rc = 0;
	BUG_ON(xd->xname);
	if (!xd->node)
		return EIO;
	if (unlikely(ref_flags(xd->node) != REF_PRISTINE)) {
		rc = do_verify_xattr_datum(c, xd);
		if (rc > 0) {
			list_del_init(&xd->xindex);
			delete_xattr_datum_node(c, xd);
		}
	}
	if (!rc)
		rc = do_load_xattr_datum(c, xd);
	return rc;
}

static int save_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_raw_node_ref *raw;
	struct jffs2_raw_xattr rx;
	struct kvec vecs[2];
	size_t length;
	int rc, totlen;
	uint32_t phys_ofs = write_ofs(c);

	BUG_ON(!xd->xname);

	vecs[0].iov_base = &rx;
	vecs[0].iov_len = PAD(sizeof(rx));
	vecs[1].iov_base = xd->xname;
	vecs[1].iov_len = xd->name_len + 1 + xd->value_len;
	totlen = vecs[0].iov_len + vecs[1].iov_len;

	/* Setup raw-xattr */
	rx.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rx.nodetype = cpu_to_je16(JFFS2_NODETYPE_XATTR);
	rx.totlen = cpu_to_je32(PAD(totlen));
	rx.hdr_crc = cpu_to_je32(crc32(0, &rx, sizeof(struct jffs2_unknown_node) - 4));

	rx.xid = cpu_to_je32(xd->xid);
	rx.version = cpu_to_je32(++xd->version);
	rx.xprefix = xd->xprefix;
	rx.name_len = xd->name_len;
	rx.value_len = cpu_to_je16(xd->value_len);
	rx.data_crc = cpu_to_je32(crc32(0, vecs[1].iov_base, vecs[1].iov_len));
	rx.node_crc = cpu_to_je32(crc32(0, &rx, sizeof(struct jffs2_raw_xattr) - 4));

	rc = jffs2_flash_writev(c, vecs, 2, phys_ofs, &length, 0);
	if (rc || totlen != length) {
		JFFS2_WARNING("jffs2_flash_writev()=%d, req=%u, wrote=%zu, at %#08x\n",
			      rc, totlen, length, phys_ofs);
		rc = rc ? rc : -EIO;
		if (length)
			jffs2_add_physical_node_ref(c, phys_ofs | REF_OBSOLETE, PAD(totlen), NULL);

		return rc;
	}

	/* success */
	raw = jffs2_add_physical_node_ref(c, phys_ofs | REF_PRISTINE, PAD(totlen), NULL);
	/* FIXME */ raw->next_in_ino = (void *)xd;

	if (xd->node)
		delete_xattr_datum_node(c, xd);
	xd->node = raw;

	dbg_xattr("success on saving xdatum (xid=%u, version=%u, xprefix=%u, xname='%s')\n",
		  xd->xid, xd->version, xd->xprefix, xd->xname);

	return 0;
}

static struct jffs2_xattr_datum *create_xattr_datum(struct jffs2_sb_info *c,
						    int xprefix, const char *xname,
						    const char *xvalue, int xsize)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_xattr_datum *xd;
	uint32_t hashkey, name_len;
	char *data;
	int i, rc;

	/* Search xattr_datum has same xname/xvalue by index */
	hashkey = xattr_datum_hashkey(xprefix, xname, xvalue, xsize);
	i = hashkey % XATTRINDEX_HASHSIZE;
	list_for_each_entry(xd, &c->xattrindex[i], xindex) {
		if (xd->hashkey==hashkey
		    && xd->xprefix==xprefix
		    && xd->value_len==xsize
		    && !strcmp(xd->xname, xname)
		    && !memcmp(xd->xvalue, xvalue, xsize)) {
			xd->refcnt++;
			return xd;
		}
	}

	/* Not found, Create NEW XATTR-Cache */
	name_len = strlen(xname);

	xd = jffs2_alloc_xattr_datum();
	if (!xd)
		return ERR_PTR(-ENOMEM);

	data = kmalloc(name_len + 1 + xsize, GFP_KERNEL);
	if (!data) {
		jffs2_free_xattr_datum(xd);
		return ERR_PTR(-ENOMEM);
	}
	strcpy(data, xname);
	memcpy(data + name_len + 1, xvalue, xsize);

	xd->refcnt = 1;
	xd->xid = ++c->highest_xid;
	xd->flags |= JFFS2_XFLAGS_HOT;
	xd->xprefix = xprefix;

	xd->hashkey = hashkey;
	xd->xname = data;
	xd->xvalue = data + name_len + 1;
	xd->name_len = name_len;
	xd->value_len = xsize;
	xd->data_crc = crc32(0, data, xd->name_len + 1 + xd->value_len);

	rc = save_xattr_datum(c, xd);
	if (rc) {
		kfree(xd->xname);
		jffs2_free_xattr_datum(xd);
		return ERR_PTR(rc);
	}

	/* Insert Hash Index */
	i = hashkey % XATTRINDEX_HASHSIZE;
	list_add(&xd->xindex, &c->xattrindex[i]);

	c->xdatum_mem_usage += (xd->name_len + 1 + xd->value_len);
	reclaim_xattr_datum(c);

	return xd;
}

/* -------- xref related functions ------------------
 * verify_xattr_ref(c, ref)
 *   is used to load xref information from medium. Because summary data does not
 *   contain xid/ino, it's necessary to verify once while mounting process.
 * delete_xattr_ref_node(c, ref)
 *   is used to delete a jffs2 node is dominated by xref. When EBS is enabled,
 *   it overwrites the obsolete node by myself. 
 * delete_xattr_ref(c, ref)
 *   is used to delete jffs2_xattr_ref object. If the reference counter of xdatum
 *   is refered by this xref become 0, delete_xattr_datum() is called later.
 * save_xattr_ref(c, ref)
 *   is used to write xref to medium.
 * create_xattr_ref(c, ic, xd)
 *   is used to create a new xref and write to medium.
 * jffs2_xattr_delete_inode(c, ic)
 *   is called to remove xrefs related to obsolete inode when inode is unlinked.
 * jffs2_xattr_free_inode(c, ic)
 *   is called to release xattr related objects when unmounting. 
 * check_xattr_ref_inode(c, ic)
 *   is used to confirm inode does not have duplicate xattr name/value pair.
 * -------------------------------------------------- */
static int verify_xattr_ref(struct jffs2_sb_info *c, struct jffs2_xattr_ref *ref)
{
	struct jffs2_eraseblock *jeb;
	struct jffs2_raw_xref rr;
	size_t readlen;
	uint32_t crc, totlen;
	int rc;

	BUG_ON(ref_flags(ref->node) != REF_UNCHECKED);

	rc = jffs2_flash_read(c, ref_offset(ref->node), sizeof(rr), &readlen, (char *)&rr);
	if (rc || sizeof(rr) != readlen) {
		JFFS2_WARNING("jffs2_flash_read()=%d, req=%zu, read=%zu, at %#08x\n",
			      rc, sizeof(rr), readlen, ref_offset(ref->node));
		return rc ? rc : -EIO;
	}
	/* obsolete node */
	crc = crc32(0, &rr, sizeof(rr) - 4);
	if (crc != je32_to_cpu(rr.node_crc)) {
		if (je32_to_cpu(rr.node_crc) != 0xffffffff)
			JFFS2_ERROR("node CRC failed at %#08x, read=%#08x, calc=%#08x\n",
				    ref_offset(ref->node), je32_to_cpu(rr.node_crc), crc);
		return EIO;
	}
	if (je16_to_cpu(rr.magic) != JFFS2_MAGIC_BITMASK
	    || je16_to_cpu(rr.nodetype) != JFFS2_NODETYPE_XREF
	    || je32_to_cpu(rr.totlen) != PAD(sizeof(rr))) {
		JFFS2_ERROR("inconsistent xref at %#08x, magic=%#04x/%#04x, "
			    "nodetype=%#04x/%#04x, totlen=%u/%zu\n",
			    ref_offset(ref->node), je16_to_cpu(rr.magic), JFFS2_MAGIC_BITMASK,
			    je16_to_cpu(rr.nodetype), JFFS2_NODETYPE_XREF,
			    je32_to_cpu(rr.totlen), PAD(sizeof(rr)));
		return EIO;
	}
	ref->ino = je32_to_cpu(rr.ino);
	ref->xid = je32_to_cpu(rr.xid);

	/* fixup superblock/eraseblock info */
	jeb = &c->blocks[ref_offset(ref->node) / c->sector_size];
	totlen = PAD(sizeof(rr));

	spin_lock(&c->erase_completion_lock);
	c->unchecked_size -= totlen; c->used_size += totlen;
	jeb->unchecked_size -= totlen; jeb->used_size += totlen;
	ref->node->flash_offset = ref_offset(ref->node) | REF_PRISTINE;
	spin_unlock(&c->erase_completion_lock);

	dbg_xattr("success on verifying xref (ino=%u, xid=%u) at %#08x\n",
		  ref->ino, ref->xid, ref_offset(ref->node));
	return 0;
}

static void delete_xattr_ref_node(struct jffs2_sb_info *c, struct jffs2_xattr_ref *ref)
{
	struct jffs2_raw_xref rr;
	size_t length;
	int rc;

	if (jffs2_sum_active()) {
		memset(&rr, 0xff, sizeof(rr));
		rc = jffs2_flash_read(c, ref_offset(ref->node),
				      sizeof(struct jffs2_unknown_node),
				      &length, (char *)&rr);
		if (rc || length != sizeof(struct jffs2_unknown_node)) {
			JFFS2_ERROR("jffs2_flash_read()=%d, req=%zu, read=%zu at %#08x\n",
				    rc, sizeof(struct jffs2_unknown_node),
				    length, ref_offset(ref->node));
		}
		rc = jffs2_flash_write(c, ref_offset(ref->node), sizeof(rr),
				       &length, (char *)&rr);
		if (rc || length != sizeof(struct jffs2_raw_xref)) {
			JFFS2_ERROR("jffs2_flash_write()=%d, req=%zu, wrote=%zu at %#08x\n",
				    rc, sizeof(rr), length, ref_offset(ref->node));
		}
	}
	spin_lock(&c->erase_completion_lock);
	ref->node->next_in_ino = NULL;
	spin_unlock(&c->erase_completion_lock);
	jffs2_mark_node_obsolete(c, ref->node);
	ref->node = NULL;
}

static void delete_xattr_ref(struct jffs2_sb_info *c, struct jffs2_xattr_ref *ref)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_xattr_datum *xd;

	BUG_ON(!ref->node);
	delete_xattr_ref_node(c, ref);

	xd = ref->xd;
	xd->refcnt--;
	if (!xd->refcnt)
		delete_xattr_datum(c, xd);
	jffs2_free_xattr_ref(ref);
}

static int save_xattr_ref(struct jffs2_sb_info *c, struct jffs2_xattr_ref *ref)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_raw_node_ref *raw;
	struct jffs2_raw_xref rr;
	size_t length;
	uint32_t phys_ofs = write_ofs(c);
	int ret;

	rr.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rr.nodetype = cpu_to_je16(JFFS2_NODETYPE_XREF);
	rr.totlen = cpu_to_je32(PAD(sizeof(rr)));
	rr.hdr_crc = cpu_to_je32(crc32(0, &rr, sizeof(struct jffs2_unknown_node) - 4));

	rr.ino = cpu_to_je32(ref->ic->ino);
	rr.xid = cpu_to_je32(ref->xd->xid);
	rr.node_crc = cpu_to_je32(crc32(0, &rr, sizeof(rr) - 4));

	ret = jffs2_flash_write(c, phys_ofs, sizeof(rr), &length, (char *)&rr);
	if (ret || sizeof(rr) != length) {
		JFFS2_WARNING("jffs2_flash_write() returned %d, request=%zu, retlen=%zu, at %#08x\n",
			      ret, sizeof(rr), length, phys_ofs);
		ret = ret ? ret : -EIO;
		if (length)
			jffs2_add_physical_node_ref(c, phys_ofs | REF_OBSOLETE, PAD(sizeof(rr)), NULL);

		return ret;
	}

	raw = jffs2_add_physical_node_ref(c, phys_ofs | REF_PRISTINE, PAD(sizeof(rr)), NULL);
	/* FIXME */ raw->next_in_ino = (void *)ref;
	if (ref->node)
		delete_xattr_ref_node(c, ref);
	ref->node = raw;

	dbg_xattr("success on saving xref (ino=%u, xid=%u)\n", ref->ic->ino, ref->xd->xid);

	return 0;
}

static struct jffs2_xattr_ref *create_xattr_ref(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic,
						struct jffs2_xattr_datum *xd)
{
	/* must be called under down_write(xattr_sem) */
	struct jffs2_xattr_ref *ref;
	int ret;

	ref = jffs2_alloc_xattr_ref();
	if (!ref)
		return ERR_PTR(-ENOMEM);
	ref->ic = ic;
	ref->xd = xd;

	ret = save_xattr_ref(c, ref);
	if (ret) {
		jffs2_free_xattr_ref(ref);
		return ERR_PTR(ret);
	}

	/* Chain to inode */
	ref->next = ic->xref;
	ic->xref = ref;

	return ref; /* success */
}

void jffs2_xattr_delete_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	/* It's called from jffs2_clear_inode() on inode removing.
	   When an inode with XATTR is removed, those XATTRs must be removed. */
	struct jffs2_xattr_ref *ref, *_ref;

	if (!ic || ic->nlink > 0)
		return;

	down_write(&c->xattr_sem);
	for (ref = ic->xref; ref; ref = _ref) {
		_ref = ref->next;
		delete_xattr_ref(c, ref);
	}
	ic->xref = NULL;
	up_write(&c->xattr_sem);
}

void jffs2_xattr_free_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	/* It's called from jffs2_free_ino_caches() until unmounting FS. */
	struct jffs2_xattr_datum *xd;
	struct jffs2_xattr_ref *ref, *_ref;

	down_write(&c->xattr_sem);
	for (ref = ic->xref; ref; ref = _ref) {
		_ref = ref->next;
		xd = ref->xd;
		xd->refcnt--;
		if (!xd->refcnt) {
			unload_xattr_datum(c, xd);
			jffs2_free_xattr_datum(xd);
		}
		jffs2_free_xattr_ref(ref);
	}
	ic->xref = NULL;
	up_write(&c->xattr_sem);
}

static int check_xattr_ref_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	/* success of check_xattr_ref_inode() means taht inode (ic) dose not have
	 * duplicate name/value pairs. If duplicate name/value pair would be found,
	 * one will be removed.
	 */
	struct jffs2_xattr_ref *ref, *cmp, **pref;
	int rc = 0;

	if (likely(ic->flags & INO_FLAGS_XATTR_CHECKED))
		return 0;
	down_write(&c->xattr_sem);
 retry:
	rc = 0;
	for (ref=ic->xref, pref=&ic->xref; ref; pref=&ref->next, ref=ref->next) {
		if (!ref->xd->xname) {
			rc = load_xattr_datum(c, ref->xd);
			if (unlikely(rc > 0)) {
				*pref = ref->next;
				delete_xattr_ref(c, ref);
				goto retry;
			} else if (unlikely(rc < 0))
				goto out;
		}
		for (cmp=ref->next, pref=&ref->next; cmp; pref=&cmp->next, cmp=cmp->next) {
			if (!cmp->xd->xname) {
				ref->xd->flags |= JFFS2_XFLAGS_BIND;
				rc = load_xattr_datum(c, cmp->xd);
				ref->xd->flags &= ~JFFS2_XFLAGS_BIND;
				if (unlikely(rc > 0)) {
					*pref = cmp->next;
					delete_xattr_ref(c, cmp);
					goto retry;
				} else if (unlikely(rc < 0))
					goto out;
			}
			if (ref->xd->xprefix == cmp->xd->xprefix
			    && !strcmp(ref->xd->xname, cmp->xd->xname)) {
				*pref = cmp->next;
				delete_xattr_ref(c, cmp);
				goto retry;
			}
		}
	}
	ic->flags |= INO_FLAGS_XATTR_CHECKED;
 out:
	up_write(&c->xattr_sem);

	return rc;
}

/* -------- xattr subsystem functions ---------------
 * jffs2_init_xattr_subsystem(c)
 *   is used to initialize semaphore and list_head, and some variables.
 * jffs2_find_xattr_datum(c, xid)
 *   is used to lookup xdatum while scanning process.
 * jffs2_clear_xattr_subsystem(c)
 *   is used to release any xattr related objects.
 * jffs2_build_xattr_subsystem(c)
 *   is used to associate xdatum and xref while super block building process.
 * jffs2_setup_xattr_datum(c, xid, version)
 *   is used to insert xdatum while scanning process.
 * -------------------------------------------------- */
void jffs2_init_xattr_subsystem(struct jffs2_sb_info *c)
{
	int i;

	for (i=0; i < XATTRINDEX_HASHSIZE; i++)
		INIT_LIST_HEAD(&c->xattrindex[i]);
	INIT_LIST_HEAD(&c->xattr_unchecked);
	c->xref_temp = NULL;

	init_rwsem(&c->xattr_sem);
	c->xdatum_mem_usage = 0;
	c->xdatum_mem_threshold = 32 * 1024;	/* Default 32KB */
}

static struct jffs2_xattr_datum *jffs2_find_xattr_datum(struct jffs2_sb_info *c, uint32_t xid)
{
	struct jffs2_xattr_datum *xd;
	int i = xid % XATTRINDEX_HASHSIZE;

	/* It's only used in scanning/building process. */
	BUG_ON(!(c->flags & (JFFS2_SB_FLAG_SCANNING|JFFS2_SB_FLAG_BUILDING)));

	list_for_each_entry(xd, &c->xattrindex[i], xindex) {
		if (xd->xid==xid)
			return xd;
	}
	return NULL;
}

void jffs2_clear_xattr_subsystem(struct jffs2_sb_info *c)
{
	struct jffs2_xattr_datum *xd, *_xd;
	struct jffs2_xattr_ref *ref, *_ref;
	int i;

	for (ref=c->xref_temp; ref; ref = _ref) {
		_ref = ref->next;
		jffs2_free_xattr_ref(ref);
	}
	c->xref_temp = NULL;

	for (i=0; i < XATTRINDEX_HASHSIZE; i++) {
		list_for_each_entry_safe(xd, _xd, &c->xattrindex[i], xindex) {
			list_del(&xd->xindex);
			if (xd->xname)
				kfree(xd->xname);
			jffs2_free_xattr_datum(xd);
		}
	}
}

void jffs2_build_xattr_subsystem(struct jffs2_sb_info *c)
{
	struct jffs2_xattr_ref *ref, *_ref;
	struct jffs2_xattr_datum *xd, *_xd;
	struct jffs2_inode_cache *ic;
	int i, xdatum_count =0, xdatum_unchecked_count = 0, xref_count = 0;

	BUG_ON(!(c->flags & JFFS2_SB_FLAG_BUILDING));

	/* Phase.1 */
	for (ref=c->xref_temp; ref; ref=_ref) {
		_ref = ref->next;
		/* checking REF_UNCHECKED nodes */
		if (ref_flags(ref->node) != REF_PRISTINE) {
			if (verify_xattr_ref(c, ref)) {
				delete_xattr_ref_node(c, ref);
				jffs2_free_xattr_ref(ref);
				continue;
			}
		}
		/* At this point, ref->xid and ref->ino contain XID and inode number.
		   ref->xd and ref->ic are not valid yet. */
		xd = jffs2_find_xattr_datum(c, ref->xid);
		ic = jffs2_get_ino_cache(c, ref->ino);
		if (!xd || !ic) {
			if (ref_flags(ref->node) != REF_UNCHECKED)
				JFFS2_WARNING("xref(ino=%u, xid=%u) is orphan. \n",
					      ref->ino, ref->xid);
			delete_xattr_ref_node(c, ref);
			jffs2_free_xattr_ref(ref);
			continue;
		}
		ref->xd = xd;
		ref->ic = ic;
		xd->refcnt++;
		ref->next = ic->xref;
		ic->xref = ref;
		xref_count++;
	}
	c->xref_temp = NULL;
	/* After this, ref->xid/ino are NEVER used. */

	/* Phase.2 */
	for (i=0; i < XATTRINDEX_HASHSIZE; i++) {
		list_for_each_entry_safe(xd, _xd, &c->xattrindex[i], xindex) {
			list_del_init(&xd->xindex);
			if (!xd->refcnt) {
				if (ref_flags(xd->node) != REF_UNCHECKED)
					JFFS2_WARNING("orphan xdatum(xid=%u, version=%u) at %#08x\n",
						      xd->xid, xd->version, ref_offset(xd->node));
				delete_xattr_datum(c, xd);
				continue;
			}
			if (ref_flags(xd->node) != REF_PRISTINE) {
				dbg_xattr("unchecked xdatum(xid=%u) at %#08x\n",
					  xd->xid, ref_offset(xd->node));
				list_add(&xd->xindex, &c->xattr_unchecked);
				xdatum_unchecked_count++;
			}
			xdatum_count++;
		}
	}
	/* build complete */
	JFFS2_NOTICE("complete building xattr subsystem, %u of xdatum (%u unchecked) and "
		     "%u of xref found.\n", xdatum_count, xdatum_unchecked_count, xref_count);
}

struct jffs2_xattr_datum *jffs2_setup_xattr_datum(struct jffs2_sb_info *c,
						  uint32_t xid, uint32_t version)
{
	struct jffs2_xattr_datum *xd, *_xd;

	_xd = jffs2_find_xattr_datum(c, xid);
	if (_xd) {
		dbg_xattr("duplicate xdatum (xid=%u, version=%u/%u) at %#08x\n",
			  xid, version, _xd->version, ref_offset(_xd->node));
		if (version < _xd->version)
			return ERR_PTR(-EEXIST);
	}
	xd = jffs2_alloc_xattr_datum();
	if (!xd)
		return ERR_PTR(-ENOMEM);
	xd->xid = xid;
	xd->version = version;
	if (xd->xid > c->highest_xid)
		c->highest_xid = xd->xid;
	list_add_tail(&xd->xindex, &c->xattrindex[xid % XATTRINDEX_HASHSIZE]);

	if (_xd) {
		list_del_init(&_xd->xindex);
		delete_xattr_datum_node(c, _xd);
		jffs2_free_xattr_datum(_xd);
	}
	return xd;
}

/* -------- xattr subsystem functions ---------------
 * xprefix_to_handler(xprefix)
 *   is used to translate xprefix into xattr_handler.
 * jffs2_listxattr(dentry, buffer, size)
 *   is an implementation of listxattr handler on jffs2.
 * do_jffs2_getxattr(inode, xprefix, xname, buffer, size)
 *   is an implementation of getxattr handler on jffs2.
 * do_jffs2_setxattr(inode, xprefix, xname, buffer, size, flags)
 *   is an implementation of setxattr handler on jffs2.
 * -------------------------------------------------- */
struct xattr_handler *jffs2_xattr_handlers[] = {
	&jffs2_user_xattr_handler,
#ifdef CONFIG_JFFS2_FS_SECURITY
	&jffs2_security_xattr_handler,
#endif
#ifdef CONFIG_JFFS2_FS_POSIX_ACL
	&jffs2_acl_access_xattr_handler,
	&jffs2_acl_default_xattr_handler,
#endif
	&jffs2_trusted_xattr_handler,
	NULL
};

static struct xattr_handler *xprefix_to_handler(int xprefix) {
	struct xattr_handler *ret;

	switch (xprefix) {
	case JFFS2_XPREFIX_USER:
		ret = &jffs2_user_xattr_handler;
		break;
#ifdef CONFIG_JFFS2_FS_SECURITY
	case JFFS2_XPREFIX_SECURITY:
		ret = &jffs2_security_xattr_handler;
		break;
#endif
#ifdef CONFIG_JFFS2_FS_POSIX_ACL
	case JFFS2_XPREFIX_ACL_ACCESS:
		ret = &jffs2_acl_access_xattr_handler;
		break;
	case JFFS2_XPREFIX_ACL_DEFAULT:
		ret = &jffs2_acl_default_xattr_handler;
		break;
#endif
	case JFFS2_XPREFIX_TRUSTED:
		ret = &jffs2_trusted_xattr_handler;
		break;
	default:
		ret = NULL;
		break;
	}
	return ret;
}

ssize_t jffs2_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_inode_cache *ic = f->inocache;
	struct jffs2_xattr_ref *ref, **pref;
	struct jffs2_xattr_datum *xd;
	struct xattr_handler *xhandle;
	ssize_t len, rc;
	int retry = 0;

	rc = check_xattr_ref_inode(c, ic);
	if (unlikely(rc))
		return rc;

	down_read(&c->xattr_sem);
 retry:
	len = 0;
	for (ref=ic->xref, pref=&ic->xref; ref; pref=&ref->next, ref=ref->next) {
		BUG_ON(ref->ic != ic);
		xd = ref->xd;
		if (!xd->xname) {
			/* xdatum is unchached */
			if (!retry) {
				retry = 1;
				up_read(&c->xattr_sem);
				down_write(&c->xattr_sem);
				goto retry;
			} else {
				rc = load_xattr_datum(c, xd);
				if (unlikely(rc > 0)) {
					*pref = ref->next;
					delete_xattr_ref(c, ref);
					goto retry;
				} else if (unlikely(rc < 0))
					goto out;
			}
		}
		xhandle = xprefix_to_handler(xd->xprefix);
		if (!xhandle)
			continue;
		if (buffer) {
			rc = xhandle->list(inode, buffer+len, size-len, xd->xname, xd->name_len);
		} else {
			rc = xhandle->list(inode, NULL, 0, xd->xname, xd->name_len);
		}
		if (rc < 0)
			goto out;
		len += rc;
	}
	rc = len;
 out:
	if (!retry) {
		up_read(&c->xattr_sem);
	} else {
		up_write(&c->xattr_sem);
	}
	return rc;
}

int do_jffs2_getxattr(struct inode *inode, int xprefix, const char *xname,
		      char *buffer, size_t size)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_inode_cache *ic = f->inocache;
	struct jffs2_xattr_datum *xd;
	struct jffs2_xattr_ref *ref, **pref;
	int rc, retry = 0;

	rc = check_xattr_ref_inode(c, ic);
	if (unlikely(rc))
		return rc;

	down_read(&c->xattr_sem);
 retry:
	for (ref=ic->xref, pref=&ic->xref; ref; pref=&ref->next, ref=ref->next) {
		BUG_ON(ref->ic!=ic);

		xd = ref->xd;
		if (xd->xprefix != xprefix)
			continue;
		if (!xd->xname) {
			/* xdatum is unchached */
			if (!retry) {
				retry = 1;
				up_read(&c->xattr_sem);
				down_write(&c->xattr_sem);
				goto retry;
			} else {
				rc = load_xattr_datum(c, xd);
				if (unlikely(rc > 0)) {
					*pref = ref->next;
					delete_xattr_ref(c, ref);
					goto retry;
				} else if (unlikely(rc < 0)) {
					goto out;
				}
			}
		}
		if (!strcmp(xname, xd->xname)) {
			rc = xd->value_len;
			if (buffer) {
				if (size < rc) {
					rc = -ERANGE;
				} else {
					memcpy(buffer, xd->xvalue, rc);
				}
			}
			goto out;
		}
	}
	rc = -ENODATA;
 out:
	if (!retry) {
		up_read(&c->xattr_sem);
	} else {
		up_write(&c->xattr_sem);
	}
	return rc;
}

int do_jffs2_setxattr(struct inode *inode, int xprefix, const char *xname,
		      const char *buffer, size_t size, int flags)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_inode_cache *ic = f->inocache;
	struct jffs2_xattr_datum *xd;
	struct jffs2_xattr_ref *ref, *newref, **pref;
	uint32_t length, request;
	int rc;

	rc = check_xattr_ref_inode(c, ic);
	if (unlikely(rc))
		return rc;

	request = PAD(sizeof(struct jffs2_raw_xattr) + strlen(xname) + 1 + size);
	rc = jffs2_reserve_space(c, request, &length,
				 ALLOC_NORMAL, JFFS2_SUMMARY_XATTR_SIZE);
	if (rc) {
		JFFS2_WARNING("jffs2_reserve_space()=%d, request=%u\n", rc, request);
		return rc;
	}

	/* Find existing xattr */
	down_write(&c->xattr_sem);
 retry:
	for (ref=ic->xref, pref=&ic->xref; ref; pref=&ref->next, ref=ref->next) {
		xd = ref->xd;
		if (xd->xprefix != xprefix)
			continue;
		if (!xd->xname) {
			rc = load_xattr_datum(c, xd);
			if (unlikely(rc > 0)) {
				*pref = ref->next;
				delete_xattr_ref(c, ref);
				goto retry;
			} else if (unlikely(rc < 0))
				goto out;
		}
		if (!strcmp(xd->xname, xname)) {
			if (flags & XATTR_CREATE) {
				rc = -EEXIST;
				goto out;
			}
			if (!buffer) {
				*pref = ref->next;
				delete_xattr_ref(c, ref);
				rc = 0;
				goto out;
			}
			goto found;
		}
	}
	/* not found */
	if (flags & XATTR_REPLACE) {
		rc = -ENODATA;
		goto out;
	}
	if (!buffer) {
		rc = -EINVAL;
		goto out;
	}
 found:
	xd = create_xattr_datum(c, xprefix, xname, buffer, size);
	if (IS_ERR(xd)) {
		rc = PTR_ERR(xd);
		goto out;
	}
	up_write(&c->xattr_sem);
	jffs2_complete_reservation(c);

	/* create xattr_ref */
	request = PAD(sizeof(struct jffs2_raw_xref));
	rc = jffs2_reserve_space(c, request, &length,
				 ALLOC_NORMAL, JFFS2_SUMMARY_XREF_SIZE);
	if (rc) {
		JFFS2_WARNING("jffs2_reserve_space()=%d, request=%u\n", rc, request);
		down_write(&c->xattr_sem);
		xd->refcnt--;
		if (!xd->refcnt)
			delete_xattr_datum(c, xd);
		up_write(&c->xattr_sem);
		return rc;
	}
	down_write(&c->xattr_sem);
	if (ref)
		*pref = ref->next;
	newref = create_xattr_ref(c, ic, xd);
	if (IS_ERR(newref)) {
		if (ref) {
			ref->next = ic->xref;
			ic->xref = ref;
		}
		rc = PTR_ERR(newref);
		xd->refcnt--;
		if (!xd->refcnt)
			delete_xattr_datum(c, xd);
	} else if (ref) {
		delete_xattr_ref(c, ref);
	}
 out:
	up_write(&c->xattr_sem);
	jffs2_complete_reservation(c);
	return rc;
}

/* -------- garbage collector functions -------------
 * jffs2_garbage_collect_xattr_datum(c, xd)
 *   is used to move xdatum into new node.
 * jffs2_garbage_collect_xattr_ref(c, ref)
 *   is used to move xref into new node.
 * jffs2_verify_xattr(c)
 *   is used to call do_verify_xattr_datum() before garbage collecting.
 * -------------------------------------------------- */
int jffs2_garbage_collect_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd)
{
	uint32_t totlen, length, old_ofs;
	int rc = -EINVAL;

	down_write(&c->xattr_sem);
	BUG_ON(!xd->node);

	old_ofs = ref_offset(xd->node);
	totlen = ref_totlen(c, c->gcblock, xd->node);
	if (totlen < sizeof(struct jffs2_raw_xattr))
		goto out;

	if (!xd->xname) {
		rc = load_xattr_datum(c, xd);
		if (unlikely(rc > 0)) {
			delete_xattr_datum_node(c, xd);
			rc = 0;
			goto out;
		} else if (unlikely(rc < 0))
			goto out;
	}
	rc = jffs2_reserve_space_gc(c, totlen, &length, JFFS2_SUMMARY_XATTR_SIZE);
	if (rc || length < totlen) {
		JFFS2_WARNING("jffs2_reserve_space()=%d, request=%u\n", rc, totlen);
		rc = rc ? rc : -EBADFD;
		goto out;
	}
	rc = save_xattr_datum(c, xd);
	if (!rc)
		dbg_xattr("xdatum (xid=%u, version=%u) GC'ed from %#08x to %08x\n",
			  xd->xid, xd->version, old_ofs, ref_offset(xd->node));
 out:
	up_write(&c->xattr_sem);
	return rc;
}


int jffs2_garbage_collect_xattr_ref(struct jffs2_sb_info *c, struct jffs2_xattr_ref *ref)
{
	uint32_t totlen, length, old_ofs;
	int rc = -EINVAL;

	down_write(&c->xattr_sem);
	BUG_ON(!ref->node);

	old_ofs = ref_offset(ref->node);
	totlen = ref_totlen(c, c->gcblock, ref->node);
	if (totlen != sizeof(struct jffs2_raw_xref))
		goto out;

	rc = jffs2_reserve_space_gc(c, totlen, &length, JFFS2_SUMMARY_XREF_SIZE);
	if (rc || length < totlen) {
		JFFS2_WARNING("%s: jffs2_reserve_space() = %d, request = %u\n",
			      __FUNCTION__, rc, totlen);
		rc = rc ? rc : -EBADFD;
		goto out;
	}
	rc = save_xattr_ref(c, ref);
	if (!rc)
		dbg_xattr("xref (ino=%u, xid=%u) GC'ed from %#08x to %08x\n",
			  ref->ic->ino, ref->xd->xid, old_ofs, ref_offset(ref->node));
 out:
	up_write(&c->xattr_sem);
	return rc;
}

int jffs2_verify_xattr(struct jffs2_sb_info *c)
{
	struct jffs2_xattr_datum *xd, *_xd;
	int rc;

	down_write(&c->xattr_sem);
	list_for_each_entry_safe(xd, _xd, &c->xattr_unchecked, xindex) {
		rc = do_verify_xattr_datum(c, xd);
		if (rc == 0) {
			list_del_init(&xd->xindex);
			break;
		} else if (rc > 0) {
			list_del_init(&xd->xindex);
			delete_xattr_datum_node(c, xd);
		}
	}
	up_write(&c->xattr_sem);

	return list_empty(&c->xattr_unchecked) ? 1 : 0;
}
