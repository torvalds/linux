/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: nodelist.h,v 1.140 2005/09/07 08:34:54 havasi Exp $
 *
 */

#ifndef __JFFS2_NODELIST_H__
#define __JFFS2_NODELIST_H__

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/jffs2.h>
#include "jffs2_fs_sb.h"
#include "jffs2_fs_i.h"
#include "xattr.h"
#include "acl.h"
#include "summary.h"

#ifdef __ECOS
#include "os-ecos.h"
#else
#include <linux/mtd/compatmac.h> /* For compatibility with older kernels */
#include "os-linux.h"
#endif

#define JFFS2_NATIVE_ENDIAN

/* Note we handle mode bits conversion from JFFS2 (i.e. Linux) to/from
   whatever OS we're actually running on here too. */

#if defined(JFFS2_NATIVE_ENDIAN)
#define cpu_to_je16(x) ((jint16_t){x})
#define cpu_to_je32(x) ((jint32_t){x})
#define cpu_to_jemode(x) ((jmode_t){os_to_jffs2_mode(x)})

#define je16_to_cpu(x) ((x).v16)
#define je32_to_cpu(x) ((x).v32)
#define jemode_to_cpu(x) (jffs2_to_os_mode((x).m))
#elif defined(JFFS2_BIG_ENDIAN)
#define cpu_to_je16(x) ((jint16_t){cpu_to_be16(x)})
#define cpu_to_je32(x) ((jint32_t){cpu_to_be32(x)})
#define cpu_to_jemode(x) ((jmode_t){cpu_to_be32(os_to_jffs2_mode(x))})

#define je16_to_cpu(x) (be16_to_cpu(x.v16))
#define je32_to_cpu(x) (be32_to_cpu(x.v32))
#define jemode_to_cpu(x) (be32_to_cpu(jffs2_to_os_mode((x).m)))
#elif defined(JFFS2_LITTLE_ENDIAN)
#define cpu_to_je16(x) ((jint16_t){cpu_to_le16(x)})
#define cpu_to_je32(x) ((jint32_t){cpu_to_le32(x)})
#define cpu_to_jemode(x) ((jmode_t){cpu_to_le32(os_to_jffs2_mode(x))})

#define je16_to_cpu(x) (le16_to_cpu(x.v16))
#define je32_to_cpu(x) (le32_to_cpu(x.v32))
#define jemode_to_cpu(x) (le32_to_cpu(jffs2_to_os_mode((x).m)))
#else
#error wibble
#endif

/* The minimal node header size */
#define JFFS2_MIN_NODE_HEADER sizeof(struct jffs2_raw_dirent)

/*
  This is all we need to keep in-core for each raw node during normal
  operation. As and when we do read_inode on a particular inode, we can
  scan the nodes which are listed for it and build up a proper map of
  which nodes are currently valid. JFFSv1 always used to keep that whole
  map in core for each inode.
*/
struct jffs2_raw_node_ref
{
	struct jffs2_raw_node_ref *next_in_ino; /* Points to the next raw_node_ref
		for this object. If this _is_ the last, it points to the inode_cache,
		xattr_ref or xattr_datum instead. The common part of those structures
		has NULL in the first word. See jffs2_raw_ref_to_ic() below */
	uint32_t flash_offset;
#define TEST_TOTLEN
#ifdef TEST_TOTLEN
	uint32_t __totlen; /* This may die; use ref_totlen(c, jeb, ) below */
#endif
};

#define REF_LINK_NODE ((int32_t)-1)
#define REF_EMPTY_NODE ((int32_t)-2)

/* Use blocks of about 256 bytes */
#define REFS_PER_BLOCK ((255/sizeof(struct jffs2_raw_node_ref))-1)

static inline struct jffs2_raw_node_ref *ref_next(struct jffs2_raw_node_ref *ref)
{
	ref++;

	/* Link to another block of refs */
	if (ref->flash_offset == REF_LINK_NODE) {
		ref = ref->next_in_ino;
		if (!ref)
			return ref;
	}

	/* End of chain */
	if (ref->flash_offset == REF_EMPTY_NODE)
		return NULL;

	return ref;
}

