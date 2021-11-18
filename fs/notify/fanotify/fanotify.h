/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsnotify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/exportfs.h>
#include <linux/hashtable.h>

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
#define FANOTIFY_FH_HDR_LEN	offsetof(struct fanotify_fh, buf)

/* Fixed size struct for file handle */
struct fanotify_fh {
	u8 type;
	u8 len;
#define FANOTIFY_FH_FLAG_EXT_BUF 1
	u8 flags;
	u8 pad;
	unsigned char buf[];
} __aligned(4);

/* Variable size struct for dir file handle + child file handle + name */
struct fanotify_info {
	/* size of dir_fh/file_fh including fanotify_fh hdr size */
	u8 dir_fh_totlen;
	u8 file_fh_totlen;
	u8 name_len;
	u8 pad;
	unsigned char buf[];
	/*
	 * (struct fanotify_fh) dir_fh starts at buf[0]
	 * (optional) file_fh starts at buf[dir_fh_totlen]
	 * name starts at buf[dir_fh_totlen + file_fh_totlen]
	 */
} __aligned(4);

static inline bool fanotify_fh_has_ext_buf(struct fanotify_fh *fh)
{
	return (fh->flags & FANOTIFY_FH_FLAG_EXT_BUF);
}

static inline char **fanotify_fh_ext_buf_ptr(struct fanotify_fh *fh)
{
	BUILD_BUG_ON(FANOTIFY_FH_HDR_LEN % 4);
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

static inline int fanotify_info_dir_fh_len(struct fanotify_info *info)
{
	if (!info->dir_fh_totlen ||
	    WARN_ON_ONCE(info->dir_fh_totlen < FANOTIFY_FH_HDR_LEN))
		return 0;

	return info->dir_fh_totlen - FANOTIFY_FH_HDR_LEN;
}

static inline struct fanotify_fh *fanotify_info_dir_fh(struct fanotify_info *info)
{
	BUILD_BUG_ON(offsetof(struct fanotify_info, buf) % 4);

	return (struct fanotify_fh *)info->buf;
}

static inline int fanotify_info_file_fh_len(struct fanotify_info *info)
{
	if (!info->file_fh_totlen ||
	    WARN_ON_ONCE(info->file_fh_totlen < FANOTIFY_FH_HDR_LEN))
		return 0;

	return info->file_fh_totlen - FANOTIFY_FH_HDR_LEN;
}

static inline struct fanotify_fh *fanotify_info_file_fh(struct fanotify_info *info)
{
	return (struct fanotify_fh *)(info->buf + info->dir_fh_totlen);
}

static inline const char *fanotify_info_name(struct fanotify_info *info)
{
	return info->buf + info->dir_fh_totlen + info->file_fh_totlen;
}

static inline void fanotify_info_init(struct fanotify_info *info)
{
	info->dir_fh_totlen = 0;
	info->file_fh_totlen = 0;
	info->name_len = 0;
}

static inline unsigned int fanotify_info_len(struct fanotify_info *info)
{
	return info->dir_fh_totlen + info->file_fh_totlen + info->name_len;
}

static inline void fanotify_info_copy_name(struct fanotify_info *info,
					   const struct qstr *name)
{
	info->name_len = name->len;
	strcpy(info->buf + info->dir_fh_totlen + info->file_fh_totlen,
	       name->name);
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
	FANOTIFY_EVENT_TYPE_OVERFLOW, /* struct fanotify_event */
	FANOTIFY_EVENT_TYPE_FS_ERROR, /* struct fanotify_error_event */
	__FANOTIFY_EVENT_TYPE_NUM
};

#define FANOTIFY_EVENT_TYPE_BITS \
	(ilog2(__FANOTIFY_EVENT_TYPE_NUM - 1) + 1)
#define FANOTIFY_EVENT_HASH_BITS \
	(32 - FANOTIFY_EVENT_TYPE_BITS)

struct fanotify_event {
	struct fsnotify_event fse;
	struct hlist_node merge_list;	/* List for hashed merge */
	u32 mask;
	struct {
		unsigned int type : FANOTIFY_EVENT_TYPE_BITS;
		unsigned int hash : FANOTIFY_EVENT_HASH_BITS;
	};
	struct pid *pid;
};

static inline void fanotify_init_event(struct fanotify_event *event,
				       unsigned int hash, u32 mask)
{
	fsnotify_init_event(&event->fse);
	INIT_HLIST_NODE(&event->merge_list);
	event->hash = hash;
	event->mask = mask;
	event->pid = NULL;
}

#define FANOTIFY_INLINE_FH(name, size)					\
struct {								\
	struct fanotify_fh (name);					\
	/* Space for object_fh.buf[] - access with fanotify_fh_buf() */	\
	unsigned char _inline_fh_buf[(size)];				\
}

struct fanotify_fid_event {
	struct fanotify_event fae;
	__kernel_fsid_t fsid;

	FANOTIFY_INLINE_FH(object_fh, FANOTIFY_INLINE_FH_LEN);
};

static inline struct fanotify_fid_event *
FANOTIFY_FE(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_fid_event, fae);
}

