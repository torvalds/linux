/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsnotify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/exportfs.h>

extern struct kmem_cache *fanotify_mark_cache;
extern struct kmem_cache *fanotify_event_cachep;
extern struct kmem_cache *fanotify_perm_event_cachep;

/* Possible states of the permission event */
enum {
	FAN_EVENT_INIT,
	FAN_EVENT_REPORTED,
	FAN_EVENT_ANSWERED,
	FAN_EVENT_CANCELED,
};

/*
 * 3 dwords are sufficient for most local fs (64bit ino, 32bit generation).
 * For 32bit arch, fid increases the size of fanotify_event by 12 bytes and
 * fh_* fields increase the size of fanotify_event by another 4 bytes.
 * For 64bit arch, fid increases the size of fanotify_fid by 8 bytes and
 * fh_* fields are packed in a hole after mask.
 */
#if BITS_PER_LONG == 32
#define FANOTIFY_INLINE_FH_LEN	(3 << 2)
#else
#define FANOTIFY_INLINE_FH_LEN	(4 << 2)
#endif

struct fanotify_fid {
	__kernel_fsid_t fsid;
	union {
		unsigned char fh[FANOTIFY_INLINE_FH_LEN];
		unsigned char *ext_fh;
	};
};

static inline void *fanotify_fid_fh(struct fanotify_fid *fid,
				    unsigned int fh_len)
{
	return fh_len <= FANOTIFY_INLINE_FH_LEN ? fid->fh : fid->ext_fh;
}

static inline bool fanotify_fid_equal(struct fanotify_fid *fid1,
				      struct fanotify_fid *fid2,
				      unsigned int fh_len)
{
	return fid1->fsid.val[0] == fid2->fsid.val[0] &&
		fid1->fsid.val[1] == fid2->fsid.val[1] &&
		!memcmp(fanotify_fid_fh(fid1, fh_len),
			fanotify_fid_fh(fid2, fh_len), fh_len);
}

/*
 * Structure for normal fanotify events. It gets allocated in
 * fanotify_handle_event() and freed when the information is retrieved by
 * userspace
 */
struct fanotify_event {
	struct fsnotify_event fse;
	u32 mask;
	/*
	 * Those fields are outside fanotify_fid to pack fanotify_event nicely
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
		 * With FAN_REPORT_FID, we do not hold any reference on the
		 * victim object. Instead we store its NFS file handle and its
		 * filesystem's fsid as a unique identifier.
		 */
		struct fanotify_fid fid;
	};
	struct pid *pid;
};

static inline bool fanotify_event_has_path(struct fanotify_event *event)
{
	return event->fh_type == FILEID_ROOT;
}

static inline bool fanotify_event_has_fid(struct fanotify_event *event)
{
	return event->fh_type != FILEID_ROOT &&
		event->fh_type != FILEID_INVALID;
}

static inline bool fanotify_event_has_ext_fh(struct fanotify_event *event)
{
	return fanotify_event_has_fid(event) &&
		event->fh_len > FANOTIFY_INLINE_FH_LEN;
}

static inline void *fanotify_event_fh(struct fanotify_event *event)
{
	return fanotify_fid_fh(&event->fid, event->fh_len);
}

/*
 * Structure for permission fanotify events. It gets allocated and freed in
 * fanotify_handle_event() since we wait there for user response. When the
 * information is retrieved by userspace the structure is moved from
 * group->notification_list to group->fanotify_data.access_list to wait for
 * user response.
 */
struct fanotify_perm_event {
	struct fanotify_event fae;
	unsigned short response;	/* userspace answer to the event */
	unsigned short state;		/* state of the event */
	int fd;		/* fd we passed to userspace for this event */
};

static inline struct fanotify_perm_event *
FANOTIFY_PE(struct fsnotify_event *fse)
{
	return container_of(fse, struct fanotify_perm_event, fae.fse);
}

static inline bool fanotify_is_perm_event(u32 mask)
{
	return IS_ENABLED(CONFIG_FANOTIFY_ACCESS_PERMISSIONS) &&
		mask & FANOTIFY_PERM_EVENTS;
}

static inline struct fanotify_event *FANOTIFY_E(struct fsnotify_event *fse)
{
	return container_of(fse, struct fanotify_event, fse);
}

struct fanotify_event *fanotify_alloc_event(struct fsnotify_group *group,
					    struct inode *inode, u32 mask,
					    const void *data, int data_type,
					    __kernel_fsid_t *fsid);
