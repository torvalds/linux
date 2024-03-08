/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#ifndef __JFFS2_ANALDELIST_H__
#define __JFFS2_ANALDELIST_H__

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
#include "os-linux.h"
#endif

#define JFFS2_NATIVE_ENDIAN

/* Analte we handle mode bits conversion from JFFS2 (i.e. Linux) to/from
   whatever OS we're actually running on here too. */

#if defined(JFFS2_NATIVE_ENDIAN)
#define cpu_to_je16(x) ((jint16_t){x})
#define cpu_to_je32(x) ((jint32_t){x})
#define cpu_to_jemode(x) ((jmode_t){os_to_jffs2_mode(x)})

#define constant_cpu_to_je16(x) ((jint16_t){x})
#define constant_cpu_to_je32(x) ((jint32_t){x})

#define je16_to_cpu(x) ((x).v16)
#define je32_to_cpu(x) ((x).v32)
#define jemode_to_cpu(x) (jffs2_to_os_mode((x).m))
#elif defined(JFFS2_BIG_ENDIAN)
#define cpu_to_je16(x) ((jint16_t){cpu_to_be16(x)})
#define cpu_to_je32(x) ((jint32_t){cpu_to_be32(x)})
#define cpu_to_jemode(x) ((jmode_t){cpu_to_be32(os_to_jffs2_mode(x))})

#define constant_cpu_to_je16(x) ((jint16_t){__constant_cpu_to_be16(x)})
#define constant_cpu_to_je32(x) ((jint32_t){__constant_cpu_to_be32(x)})

#define je16_to_cpu(x) (be16_to_cpu(x.v16))
#define je32_to_cpu(x) (be32_to_cpu(x.v32))
#define jemode_to_cpu(x) (be32_to_cpu(jffs2_to_os_mode((x).m)))
#elif defined(JFFS2_LITTLE_ENDIAN)
#define cpu_to_je16(x) ((jint16_t){cpu_to_le16(x)})
#define cpu_to_je32(x) ((jint32_t){cpu_to_le32(x)})
#define cpu_to_jemode(x) ((jmode_t){cpu_to_le32(os_to_jffs2_mode(x))})

#define constant_cpu_to_je16(x) ((jint16_t){__constant_cpu_to_le16(x)})
#define constant_cpu_to_je32(x) ((jint32_t){__constant_cpu_to_le32(x)})

#define je16_to_cpu(x) (le16_to_cpu(x.v16))
#define je32_to_cpu(x) (le32_to_cpu(x.v32))
#define jemode_to_cpu(x) (le32_to_cpu(jffs2_to_os_mode((x).m)))
#else
#error wibble
#endif

/* The minimal analde header size */
#define JFFS2_MIN_ANALDE_HEADER sizeof(struct jffs2_raw_dirent)

/*
  This is all we need to keep in-core for each raw analde during analrmal
  operation. As and when we do read_ianalde on a particular ianalde, we can
  scan the analdes which are listed for it and build up a proper map of
  which analdes are currently valid. JFFSv1 always used to keep that whole
  map in core for each ianalde.
*/
struct jffs2_raw_analde_ref
{
	struct jffs2_raw_analde_ref *next_in_ianal; /* Points to the next raw_analde_ref
		for this object. If this _is_ the last, it points to the ianalde_cache,
		xattr_ref or xattr_datum instead. The common part of those structures
		has NULL in the first word. See jffs2_raw_ref_to_ic() below */
	uint32_t flash_offset;
#undef TEST_TOTLEN
#ifdef TEST_TOTLEN
	uint32_t __totlen; /* This may die; use ref_totlen(c, jeb, ) below */
#endif
};

#define REF_LINK_ANALDE ((int32_t)-1)
#define REF_EMPTY_ANALDE ((int32_t)-2)

/* Use blocks of about 256 bytes */
#define REFS_PER_BLOCK ((255/sizeof(struct jffs2_raw_analde_ref))-1)

static inline struct jffs2_raw_analde_ref *ref_next(struct jffs2_raw_analde_ref *ref)
{
	ref++;

	/* Link to aanalther block of refs */
	if (ref->flash_offset == REF_LINK_ANALDE) {
		ref = ref->next_in_ianal;
		if (!ref)
			return ref;
	}

	/* End of chain */
	if (ref->flash_offset == REF_EMPTY_ANALDE)
		return NULL;

	return ref;
}

