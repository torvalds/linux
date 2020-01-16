/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsyestify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/exportfs.h>

extern struct kmem_cache *fayestify_mark_cache;
extern struct kmem_cache *fayestify_event_cachep;
extern struct kmem_cache *fayestify_perm_event_cachep;

/* Possible states of the permission event */
enum {
	FAN_EVENT_INIT,
	FAN_EVENT_REPORTED,
	FAN_EVENT_ANSWERED,
	FAN_EVENT_CANCELED,
};

/*
 * 3 dwords are sufficient for most local fs (64bit iyes, 32bit generation).
 * For 32bit arch, fid increases the size of fayestify_event by 12 bytes and
 * fh_* fields increase the size of fayestify_event by ayesther 4 bytes.
 * For 64bit arch, fid increases the size of fayestify_fid by 8 bytes and
 * fh_* fields are packed in a hole after mask.
 */
#if BITS_PER_LONG == 32
#define FANOTIFY_INLINE_FH_LEN	(3 << 2)
#else
#define FANOTIFY_INLINE_FH_LEN	(4 << 2)
#endif

struct fayestify_fid {
	__kernel_fsid_t fsid;
	union {
		unsigned char fh[FANOTIFY_INLINE_FH_LEN];
		unsigned char *ext_fh;
	};
};

static inline void *fayestify_fid_fh(struct fayestify_fid *fid,
				    unsigned int fh_len)
{
	return fh_len <= FANOTIFY_INLINE_FH_LEN ? fid->fh : fid->ext_fh;
}

static inline bool fayestify_fid_equal(struct fayestify_fid *fid1,
				      struct fayestify_fid *fid2,
				      unsigned int fh_len)
{
	return fid1->fsid.val[0] == fid2->fsid.val[0] &&
		fid1->fsid.val[1] == fid2->fsid.val[1] &&
		!memcmp(fayestify_fid_fh(fid1, fh_len),
			fayestify_fid_fh(fid2, fh_len), fh_len);
}

/*
 * Structure for yesrmal fayestify events. It gets allocated in
 * fayestify_handle_event() and freed when the information is retrieved by
 * userspace
 */
struct fayestify_event {
	struct fsyestify_event fse;
	u32 mask;
	/*
	 * Those fields are outside fayestify_fid to pack fayestify_event nicely
	 * on 64bit arch and to use fh_type as an indication of whether path
	 * or fid are used in the union:
	 * FILEID_ROOT (0) for path, > 0 for fid, FILEID_INVALID for neither.
	 */
	u8 fh_type;
	u8 fh_len;
	u16 pad;
	union {
		/*
		 * We hold ref to this path so it may be dereferenced at any
		 * point during this object's lifetime
		 */
		struct path path;
		/*
		 * With FAN_REPORT_FID, we do yest hold any reference on the
		 * victim object. Instead we store its NFS file handle and its
		 * filesystem's fsid as a unique identifier.
		 */
		struct fayestify_fid fid;
	};
	struct pid *pid;
};

static inline bool fayestify_event_has_path(struct fayestify_event *event)
{
	return event->fh_type == FILEID_ROOT;
}

static inline bool fayestify_event_has_fid(struct fayestify_event *event)
{
	return event->fh_type != FILEID_ROOT &&
		event->fh_type != FILEID_INVALID;
}

static inline bool fayestify_event_has_ext_fh(struct fayestify_event *event)
{
	return fayestify_event_has_fid(event) &&
		event->fh_len > FANOTIFY_INLINE_FH_LEN;
}

static inline void *fayestify_event_fh(struct fayestify_event *event)
{
	return fayestify_fid_fh(&event->fid, event->fh_len);
}

/*
 * Structure for permission fayestify events. It gets allocated and freed in
 * fayestify_handle_event() since we wait there for user response. When the
 * information is retrieved by userspace the structure is moved from
 * group->yestification_list to group->fayestify_data.access_list to wait for
 * user response.
 */
struct fayestify_perm_event {
	struct fayestify_event fae;
	unsigned short response;	/* userspace answer to the event */
	unsigned short state;		/* state of the event */
	int fd;		/* fd we passed to userspace for this event */
};

static inline struct fayestify_perm_event *
FANOTIFY_PE(struct fsyestify_event *fse)
{
	return container_of(fse, struct fayestify_perm_event, fae.fse);
}

static inline bool fayestify_is_perm_event(u32 mask)
{
	return IS_ENABLED(CONFIG_FANOTIFY_ACCESS_PERMISSIONS) &&
		mask & FANOTIFY_PERM_EVENTS;
}

static inline struct fayestify_event *FANOTIFY_E(struct fsyestify_event *fse)
{
	return container_of(fse, struct fayestify_event, fse);
}

struct fayestify_event *fayestify_alloc_event(struct fsyestify_group *group,
					    struct iyesde *iyesde, u32 mask,
					    const void *data, int data_type,
					    __kernel_fsid_t *fsid);