static inline struct jffs2_inode_cache *jffs2_raw_ref_to_ic(struct jffs2_raw_node_ref *raw)
{
	while(raw->next_in_ino)
		raw = raw->next_in_ino;

	/* NB. This can be a jffs2_xattr_datum or jffs2_xattr_ref and
	   not actually a jffs2_inode_cache. Check ->class */
	return ((struct jffs2_inode_cache *)raw);
}

        /* flash_offset & 3 always has to be zero, because nodes are
	   always aligned at 4 bytes. So we have a couple of extra bits
	   to play with, which indicate the node's status; see below: */
#define REF_UNCHECKED	0	/* We haven't yet checked the CRC or built its inode */
#define REF_OBSOLETE	1	/* Obsolete, can be completely ignored */
#define REF_PRISTINE	2	/* Completely clean. GC without looking */
#define REF_NORMAL	3	/* Possibly overlapped. Read the page and write again on GC */
#define ref_flags(ref)		((ref)->flash_offset & 3)
#define ref_offset(ref)		((ref)->flash_offset & ~3)
#define ref_obsolete(ref)	(((ref)->flash_offset & 3) == REF_OBSOLETE)
#define mark_ref_normal(ref)    do { (ref)->flash_offset = ref_offset(ref) | REF_NORMAL; } while(0)

/* NB: REF_PRISTINE for an inode-less node (ref->next_in_ino == NULL) indicates
   it is an unknown node of type JFFS2_NODETYPE_RWCOMPAT_COPY, so it'll get
   copied. If you need to do anything different to GC inode-less nodes, then
   you need to modify gc.c accordingly. */

/* For each inode in the filesystem, we need to keep a record of
   nlink, because it would be a PITA to scan the whole directory tree
   at read_inode() time to calculate it, and to keep sufficient information
   in the raw_node_ref (basically both parent and child inode number for
   dirent nodes) would take more space than this does. We also keep
   a pointer to the first physical node which is part of this inode, too.
*/
struct jffs2_inode_cache {
	/* First part of structure is shared with other objects which
	   can terminate the raw node refs' next_in_ino list -- which
	   currently struct jffs2_xattr_datum and struct jffs2_xattr_ref. */

	struct jffs2_full_dirent *scan_dents; /* Used during scan to hold
		temporary lists of dirents, and later must be set to
		NULL to mark the end of the raw_node_ref->next_in_ino
		chain. */
	struct jffs2_raw_node_ref *nodes;
	uint8_t class;	/* It's used for identification */

	/* end of shared structure */

	uint8_t flags;
	uint16_t state;
	uint32_t ino;
	struct jffs2_inode_cache *next;
#ifdef CONFIG_JFFS2_FS_XATTR
	struct jffs2_xattr_ref *xref;
#endif
	int nlink;
};

/* Inode states for 'state' above. We need the 'GC' state to prevent
   someone from doing a read_inode() while we're moving a 'REF_PRISTINE'
   node without going through all the iget() nonsense */
#define INO_STATE_UNCHECKED	0	/* CRC checks not yet done */
#define INO_STATE_CHECKING	1	/* CRC checks in progress */
#define INO_STATE_PRESENT	2	/* In core */
#define INO_STATE_CHECKEDABSENT	3	/* Checked, cleared again */
#define INO_STATE_GC		4	/* GCing a 'pristine' node */
#define INO_STATE_READING	5	/* In read_inode() */
#define INO_STATE_CLEARING	6	/* In clear_inode() */

#define INO_FLAGS_XATTR_CHECKED	0x01	/* has no duplicate xattr_ref */

#define RAWNODE_CLASS_INODE_CACHE	0
#define RAWNODE_CLASS_XATTR_DATUM	1
#define RAWNODE_CLASS_XATTR_REF		2

#define INOCACHE_HASHSIZE 128

#define write_ofs(c) ((c)->nextblock->offset + (c)->sector_size - (c)->nextblock->free_size)