struct fanotify_name_event {
	struct fanotify_event fae;
	__kernel_fsid_t fsid;
	struct fanotify_info info;
};

static inline struct fanotify_name_event *
FANOTIFY_NE(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_name_event, fae);
}

struct fanotify_error_event {
	struct fanotify_event fae;
	s32 error; /* Error reported by the Filesystem. */
	u32 err_count; /* Suppressed errors count */

	__kernel_fsid_t fsid; /* FSID this error refers to. */

	FANOTIFY_INLINE_FH(object_fh, MAX_HANDLE_SZ);
};

static inline struct fanotify_error_event *
FANOTIFY_EE(struct fanotify_event *event)
{
	return container_of(event, struct fanotify_error_event, fae);
}

static inline __kernel_fsid_t *fanotify_event_fsid(struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_FID)
		return &FANOTIFY_FE(event)->fsid;
	else if (event->type == FANOTIFY_EVENT_TYPE_FID_NAME)
		return &FANOTIFY_NE(event)->fsid;
	else if (event->type == FANOTIFY_EVENT_TYPE_FS_ERROR)
		return &FANOTIFY_EE(event)->fsid;
	else
		return NULL;
}

static inline struct fanotify_fh *fanotify_event_object_fh(
						struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_FID)
		return &FANOTIFY_FE(event)->object_fh;
	else if (event->type == FANOTIFY_EVENT_TYPE_FID_NAME)
		return fanotify_info_file_fh(&FANOTIFY_NE(event)->info);
	else if (event->type == FANOTIFY_EVENT_TYPE_FS_ERROR)
		return &FANOTIFY_EE(event)->object_fh;
	else
		return NULL;
}

static inline struct fanotify_info *fanotify_event_info(
						struct fanotify_event *event)
{
	if (event->type == FANOTIFY_EVENT_TYPE_FID_NAME)
		return &FANOTIFY_NE(event)->info;
	else
		return NULL;
}

static inline int fanotify_event_object_fh_len(struct fanotify_event *event)
{
	struct fanotify_info *info = fanotify_event_info(event);
	struct fanotify_fh *fh = fanotify_event_object_fh(event);

	if (info)
		return info->file_fh_totlen ? fh->len : 0;
	else
		return fh ? fh->len : 0;
}

static inline int fanotify_event_dir_fh_len(struct fanotify_event *event)
{
	struct fanotify_info *info = fanotify_event_info(event);

	return info ? fanotify_info_dir_fh_len(info) : 0;
}

static inline bool fanotify_event_has_object_fh(struct fanotify_event *event)
{
	/* For error events, even zeroed fh are reported. */
	if (event->type == FANOTIFY_EVENT_TYPE_FS_ERROR)
		return true;
	return fanotify_event_object_fh_len(event) > 0;
}

static inline bool fanotify_event_has_dir_fh(struct fanotify_event *event)
{
	return fanotify_event_dir_fh_len(event) > 0;
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

static inline bool fanotify_is_error_event(u32 mask)
{
	return mask & FAN_FS_ERROR;
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

/*
 * Use 128 size hash table to speed up events merge.
 */
#define FANOTIFY_HTABLE_BITS	(7)
#define FANOTIFY_HTABLE_SIZE	(1 << FANOTIFY_HTABLE_BITS)
#define FANOTIFY_HTABLE_MASK	(FANOTIFY_HTABLE_SIZE - 1)

/*
 * Permission events and overflow event do not get merged - don't hash them.
 */
static inline bool fanotify_is_hashed_event(u32 mask)
{
	return !(fanotify_is_perm_event(mask) ||
		 fsnotify_is_overflow_event(mask));
}

static inline unsigned int fanotify_event_hash_bucket(
						struct fsnotify_group *group,
						struct fanotify_event *event)
{
	return event->hash & FANOTIFY_HTABLE_MASK;
}