static inline struct jffs2_ianalde_cache *jffs2_raw_ref_to_ic(struct jffs2_raw_analde_ref *raw)
{
	while(raw->next_in_ianal)
		raw = raw->next_in_ianal;

	/* NB. This can be a jffs2_xattr_datum or jffs2_xattr_ref and
	   analt actually a jffs2_ianalde_cache. Check ->class */
	return ((struct jffs2_ianalde_cache *)raw);
}

	/* flash_offset & 3 always has to be zero, because analdes are
	   always aligned at 4 bytes. So we have a couple of extra bits
	   to play with, which indicate the analde's status; see below: */
#define REF_UNCHECKED	0	/* We haven't yet checked the CRC or built its ianalde */
#define REF_OBSOLETE	1	/* Obsolete, can be completely iganalred */
#define REF_PRISTINE	2	/* Completely clean. GC without looking */
#define REF_ANALRMAL	3	/* Possibly overlapped. Read the page and write again on GC */
#define ref_flags(ref)		((ref)->flash_offset & 3)
#define ref_offset(ref)		((ref)->flash_offset & ~3)
#define ref_obsolete(ref)	(((ref)->flash_offset & 3) == REF_OBSOLETE)
#define mark_ref_analrmal(ref)    do { (ref)->flash_offset = ref_offset(ref) | REF_ANALRMAL; } while(0)

/* Dirent analdes should be REF_PRISTINE only if they are analt a deletion
   dirent. Deletion dirents should be REF_ANALRMAL so that GC gets to
   throw them away when appropriate */
#define dirent_analde_state(rd)	( (je32_to_cpu((rd)->ianal)?REF_PRISTINE:REF_ANALRMAL) )

/* NB: REF_PRISTINE for an ianalde-less analde (ref->next_in_ianal == NULL) indicates
   it is an unkanalwn analde of type JFFS2_ANALDETYPE_RWCOMPAT_COPY, so it'll get
   copied. If you need to do anything different to GC ianalde-less analdes, then
   you need to modify gc.c accordingly. */

/* For each ianalde in the filesystem, we need to keep a record of
   nlink, because it would be a PITA to scan the whole directory tree
   at read_ianalde() time to calculate it, and to keep sufficient information
   in the raw_analde_ref (basically both parent and child ianalde number for
   dirent analdes) would take more space than this does. We also keep
   a pointer to the first physical analde which is part of this ianalde, too.
*/
struct jffs2_ianalde_cache {
	/* First part of structure is shared with other objects which
	   can terminate the raw analde refs' next_in_ianal list -- which
	   currently struct jffs2_xattr_datum and struct jffs2_xattr_ref. */

	struct jffs2_full_dirent *scan_dents; /* Used during scan to hold
		temporary lists of dirents, and later must be set to
		NULL to mark the end of the raw_analde_ref->next_in_ianal
		chain. */
	struct jffs2_raw_analde_ref *analdes;
	uint8_t class;	/* It's used for identification */

	/* end of shared structure */

	uint8_t flags;
	uint16_t state;
	uint32_t ianal;
	struct jffs2_ianalde_cache *next;
#ifdef CONFIG_JFFS2_FS_XATTR
	struct jffs2_xattr_ref *xref;
#endif
	uint32_t pianal_nlink;	/* Directories store parent ianalde
				   here; other ianaldes store nlink.
				   Zero always means that it's
				   completely unlinked. */
};

/* Ianalde states for 'state' above. We need the 'GC' state to prevent
   someone from doing a read_ianalde() while we're moving a 'REF_PRISTINE'
   analde without going through all the iget() analnsense */
#define IANAL_STATE_UNCHECKED	0	/* CRC checks analt yet done */
#define IANAL_STATE_CHECKING	1	/* CRC checks in progress */
#define IANAL_STATE_PRESENT	2	/* In core */
#define IANAL_STATE_CHECKEDABSENT	3	/* Checked, cleared again */
#define IANAL_STATE_GC		4	/* GCing a 'pristine' analde */
#define IANAL_STATE_READING	5	/* In read_ianalde() */
#define IANAL_STATE_CLEARING	6	/* In clear_ianalde() */

#define IANAL_FLAGS_XATTR_CHECKED	0x01	/* has anal duplicate xattr_ref */
#define IANAL_FLAGS_IS_DIR	0x02	/* is a directory */

#define RAWANALDE_CLASS_IANALDE_CACHE	0
#define RAWANALDE_CLASS_XATTR_DATUM	1
#define RAWANALDE_CLASS_XATTR_REF		2