/*
  Larger representation of a raw node, kept in-core only when the
  struct inode for this particular ino is instantiated.
*/

struct jffs2_full_dnode
{
	struct jffs2_raw_node_ref *raw;
	uint32_t ofs; /* The offset to which the data of this node belongs */
	uint32_t size;
	uint32_t frags; /* Number of fragments which currently refer
			to this node. When this reaches zero,
			the node is obsolete.  */
};

/*
   Even larger representation of a raw node, kept in-core only while
   we're actually building up the original map of which nodes go where,
   in read_inode()
*/
struct jffs2_tmp_dnode_info
{
	struct rb_node rb;
	struct jffs2_full_dnode *fn;
	uint32_t version;
	uint32_t data_crc;
	uint32_t partial_crc;
	uint32_t csize;
};

struct jffs2_full_dirent
{
	struct jffs2_raw_node_ref *raw;
	struct jffs2_full_dirent *next;
	uint32_t version;
	uint32_t ino; /* == zero for unlink */
	unsigned int nhash;
	unsigned char type;
	unsigned char name[0];
};

/*
  Fragments - used to build a map of which raw node to obtain
  data from for each part of the ino
*/
struct jffs2_node_frag
{
	struct rb_node rb;
	struct jffs2_full_dnode *node; /* NULL for holes */
	uint32_t size;
	uint32_t ofs; /* The offset to which this fragment belongs */
};

struct jffs2_eraseblock
{
	struct list_head list;
	int bad_count;
	uint32_t offset;		/* of this block in the MTD */

	uint32_t unchecked_size;
	uint32_t used_size;
	uint32_t dirty_size;
	uint32_t wasted_size;
	uint32_t free_size;	/* Note that sector_size - free_size
				   is the address of the first free space */
	uint32_t allocated_refs;
	struct jffs2_raw_node_ref *first_node;
	struct jffs2_raw_node_ref *last_node;

	struct jffs2_raw_node_ref *gc_node;	/* Next node to be garbage collected */
};

static inline int jffs2_blocks_use_vmalloc(struct jffs2_sb_info *c)
{
	return ((c->flash_size / c->sector_size) * sizeof (struct jffs2_eraseblock)) > (128 * 1024);
}

#define ref_totlen(a, b, c) __jffs2_ref_totlen((a), (b), (c))

#define ALLOC_NORMAL	0	/* Normal allocation */
#define ALLOC_DELETION	1	/* Deletion node. Best to allow it */
#define ALLOC_GC	2	/* Space requested for GC. Give it or die */
#define ALLOC_NORETRY	3	/* For jffs2_write_dnode: On failure, return -EAGAIN instead of retrying */

/* How much dirty space before it goes on the very_dirty_list */
#define VERYDIRTY(c, size) ((size) >= ((c)->sector_size / 2))

/* check if dirty space is more than 255 Byte */
#define ISDIRTY(size) ((size) >  sizeof (struct jffs2_raw_inode) + JFFS2_MIN_DATA_LEN)

#define PAD(x) (((x)+3)&~3)

static inline int jffs2_encode_dev(union jffs2_device_node *jdev, dev_t rdev)
{
	if (old_valid_dev(rdev)) {
		jdev->old = cpu_to_je16(old_encode_dev(rdev));
		return sizeof(jdev->old);
	} else {
		jdev->new = cpu_to_je32(new_encode_dev(rdev));
		return sizeof(jdev->new);
	}
}

static inline struct jffs2_node_frag *frag_first(struct rb_root *root)
{
	struct rb_node *node = root->rb_node;

	if (!node)
		return NULL;
	while(node->rb_left)
		node = node->rb_left;
	return rb_entry(node, struct jffs2_node_frag, rb);
}

static inline struct jffs2_node_frag *frag_last(struct rb_root *root)
{
	struct rb_node *node = root->rb_node;

	if (!node)
		return NULL;
	while(node->rb_right)
		node = node->rb_right;
	return rb_entry(node, struct jffs2_node_frag, rb);
}

