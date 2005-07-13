/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: nodelist.h,v 1.131 2005/07/05 21:03:07 dwmw2 Exp $
 *
 */

#ifndef __JFFS2_NODELIST_H__
#define __JFFS2_NODELIST_H__

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/jffs2.h>
#include <linux/jffs2_fs_sb.h>
#include <linux/jffs2_fs_i.h>

#ifdef __ECOS
#include "os-ecos.h"
#else
#include <linux/mtd/compatmac.h> /* For min/max in older kernels */
#include "os-linux.h"
#endif

#ifndef CONFIG_JFFS2_FS_DEBUG
#define CONFIG_JFFS2_FS_DEBUG 1
#endif

#if CONFIG_JFFS2_FS_DEBUG > 0
#define D1(x) x
#else
#define D1(x)
#endif

#if CONFIG_JFFS2_FS_DEBUG > 1
#define D2(x) x
#else
#define D2(x)
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
		for this inode. If this is the last, it points to the inode_cache
		for this inode instead. The inode_cache will have NULL in the first
		word so you know when you've got there :) */
	struct jffs2_raw_node_ref *next_phys;
	uint32_t flash_offset;
	uint32_t __totlen; /* This may die; use ref_totlen(c, jeb, ) below */
};

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

/* For each inode in the filesystem, we need to keep a record of
   nlink, because it would be a PITA to scan the whole directory tree
   at read_inode() time to calculate it, and to keep sufficient information
   in the raw_node_ref (basically both parent and child inode number for 
   dirent nodes) would take more space than this does. We also keep
   a pointer to the first physical node which is part of this inode, too.
*/
struct jffs2_inode_cache {
	struct jffs2_full_dirent *scan_dents; /* Used during scan to hold
		temporary lists of dirents, and later must be set to
		NULL to mark the end of the raw_node_ref->next_in_ino
		chain. */
	struct jffs2_inode_cache *next;
	struct jffs2_raw_node_ref *nodes;
	uint32_t ino;
	int nlink;
	int state;
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

#define INOCACHE_HASHSIZE 128

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
	struct jffs2_raw_node_ref *first_node;
	struct jffs2_raw_node_ref *last_node;

	struct jffs2_raw_node_ref *gc_node;	/* Next node to be garbage collected */
};

#define ACCT_SANITY_CHECK(c, jeb) do { \
		struct jffs2_eraseblock *___j = jeb; \
		if ((___j) && ___j->used_size + ___j->dirty_size + ___j->free_size + ___j->wasted_size + ___j->unchecked_size != c->sector_size) { \
		printk(KERN_NOTICE "Eeep. Space accounting for block at 0x%08x is screwed\n", ___j->offset); \
		printk(KERN_NOTICE "free 0x%08x + dirty 0x%08x + used %08x + wasted %08x + unchecked %08x != total %08x\n", \
		___j->free_size, ___j->dirty_size, ___j->used_size, ___j->wasted_size, ___j->unchecked_size, c->sector_size); \
		BUG(); \
	} \
	if (c->used_size + c->dirty_size + c->free_size + c->erasing_size + c->bad_size + c->wasted_size + c->unchecked_size != c->flash_size) { \
		printk(KERN_NOTICE "Eeep. Space accounting superblock info is screwed\n"); \
		printk(KERN_NOTICE "free 0x%08x + dirty 0x%08x + used %08x + erasing %08x + bad %08x + wasted %08x + unchecked %08x != total %08x\n", \
		c->free_size, c->dirty_size, c->used_size, c->erasing_size, c->bad_size, c->wasted_size, c->unchecked_size, c->flash_size); \
		BUG(); \
	} \
} while(0)

static inline void paranoia_failed_dump(struct jffs2_eraseblock *jeb)
{
	struct jffs2_raw_node_ref *ref;
	int i=0;

	printk(KERN_NOTICE);
	for (ref = jeb->first_node; ref; ref = ref->next_phys) {
		printk("%08x->", ref_offset(ref));
		if (++i == 8) {
			i = 0;
			printk("\n" KERN_NOTICE);
		}
	}
	printk("\n");
}


