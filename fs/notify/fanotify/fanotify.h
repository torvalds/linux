/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsnotify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/exportfs.h>

extern struct kmem_cache *fanotify_mark_cache;
extern struct kmem_cache *fanotify_fid_event_cachep;
extern struct kmem_cache *fanotify_path_event_cachep;
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
 * Common structure for fanotify events. Concrete structs are allocated in
 * fanotify_handle_event() and freed when the information is retrieved by
 * userspace. The type of event determines how it was allocated, how it will
 * be freed and which concrete struct it may be cast to.
 */
enum fanotify_event_type {
	FANOTIFY_EVENT_TYPE_FID, /* fixed length */
	FANOTIFY_EVENT_TYPE_FID_NAME, /* variable length */
	FANOTIFY_EVENT_TYPE_PATH,
	FANOTIFY_EVENT_TYPE_PATH_PERM,
};

struct fanotify_event {
	struct fsnotify_event fse;
	u32 mask;
	enum fanotify_event_type type;
	struct pid *pid;
};

struct fanotify_fid_event {
	struct fanotify_event fae;
	__kernel_fsid_t fsid;
	struct fanotify_fh object_fh;
};

static inline struct fanotify_fid_event *
FANOTIFY_FE(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_fid_event, fae);
}

struct fanotify_name_event {
	struct fanotify_event fae;
	__kernel_fsid_t fsid;
	struct fanotify_fh dir_fh;
	u8 name_len;
	char name[];
};

static inline struct fanotify_name_event *
FANOTIFY_NE(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_name_event, fae);
}

static inline __kernel_fsid_t *fanotify_event_fsid(struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_FID)
		return &FANOTIFY_FE(event)->fsid;
	else if (event->type == FANOTIFY_EVENT_TYPE_FID_NAME)
		return &FANOTIFY_NE(event)->fsid;
	else
		return NULL;
}

static inline struct fanotify_fh *fanotify_event_object_fh(
						struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_FID)
		return &FANOTIFY_FE(event)->object_fh;
	else
		return NULL;
}

static inline struct fanotify_fh *fanotify_event_dir_fh(
						struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_FID_NAME)
		return &FANOTIFY_NE(event)->dir_fh;
	else
		return NULL;
}

static inline int fanotify_event_object_fh_len(struct fanotify_event *event)
{
	struct fanotify_fh *fh = fanotify_event_object_fh(event);

	return fh ? fh->len : 0;
}

static inline bool fanotify_event_has_name(struct fanotify_event *event)
{
	return event->type == FANOTIFY_EVENT_TYPE_FID_NAME;
}

static inline int fanotify_event_name_len(struct fanotify_event *event)
{
	return fanotify_event_has_name(event) ?
		FANOTIFY_NE(event)->name_len : 0;
}

struct fanotify_path_event {
	struct fanotify_event fae;
	struct path path;
};

static inline struct fanotify_path_event *
FANOTIFY_PE(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_path_event, fae);
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
	struct path path;
	unsigned short response;	/* userspace answer to the event */
	unsigned short state;		/* state of the event */
	int fd;		/* fd we passed to userspace for this event */
};

static inline struct fanotify_perm_event *
FANOTIFY_PERM(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_perm_event, fae);
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

static inline bool fanotify_event_has_path(struct fanotify_event *event)
{
	return event->type == FANOTIFY_EVENT_TYPE_PATH ||
		event->type == FANOTIFY_EVENT_TYPE_PATH_PERM;
}

static inline struct path *fanotify_event_path(struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_PATH)
		return &FANOTIFY_PE(event)->path;
	else if (event->type == FANOTIFY_EVENT_TYPE_PATH_PERM)
		return &FANOTIFY_PERM(event)->path;
	else
		return NULL;
}

struct fanotify_event *fanotify_alloc_event(struct fsnotify_group *group,
					    struct inode *inode, u32 mask,
					    const void *data, int data_type,
					    const struct qstr *file_name,
					    __kernel_fsid_t *fsid);