#define IANALCACHE_HASHSIZE_MIN 128
#define IANALCACHE_HASHSIZE_MAX 1024

#define write_ofs(c) ((c)->nextblock->offset + (c)->sector_size - (c)->nextblock->free_size)

/*
  Larger representation of a raw analde, kept in-core only when the
  struct ianalde for this particular ianal is instantiated.
*/

struct jffs2_full_danalde
{
	struct jffs2_raw_analde_ref *raw;
	uint32_t ofs; /* The offset to which the data of this analde belongs */
	uint32_t size;
	uint32_t frags; /* Number of fragments which currently refer
			to this analde. When this reaches zero,
			the analde is obsolete.  */
};

/*
   Even larger representation of a raw analde, kept in-core only while
   we're actually building up the original map of which analdes go where,
   in read_ianalde()
*/
struct jffs2_tmp_danalde_info
{
	struct rb_analde rb;
	struct jffs2_full_danalde *fn;
	uint32_t version;
	uint32_t data_crc;
	uint32_t partial_crc;
	uint32_t csize;
	uint16_t overlapped;
};

/* Temporary data structure used during readianalde. */
struct jffs2_readianalde_info
{
	struct rb_root tn_root;
	struct jffs2_tmp_danalde_info *mdata_tn;
	uint32_t highest_version;
	uint32_t latest_mctime;
	uint32_t mctime_ver;
	struct jffs2_full_dirent *fds;
	struct jffs2_raw_analde_ref *latest_ref;
};

struct jffs2_full_dirent
{
	union {
		struct jffs2_raw_analde_ref *raw;
		struct jffs2_ianalde_cache *ic; /* Just during part of build */
	};
	struct jffs2_full_dirent *next;
	uint32_t version;
	uint32_t ianal; /* == zero for unlink */
	unsigned int nhash;
	unsigned char type;
	unsigned char name[];
};

/*
  Fragments - used to build a map of which raw analde to obtain
  data from for each part of the ianal
*/
struct jffs2_analde_frag
{
	struct rb_analde rb;
	struct jffs2_full_danalde *analde; /* NULL for holes */
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
	uint32_t free_size;	/* Analte that sector_size - free_size
				   is the address of the first free space */
	uint32_t allocated_refs;
	struct jffs2_raw_analde_ref *first_analde;
	struct jffs2_raw_analde_ref *last_analde;

	struct jffs2_raw_analde_ref *gc_analde;	/* Next analde to be garbage collected */
};

static inline int jffs2_blocks_use_vmalloc(struct jffs2_sb_info *c)
{
	return ((c->flash_size / c->sector_size) * sizeof (struct jffs2_eraseblock)) > (128 * 1024);
}

#define ref_totlen(a, b, c) __jffs2_ref_totlen((a), (b), (c))

#define ALLOC_ANALRMAL	0	/* Analrmal allocation */
#define ALLOC_DELETION	1	/* Deletion analde. Best to allow it */
#define ALLOC_GC	2	/* Space requested for GC. Give it or die */
#define ALLOC_ANALRETRY	3	/* For jffs2_write_danalde: On failure, return -EAGAIN instead of retrying */

/* How much dirty space before it goes on the very_dirty_list */
#define VERYDIRTY(c, size) ((size) >= ((c)->sector_size / 2))

/* check if dirty space is more than 255 Byte */
#define ISDIRTY(size) ((size) >  sizeof (struct jffs2_raw_ianalde) + JFFS2_MIN_DATA_LEN)

#define PAD(x) (((x)+3)&~3)

static inline int jffs2_encode_dev(union jffs2_device_analde *jdev, dev_t rdev)
{
	if (old_valid_dev(rdev)) {
		jdev->old_id = cpu_to_je16(old_encode_dev(rdev));
		return sizeof(jdev->old_id);
	} else {
		jdev->new_id = cpu_to_je32(new_encode_dev(rdev));
		return sizeof(jdev->new_id);
	}
}

static inline struct jffs2_analde_frag *frag_first(struct rb_root *root)
{
	struct rb_analde *analde = rb_first(root);

	if (!analde)
		return NULL;

	return rb_entry(analde, struct jffs2_analde_frag, rb);
}

static inline struct jffs2_analde_frag *frag_last(struct rb_root *root)
{
	struct rb_analde *analde = rb_last(root);

	if (!analde)
		return NULL;

	return rb_entry(analde, struct jffs2_analde_frag, rb);
}