#define ACCT_PARANOIA_CHECK(jeb) do { \
		uint32_t my_used_size = 0; \
		uint32_t my_unchecked_size = 0; \
		struct jffs2_raw_node_ref *ref2 = jeb->first_node; \
		while (ref2) { \
			if (unlikely(ref2->flash_offset < jeb->offset || \
				     ref2->flash_offset > jeb->offset + c->sector_size)) { \
				printk(KERN_NOTICE "Node %08x shouldn't be in block at %08x!\n", \
				       ref_offset(ref2), jeb->offset); \
				paranoia_failed_dump(jeb); \
				BUG(); \
			} \
			if (ref_flags(ref2) == REF_UNCHECKED) \
				my_unchecked_size += ref_totlen(c, jeb, ref2); \
			else if (!ref_obsolete(ref2)) \
				my_used_size += ref_totlen(c, jeb, ref2); \
			if (unlikely((!ref2->next_phys) != (ref2 == jeb->last_node))) { \
                                if (!ref2->next_phys) \
				       printk("ref for node at %p (phys %08x) has next_phys->%p (----), last_node->%p (phys %08x)\n", \
				             ref2, ref_offset(ref2), ref2->next_phys, \
				             jeb->last_node, ref_offset(jeb->last_node)); \
                                else \
                                       printk("ref for node at %p (phys %08x) has next_phys->%p (%08x), last_node->%p (phys %08x)\n", \
				             ref2, ref_offset(ref2), ref2->next_phys, ref_offset(ref2->next_phys), \
				             jeb->last_node, ref_offset(jeb->last_node)); \
				paranoia_failed_dump(jeb); \
				BUG(); \
			} \
			ref2 = ref2->next_phys; \
		} \
		if (my_used_size != jeb->used_size) { \
			printk(KERN_NOTICE "Calculated used size %08x != stored used size %08x\n", my_used_size, jeb->used_size); \
			BUG(); \
		} \
		if (my_unchecked_size != jeb->unchecked_size) { \
			printk(KERN_NOTICE "Calculated unchecked size %08x != stored unchecked size %08x\n", my_unchecked_size, jeb->unchecked_size); \
			BUG(); \
		} \
	} while(0)

/* Calculate totlen from surrounding nodes or eraseblock */
static inline uint32_t __ref_totlen(struct jffs2_sb_info *c,
				    struct jffs2_eraseblock *jeb,
				    struct jffs2_raw_node_ref *ref)
{
	uint32_t ref_end;
	
	if (ref->next_phys)
		ref_end = ref_offset(ref->next_phys);
	else {
		if (!jeb)
			jeb = &c->blocks[ref->flash_offset / c->sector_size];

		/* Last node in block. Use free_space */
		BUG_ON(ref != jeb->last_node);
		ref_end = jeb->offset + c->sector_size - jeb->free_size;
	}
	return ref_end - ref_offset(ref);
}

static inline uint32_t ref_totlen(struct jffs2_sb_info *c,
				  struct jffs2_eraseblock *jeb,
				  struct jffs2_raw_node_ref *ref)
{
	uint32_t ret;

	D1(if (jeb && jeb != &c->blocks[ref->flash_offset / c->sector_size]) {
		printk(KERN_CRIT "ref_totlen called with wrong block -- at 0x%08x instead of 0x%08x; ref 0x%08x\n",
		       jeb->offset, c->blocks[ref->flash_offset / c->sector_size].offset, ref_offset(ref));
		BUG();
	})

#if 1
	ret = ref->__totlen;
#else
	/* This doesn't actually work yet */
	ret = __ref_totlen(c, jeb, ref);
	if (ret != ref->__totlen) {
		printk(KERN_CRIT "Totlen for ref at %p (0x%08x-0x%08x) miscalculated as 0x%x instead of %x\n",
		       ref, ref_offset(ref), ref_offset(ref)+ref->__totlen,
		       ret, ref->__totlen);
		if (!jeb)
			jeb = &c->blocks[ref->flash_offset / c->sector_size];
		paranoia_failed_dump(jeb);
		BUG();
	}
#endif
	return ret;
}


#define ALLOC_NORMAL	0	/* Normal allocation */
#define ALLOC_DELETION	1	/* Deletion node. Best to allow it */
#define ALLOC_GC	2	/* Space requested for GC. Give it or die */
#define ALLOC_NORETRY	3	/* For jffs2_write_dnode: On failure, return -EAGAIN instead of retrying */

/* How much dirty space before it goes on the very_dirty_list */
#define VERYDIRTY(c, size) ((size) >= ((c)->sector_size / 2))

/* check if dirty space is more than 255 Byte */
#define ISDIRTY(size) ((size) >  sizeof (struct jffs2_raw_inode) + JFFS2_MIN_DATA_LEN) 

#define PAD(x) (((x)+3)&~3)