#define rb_parent(rb) ((rb)->rb_parent)
#define frag_next(frag) rb_entry(rb_next(&(frag)->rb), struct jffs2_node_frag, rb)
#define frag_prev(frag) rb_entry(rb_prev(&(frag)->rb), struct jffs2_node_frag, rb)
#define frag_parent(frag) rb_entry(rb_parent(&(frag)->rb), struct jffs2_node_frag, rb)
#define frag_left(frag) rb_entry((frag)->rb.rb_left, struct jffs2_node_frag, rb)
#define frag_right(frag) rb_entry((frag)->rb.rb_right, struct jffs2_node_frag, rb)
#define frag_erase(frag, list) rb_erase(&frag->rb, list);

/* nodelist.c */
void jffs2_add_fd_to_list(struct jffs2_sb_info *c, struct jffs2_full_dirent *new, struct jffs2_full_dirent **list);
void jffs2_set_inocache_state(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic, int state);
struct jffs2_inode_cache *jffs2_get_ino_cache(struct jffs2_sb_info *c, uint32_t ino);
void jffs2_add_ino_cache (struct jffs2_sb_info *c, struct jffs2_inode_cache *new);
void jffs2_del_ino_cache(struct jffs2_sb_info *c, struct jffs2_inode_cache *old);
void jffs2_free_ino_caches(struct jffs2_sb_info *c);
void jffs2_free_raw_node_refs(struct jffs2_sb_info *c);
struct jffs2_node_frag *jffs2_lookup_node_frag(struct rb_root *fragtree, uint32_t offset);
void jffs2_kill_fragtree(struct rb_root *root, struct jffs2_sb_info *c_delete);
struct rb_node *rb_next(struct rb_node *);
struct rb_node *rb_prev(struct rb_node *);
void rb_replace_node(struct rb_node *victim, struct rb_node *new, struct rb_root *root);
void jffs2_obsolete_node_frag(struct jffs2_sb_info *c, struct jffs2_node_frag *this);
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn);
void jffs2_truncate_fragtree (struct jffs2_sb_info *c, struct rb_root *list, uint32_t size);
int jffs2_add_older_frag_to_fragtree(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_tmp_dnode_info *tn);
struct jffs2_raw_node_ref *jffs2_link_node_ref(struct jffs2_sb_info *c,
					       struct jffs2_eraseblock *jeb,
					       uint32_t ofs, uint32_t len,
					       struct jffs2_inode_cache *ic);
extern uint32_t __jffs2_ref_totlen(struct jffs2_sb_info *c,
				   struct jffs2_eraseblock *jeb,
				   struct jffs2_raw_node_ref *ref);

/* nodemgmt.c */
int jffs2_thread_should_wake(struct jffs2_sb_info *c);
int jffs2_reserve_space(struct jffs2_sb_info *c, uint32_t minsize,
			uint32_t *len, int prio, uint32_t sumsize);
int jffs2_reserve_space_gc(struct jffs2_sb_info *c, uint32_t minsize,
			uint32_t *len, uint32_t sumsize);
struct jffs2_raw_node_ref *jffs2_add_physical_node_ref(struct jffs2_sb_info *c, 
						       uint32_t ofs, uint32_t len,
						       struct jffs2_inode_cache *ic);
void jffs2_complete_reservation(struct jffs2_sb_info *c);
void jffs2_mark_node_obsolete(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *raw);

/* write.c */
int jffs2_do_new_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, uint32_t mode, struct jffs2_raw_inode *ri);

struct jffs2_full_dnode *jffs2_write_dnode(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
					   struct jffs2_raw_inode *ri, const unsigned char *data,
					   uint32_t datalen, int alloc_mode);
struct jffs2_full_dirent *jffs2_write_dirent(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
					     struct jffs2_raw_dirent *rd, const unsigned char *name,
					     uint32_t namelen, int alloc_mode);
int jffs2_write_inode_range(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			    struct jffs2_raw_inode *ri, unsigned char *buf,
			    uint32_t offset, uint32_t writelen, uint32_t *retlen);