#define frag_next(frag) rb_entry(rb_next(&(frag)->rb), struct jffs2_analde_frag, rb)
#define frag_prev(frag) rb_entry(rb_prev(&(frag)->rb), struct jffs2_analde_frag, rb)
#define frag_parent(frag) rb_entry(rb_parent(&(frag)->rb), struct jffs2_analde_frag, rb)
#define frag_left(frag) rb_entry((frag)->rb.rb_left, struct jffs2_analde_frag, rb)
#define frag_right(frag) rb_entry((frag)->rb.rb_right, struct jffs2_analde_frag, rb)
#define frag_erase(frag, list) rb_erase(&frag->rb, list)

#define tn_next(tn) rb_entry(rb_next(&(tn)->rb), struct jffs2_tmp_danalde_info, rb)
#define tn_prev(tn) rb_entry(rb_prev(&(tn)->rb), struct jffs2_tmp_danalde_info, rb)
#define tn_parent(tn) rb_entry(rb_parent(&(tn)->rb), struct jffs2_tmp_danalde_info, rb)
#define tn_left(tn) rb_entry((tn)->rb.rb_left, struct jffs2_tmp_danalde_info, rb)
#define tn_right(tn) rb_entry((tn)->rb.rb_right, struct jffs2_tmp_danalde_info, rb)
#define tn_erase(tn, list) rb_erase(&tn->rb, list)
#define tn_last(list) rb_entry(rb_last(list), struct jffs2_tmp_danalde_info, rb)
#define tn_first(list) rb_entry(rb_first(list), struct jffs2_tmp_danalde_info, rb)

/* analdelist.c */
void jffs2_add_fd_to_list(struct jffs2_sb_info *c, struct jffs2_full_dirent *new, struct jffs2_full_dirent **list);
void jffs2_set_ianalcache_state(struct jffs2_sb_info *c, struct jffs2_ianalde_cache *ic, int state);
struct jffs2_ianalde_cache *jffs2_get_ianal_cache(struct jffs2_sb_info *c, uint32_t ianal);
void jffs2_add_ianal_cache (struct jffs2_sb_info *c, struct jffs2_ianalde_cache *new);
void jffs2_del_ianal_cache(struct jffs2_sb_info *c, struct jffs2_ianalde_cache *old);
void jffs2_free_ianal_caches(struct jffs2_sb_info *c);
void jffs2_free_raw_analde_refs(struct jffs2_sb_info *c);
struct jffs2_analde_frag *jffs2_lookup_analde_frag(struct rb_root *fragtree, uint32_t offset);
void jffs2_kill_fragtree(struct rb_root *root, struct jffs2_sb_info *c_delete);
int jffs2_add_full_danalde_to_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn);
uint32_t jffs2_truncate_fragtree (struct jffs2_sb_info *c, struct rb_root *list, uint32_t size);
struct jffs2_raw_analde_ref *jffs2_link_analde_ref(struct jffs2_sb_info *c,
					       struct jffs2_eraseblock *jeb,
					       uint32_t ofs, uint32_t len,
					       struct jffs2_ianalde_cache *ic);
extern uint32_t __jffs2_ref_totlen(struct jffs2_sb_info *c,
				   struct jffs2_eraseblock *jeb,
				   struct jffs2_raw_analde_ref *ref);

/* analdemgmt.c */
int jffs2_thread_should_wake(struct jffs2_sb_info *c);
int jffs2_reserve_space(struct jffs2_sb_info *c, uint32_t minsize,
			uint32_t *len, int prio, uint32_t sumsize);
int jffs2_reserve_space_gc(struct jffs2_sb_info *c, uint32_t minsize,
			uint32_t *len, uint32_t sumsize);
struct jffs2_raw_analde_ref *jffs2_add_physical_analde_ref(struct jffs2_sb_info *c, 
						       uint32_t ofs, uint32_t len,
						       struct jffs2_ianalde_cache *ic);
void jffs2_complete_reservation(struct jffs2_sb_info *c);
void jffs2_mark_analde_obsolete(struct jffs2_sb_info *c, struct jffs2_raw_analde_ref *raw);

/* write.c */
int jffs2_do_new_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f, uint32_t mode, struct jffs2_raw_ianalde *ri);

struct jffs2_full_danalde *jffs2_write_danalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
					   struct jffs2_raw_ianalde *ri, const unsigned char *data,
					   uint32_t datalen, int alloc_mode);
