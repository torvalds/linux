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
 * fh buf should be dword aligned. On 64bit arch, the ext_buf pointer is
 * stored in either the first or last 2 dwords.
 */
#define FANOTIFY_INLINE_FH_LEN	(3 << 2)

struct fanotify_fh {
	unsigned char buf[FANOTIFY_INLINE_FH_LEN];
	u8 type;
	u8 len;
} __aligned(4);

static inline bool fanotify_fh_has_ext_buf(struct fanotify_fh *fh)
{
	return fh->len > FANOTIFY_INLINE_FH_LEN;
}

static inline char **fanotify_fh_ext_buf_ptr(struct fanotify_fh *fh)
{
	BUILD_BUG_ON(__alignof__(char *) - 4 + sizeof(char *) >
		     FANOTIFY_INLINE_FH_LEN);
	return (char **)ALIGN((unsigned long)(fh->buf), __alignof__(char *));
}

static inline void *fanotify_fh_ext_buf(struct fanotify_fh *fh)
{
	return *fanotify_fh_ext_buf_ptr(fh);
}

static inline void *fanotify_fh_buf(struct fanotify_fh *fh)
{
	return fanotify_fh_has_ext_buf(fh) ? fanotify_fh_ext_buf(fh) : fh->buf;
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
	 * With FAN_REPORT_FID, we do not hold any reference on the
	 * victim object. Instead we store its NFS file handle and its
	 * filesystem's fsid as a unique identifier.
	 */
	__kernel_fsid_t fsid;
	struct fanotify_fh fh;
	/*
	 * We hold ref to this path so it may be dereferenced at any
	 * point during this object's lifetime
	 */
	struct path path;
	struct pid *pid;
};

static inline bool fanotify_event_has_path(struct fanotify_event *event)
{
	return event->fh.type == FILEID_ROOT;
}

static inline bool fanotify_event_has_fid(struct fanotify_event *event)
{
	return event->fh.type != FILEID_ROOT &&
	       event->fh.type != FILEID_INVALID;
}

static inline __kernel_fsid_t *fanotify_event_fsid(struct fanotify_event *event)
{
	if (fanotify_event_has_fid(event))
		return &event->fsid;
	else
		return NULL;
}

static inline struct fanotify_fh *fanotify_event_object_fh(
						struct fanotify_event *event)
{
	if (fanotify_event_has_fid(event))
		return &event->fh;
	else
		return NULL;
}

static inline int fanotify_event_object_fh_len(struct fanotify_event *event)
{
	struct fanotify_fh *fh = fanotify_event_object_fh(event);

	return fh ? fh->len : 0;
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

static inline struct path *fanotify_event_path(struct fanotify_event *event)
{
	if (fanotify_event_has_path(event))
		return &event->path;
	else
		return NULL;
}

struct fanotify_event *fanotify_alloc_event(struct fsnotify_group *group,
					    struct inode *inode, u32 mask,
					    const void *data, int data_type,
					    __kernel_fsid_t *fsid);