static inline struct jffs2_inode_cache *jffs2_raw_ref_to_ic(struct jffs2_raw_node_ref *raw)
{
	while(raw->next_in_ino) {
		raw = raw->next_in_ino;
	}

	return ((struct jffs2_inode_cache *)raw);
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
D2(void jffs2_print_frag_list(struct jffs2_inode_info *f));
void jffs2_add_fd_to_list(struct jffs2_sb_info *c, struct jffs2_full_dirent *new, struct jffs2_full_dirent **list);
int jffs2_get_inode_nodes(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			  struct rb_root *tnp, struct jffs2_full_dirent **fdp,
			  uint32_t *highest_version, uint32_t *latest_mctime,
			  uint32_t *mctime_ver);
void jffs2_set_inocache_state(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic, int state);
struct jffs2_inode_cache *jffs2_get_ino_cache(struct jffs2_sb_info *c, uint32_t ino);
void jffs2_add_ino_cache (struct jffs2_sb_info *c, struct jffs2_inode_cache *new);
void jffs2_del_ino_cache(struct jffs2_sb_info *c, struct jffs2_inode_cache *old);
void jffs2_free_ino_caches(struct jffs2_sb_info *c);
void jffs2_free_raw_node_refs(struct jffs2_sb_info *c);
struct jffs2_node_frag *jffs2_lookup_node_frag(struct rb_root *fragtree, uint32_t offset);
void jffs2_kill_fragtree(struct rb_root *root, struct jffs2_sb_info *c_delete);
void jffs2_fragtree_insert(struct jffs2_node_frag *newfrag, struct jffs2_node_frag *base);
struct rb_node *rb_next(struct rb_node *);
struct rb_node *rb_prev(struct rb_node *);
void rb_replace_node(struct rb_node *victim, struct rb_node *new, struct rb_root *root);

/* nodemgmt.c */
int jffs2_thread_should_wake(struct jffs2_sb_info *c);
int jffs2_reserve_space(struct jffs2_sb_info *c, uint32_t minsize, uint32_t *ofs, uint32_t *len, int prio);
int jffs2_reserve_space_gc(struct jffs2_sb_info *c, uint32_t minsize, uint32_t *ofs, uint32_t *len);
int jffs2_add_physical_node_ref(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *new);
void jffs2_complete_reservation(struct jffs2_sb_info *c);
void jffs2_mark_node_obsolete(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *raw);
void jffs2_dump_block_lists(struct jffs2_sb_info *c);

/* write.c */
int jffs2_do_new_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, uint32_t mode, struct jffs2_raw_inode *ri);

struct jffs2_full_dnode *jffs2_write_dnode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_raw_inode *ri, const unsigned char *data, uint32_t datalen, uint32_t flash_ofs, int alloc_mode);
struct jffs2_full_dirent *jffs2_write_dirent(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_raw_dirent *rd, const unsigned char *name, uint32_t namelen, uint32_t flash_ofs, int alloc_mode);
int jffs2_write_inode_range(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			    struct jffs2_raw_inode *ri, unsigned char *buf, 
			    uint32_t offset, uint32_t writelen, uint32_t *retlen);
int jffs2_do_create(struct jffs2_sb_info *c, struct jffs2_inode_info *dir_f, struct jffs2_inode_info *f, struct jffs2_raw_inode *ri, const char *name, int namelen);
int jffs2_do_unlink(struct jffs2_sb_info *c, struct jffs2_inode_info *dir_f, const char *name, int namelen, struct jffs2_inode_info *dead_f);
int jffs2_do_link (struct jffs2_sb_info *c, struct jffs2_inode_info *dir_f, uint32_t ino, uint8_t type, const char *name, int namelen);


/* readinode.c */
void jffs2_truncate_fraglist (struct jffs2_sb_info *c, struct rb_root *list, uint32_t size);
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn);
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
struct jffs2_raw_node_ref *jffs2_alloc_raw_node_ref(void);
void jffs2_free_raw_node_ref(struct jffs2_raw_node_ref *);
struct jffs2_node_frag *jffs2_alloc_node_frag(void);
void jffs2_free_node_frag(struct jffs2_node_frag *);
struct jffs2_inode_cache *jffs2_alloc_inode_cache(void);
void jffs2_free_inode_cache(struct jffs2_inode_cache *);

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

/* build.c */
int jffs2_do_mount_fs(struct jffs2_sb_info *c);

/* erase.c */
void jffs2_erase_pending_blocks(struct jffs2_sb_info *c, int count);

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
/* wbuf.c */
int jffs2_flush_wbuf_gc(struct jffs2_sb_info *c, uint32_t ino);
int jffs2_flush_wbuf_pad(struct jffs2_sb_info *c);
int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
#endif

#endif /* __JFFS2_NODELIST_H__ */