int jffs2_do_create(struct jffs2_sb_info *c, struct jffs2_inode_info *dir_f, struct jffs2_inode_info *f,
		    struct jffs2_raw_inode *ri, const char *name, int namelen);
int jffs2_do_unlink(struct jffs2_sb_info *c, struct jffs2_inode_info *dir_f, const char *name,
		    int namelen, struct jffs2_inode_info *dead_f, uint32_t time);
int jffs2_do_link(struct jffs2_sb_info *c, struct jffs2_inode_info *dir_f, uint32_t ino,
		   uint8_t type, const char *name, int namelen, uint32_t time);


/* readinode.c */
int jffs2_do_read_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			uint32_t ino, struct jffs2_raw_inode *latest_node);
int jffs2_do_crccheck_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic);
void jffs2_do_clear_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f);

/* malloc.c */
int jffs2_create_slab_caches(void);
void jffs2_destroy_slab_caches(void);

struct jffs2_full_dirent *jffs2_alloc_full_dirent(int namesize);
void jffs2_free_full_dirent(struct jffs2_full_dirent *);
struct jffs2_full_dnode *jffs2_alloc_full_dnode(void);
void jffs2_free_full_dnode(struct jffs2_full_dnode *);
struct jffs2_raw_dirent *jffs2_alloc_raw_dirent(void);
void jffs2_free_raw_dirent(struct jffs2_raw_dirent *);
struct jffs2_raw_inode *jffs2_alloc_raw_inode(void);
void jffs2_free_raw_inode(struct jffs2_raw_inode *);
struct jffs2_tmp_dnode_info *jffs2_alloc_tmp_dnode_info(void);
void jffs2_free_tmp_dnode_info(struct jffs2_tmp_dnode_info *);
int jffs2_prealloc_raw_node_refs(struct jffs2_sb_info *c,
				 struct jffs2_eraseblock *jeb, int nr);
void jffs2_free_refblock(struct jffs2_raw_node_ref *);
struct jffs2_node_frag *jffs2_alloc_node_frag(void);
void jffs2_free_node_frag(struct jffs2_node_frag *);
struct jffs2_inode_cache *jffs2_alloc_inode_cache(void);
void jffs2_free_inode_cache(struct jffs2_inode_cache *);
#ifdef CONFIG_JFFS2_FS_XATTR
struct jffs2_xattr_datum *jffs2_alloc_xattr_datum(void);
void jffs2_free_xattr_datum(struct jffs2_xattr_datum *);
struct jffs2_xattr_ref *jffs2_alloc_xattr_ref(void);
void jffs2_free_xattr_ref(struct jffs2_xattr_ref *);
#endif

/* gc.c */
int jffs2_garbage_collect_pass(struct jffs2_sb_info *c);

/* read.c */
int jffs2_read_dnode(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
		     struct jffs2_full_dnode *fd, unsigned char *buf,
		     int ofs, int len);
int jffs2_read_inode_range(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			   unsigned char *buf, uint32_t offset, uint32_t len);
char *jffs2_getlink(struct jffs2_sb_info *c, struct jffs2_inode_info *f);

/* scan.c */
int jffs2_scan_medium(struct jffs2_sb_info *c);
void jffs2_rotate_lists(struct jffs2_sb_info *c);
int jffs2_fill_scan_buf(struct jffs2_sb_info *c, void *buf,
				uint32_t ofs, uint32_t len);
struct jffs2_inode_cache *jffs2_scan_make_ino_cache(struct jffs2_sb_info *c, uint32_t ino);
int jffs2_scan_classify_jeb(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_scan_dirty_space(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t size);

/* build.c */
int jffs2_do_mount_fs(struct jffs2_sb_info *c);

/* erase.c */
void jffs2_erase_pending_blocks(struct jffs2_sb_info *c, int count);
void jffs2_free_jeb_node_refs(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
/* wbuf.c */
int jffs2_flush_wbuf_gc(struct jffs2_sb_info *c, uint32_t ino);
int jffs2_flush_wbuf_pad(struct jffs2_sb_info *c);
int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
#endif

#include "debug.h"

#endif /* __JFFS2_NODELIST_H__ */