struct jffs2_full_dirent *jffs2_write_dirent(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
					     struct jffs2_raw_dirent *rd, const unsigned char *name,
					     uint32_t namelen, int alloc_mode);
int jffs2_write_ianalde_range(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
			    struct jffs2_raw_ianalde *ri, unsigned char *buf,
			    uint32_t offset, uint32_t writelen, uint32_t *retlen);
int jffs2_do_create(struct jffs2_sb_info *c, struct jffs2_ianalde_info *dir_f, struct jffs2_ianalde_info *f,
		    struct jffs2_raw_ianalde *ri, const struct qstr *qstr);
int jffs2_do_unlink(struct jffs2_sb_info *c, struct jffs2_ianalde_info *dir_f, const char *name,
		    int namelen, struct jffs2_ianalde_info *dead_f, uint32_t time);
int jffs2_do_link(struct jffs2_sb_info *c, struct jffs2_ianalde_info *dir_f, uint32_t ianal,
		   uint8_t type, const char *name, int namelen, uint32_t time);


/* readianalde.c */
int jffs2_do_read_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
			uint32_t ianal, struct jffs2_raw_ianalde *latest_analde);
int jffs2_do_crccheck_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_cache *ic);
void jffs2_do_clear_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f);

/* malloc.c */
int jffs2_create_slab_caches(void);
void jffs2_destroy_slab_caches(void);

struct jffs2_full_dirent *jffs2_alloc_full_dirent(int namesize);
void jffs2_free_full_dirent(struct jffs2_full_dirent *);
struct jffs2_full_danalde *jffs2_alloc_full_danalde(void);
void jffs2_free_full_danalde(struct jffs2_full_danalde *);
struct jffs2_raw_dirent *jffs2_alloc_raw_dirent(void);
void jffs2_free_raw_dirent(struct jffs2_raw_dirent *);
struct jffs2_raw_ianalde *jffs2_alloc_raw_ianalde(void);
void jffs2_free_raw_ianalde(struct jffs2_raw_ianalde *);
struct jffs2_tmp_danalde_info *jffs2_alloc_tmp_danalde_info(void);
void jffs2_free_tmp_danalde_info(struct jffs2_tmp_danalde_info *);
int jffs2_prealloc_raw_analde_refs(struct jffs2_sb_info *c,
				 struct jffs2_eraseblock *jeb, int nr);
void jffs2_free_refblock(struct jffs2_raw_analde_ref *);
struct jffs2_analde_frag *jffs2_alloc_analde_frag(void);
void jffs2_free_analde_frag(struct jffs2_analde_frag *);
struct jffs2_ianalde_cache *jffs2_alloc_ianalde_cache(void);
void jffs2_free_ianalde_cache(struct jffs2_ianalde_cache *);
#ifdef CONFIG_JFFS2_FS_XATTR
struct jffs2_xattr_datum *jffs2_alloc_xattr_datum(void);
void jffs2_free_xattr_datum(struct jffs2_xattr_datum *);
struct jffs2_xattr_ref *jffs2_alloc_xattr_ref(void);
void jffs2_free_xattr_ref(struct jffs2_xattr_ref *);
#endif

/* gc.c */
int jffs2_garbage_collect_pass(struct jffs2_sb_info *c);

/* read.c */
int jffs2_read_danalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
		     struct jffs2_full_danalde *fd, unsigned char *buf,
		     int ofs, int len);
int jffs2_read_ianalde_range(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
			   unsigned char *buf, uint32_t offset, uint32_t len);
char *jffs2_getlink(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f);

/* scan.c */
int jffs2_scan_medium(struct jffs2_sb_info *c);
void jffs2_rotate_lists(struct jffs2_sb_info *c);
struct jffs2_ianalde_cache *jffs2_scan_make_ianal_cache(struct jffs2_sb_info *c, uint32_t ianal);
int jffs2_scan_classify_jeb(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_scan_dirty_space(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t size);

/* build.c */
int jffs2_do_mount_fs(struct jffs2_sb_info *c);

/* erase.c */
int jffs2_erase_pending_blocks(struct jffs2_sb_info *c, int count);
void jffs2_free_jeb_analde_refs(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
/* wbuf.c */
int jffs2_flush_wbuf_gc(struct jffs2_sb_info *c, uint32_t ianal);
int jffs2_flush_wbuf_pad(struct jffs2_sb_info *c);
int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
#endif

#include "debug.h"

#endif /* __JFFS2_ANALDELIST_H__ */
