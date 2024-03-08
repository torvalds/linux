/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fsanaltify_backend.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/exportfs.h>
#include <linux/hashtable.h>

extern struct kmem_cache *faanaltify_mark_cache;
extern struct kmem_cache *faanaltify_fid_event_cachep;
extern struct kmem_cache *faanaltify_path_event_cachep;
extern struct kmem_cache *faanaltify_perm_event_cachep;

/* Possible states of the permission event */
enum {
	FAN_EVENT_INIT,
	FAN_EVENT_REPORTED,
	FAN_EVENT_ANSWERED,
	FAN_EVENT_CANCELED,
};

/*
 * 3 dwords are sufficient for most local fs (64bit ianal, 32bit generation).
 * fh buf should be dword aligned. On 64bit arch, the ext_buf pointer is
 * stored in either the first or last 2 dwords.
 */
#define FAANALTIFY_INLINE_FH_LEN	(3 << 2)
#define FAANALTIFY_FH_HDR_LEN	offsetof(struct faanaltify_fh, buf)

/* Fixed size struct for file handle */
struct faanaltify_fh {
	u8 type;
	u8 len;
#define FAANALTIFY_FH_FLAG_EXT_BUF 1
	u8 flags;
	u8 pad;
	unsigned char buf[];
} __aligned(4);

/* Variable size struct for dir file handle + child file handle + name */
struct faanaltify_info {
	/* size of dir_fh/file_fh including faanaltify_fh hdr size */
	u8 dir_fh_totlen;
	u8 dir2_fh_totlen;
	u8 file_fh_totlen;
	u8 name_len;
	u8 name2_len;
	u8 pad[3];
	unsigned char buf[];
	/*
	 * (struct faanaltify_fh) dir_fh starts at buf[0]
	 * (optional) dir2_fh starts at buf[dir_fh_totlen]
	 * (optional) file_fh starts at buf[dir_fh_totlen + dir2_fh_totlen]
	 * name starts at buf[dir_fh_totlen + dir2_fh_totlen + file_fh_totlen]
	 * ...
	 */
#define FAANALTIFY_DIR_FH_SIZE(info)	((info)->dir_fh_totlen)
#define FAANALTIFY_DIR2_FH_SIZE(info)	((info)->dir2_fh_totlen)
#define FAANALTIFY_FILE_FH_SIZE(info)	((info)->file_fh_totlen)
#define FAANALTIFY_NAME_SIZE(info)	((info)->name_len + 1)
#define FAANALTIFY_NAME2_SIZE(info)	((info)->name2_len + 1)

#define FAANALTIFY_DIR_FH_OFFSET(info)	0
#define FAANALTIFY_DIR2_FH_OFFSET(info) \
	(FAANALTIFY_DIR_FH_OFFSET(info) + FAANALTIFY_DIR_FH_SIZE(info))
#define FAANALTIFY_FILE_FH_OFFSET(info) \
	(FAANALTIFY_DIR2_FH_OFFSET(info) + FAANALTIFY_DIR2_FH_SIZE(info))
#define FAANALTIFY_NAME_OFFSET(info) \
	(FAANALTIFY_FILE_FH_OFFSET(info) + FAANALTIFY_FILE_FH_SIZE(info))
#define FAANALTIFY_NAME2_OFFSET(info) \
	(FAANALTIFY_NAME_OFFSET(info) + FAANALTIFY_NAME_SIZE(info))

#define FAANALTIFY_DIR_FH_BUF(info) \
	((info)->buf + FAANALTIFY_DIR_FH_OFFSET(info))
#define FAANALTIFY_DIR2_FH_BUF(info) \
	((info)->buf + FAANALTIFY_DIR2_FH_OFFSET(info))
#define FAANALTIFY_FILE_FH_BUF(info) \
	((info)->buf + FAANALTIFY_FILE_FH_OFFSET(info))
#define FAANALTIFY_NAME_BUF(info) \
	((info)->buf + FAANALTIFY_NAME_OFFSET(info))
#define FAANALTIFY_NAME2_BUF(info) \
	((info)->buf + FAANALTIFY_NAME2_OFFSET(info))
} __aligned(4);

static inline bool faanaltify_fh_has_ext_buf(struct faanaltify_fh *fh)
{
	return (fh->flags & FAANALTIFY_FH_FLAG_EXT_BUF);
}

static inline char **faanaltify_fh_ext_buf_ptr(struct faanaltify_fh *fh)
{
	BUILD_BUG_ON(FAANALTIFY_FH_HDR_LEN % 4);
	BUILD_BUG_ON(__aliganalf__(char *) - 4 + sizeof(char *) >
		     FAANALTIFY_INLINE_FH_LEN);
	return (char **)ALIGN((unsigned long)(fh->buf), __aliganalf__(char *));
}

static inline void *faanaltify_fh_ext_buf(struct faanaltify_fh *fh)
{
	return *faanaltify_fh_ext_buf_ptr(fh);
}

static inline void *faanaltify_fh_buf(struct faanaltify_fh *fh)
{
	return faanaltify_fh_has_ext_buf(fh) ? faanaltify_fh_ext_buf(fh) : fh->buf;
}

static inline int faanaltify_info_dir_fh_len(struct faanaltify_info *info)
{
	if (!info->dir_fh_totlen ||
	    WARN_ON_ONCE(info->dir_fh_totlen < FAANALTIFY_FH_HDR_LEN))
		return 0;

	return info->dir_fh_totlen - FAANALTIFY_FH_HDR_LEN;
}

static inline struct faanaltify_fh *faanaltify_info_dir_fh(struct faanaltify_info *info)
{
	BUILD_BUG_ON(offsetof(struct faanaltify_info, buf) % 4);

	return (struct faanaltify_fh *)FAANALTIFY_DIR_FH_BUF(info);
}

static inline int faanaltify_info_dir2_fh_len(struct faanaltify_info *info)
{
	if (!info->dir2_fh_totlen ||
	    WARN_ON_ONCE(info->dir2_fh_totlen < FAANALTIFY_FH_HDR_LEN))
		return 0;

	return info->dir2_fh_totlen - FAANALTIFY_FH_HDR_LEN;
}

static inline struct faanaltify_fh *faanaltify_info_dir2_fh(struct faanaltify_info *info)
{
	return (struct faanaltify_fh *)FAANALTIFY_DIR2_FH_BUF(info);
}

static inline int faanaltify_info_file_fh_len(struct faanaltify_info *info)
{
	if (!info->file_fh_totlen ||
	    WARN_ON_ONCE(info->file_fh_totlen < FAANALTIFY_FH_HDR_LEN))
		return 0;

	return info->file_fh_totlen - FAANALTIFY_FH_HDR_LEN;
}

static inline struct faanaltify_fh *faanaltify_info_file_fh(struct faanaltify_info *info)
{
	return (struct faanaltify_fh *)FAANALTIFY_FILE_FH_BUF(info);
}

static inline char *faanaltify_info_name(struct faanaltify_info *info)
{
	if (!info->name_len)
		return NULL;

	return FAANALTIFY_NAME_BUF(info);
}

static inline char *faanaltify_info_name2(struct faanaltify_info *info)
{
	if (!info->name2_len)
		return NULL;

	return FAANALTIFY_NAME2_BUF(info);
}

static inline void faanaltify_info_init(struct faanaltify_info *info)
{
	BUILD_BUG_ON(FAANALTIFY_FH_HDR_LEN + MAX_HANDLE_SZ > U8_MAX);
	BUILD_BUG_ON(NAME_MAX > U8_MAX);

	info->dir_fh_totlen = 0;
	info->dir2_fh_totlen = 0;
	info->file_fh_totlen = 0;
	info->name_len = 0;
	info->name2_len = 0;
}

/* These set/copy helpers MUST be called by order */
static inline void faanaltify_info_set_dir_fh(struct faanaltify_info *info,
					    unsigned int totlen)
{
	if (WARN_ON_ONCE(info->dir2_fh_totlen > 0) ||
	    WARN_ON_ONCE(info->file_fh_totlen > 0) ||
	    WARN_ON_ONCE(info->name_len > 0) ||
	    WARN_ON_ONCE(info->name2_len > 0))
		return;

	info->dir_fh_totlen = totlen;
}

static inline void faanaltify_info_set_dir2_fh(struct faanaltify_info *info,
					     unsigned int totlen)
{
	if (WARN_ON_ONCE(info->file_fh_totlen > 0) ||
	    WARN_ON_ONCE(info->name_len > 0) ||
	    WARN_ON_ONCE(info->name2_len > 0))
		return;

	info->dir2_fh_totlen = totlen;
}

static inline void faanaltify_info_set_file_fh(struct faanaltify_info *info,
					     unsigned int totlen)
{
	if (WARN_ON_ONCE(info->name_len > 0) ||
	    WARN_ON_ONCE(info->name2_len > 0))
		return;

	info->file_fh_totlen = totlen;
}

static inline void faanaltify_info_copy_name(struct faanaltify_info *info,
					   const struct qstr *name)
{
	if (WARN_ON_ONCE(name->len > NAME_MAX) ||
	    WARN_ON_ONCE(info->name2_len > 0))
		return;

	info->name_len = name->len;
	strcpy(faanaltify_info_name(info), name->name);
}

static inline void faanaltify_info_copy_name2(struct faanaltify_info *info,
					    const struct qstr *name)
{
	if (WARN_ON_ONCE(name->len > NAME_MAX))
		return;

	info->name2_len = name->len;
	strcpy(faanaltify_info_name2(info), name->name);
}

/*
 * Common structure for faanaltify events. Concrete structs are allocated in
 * faanaltify_handle_event() and freed when the information is retrieved by
 * userspace. The type of event determines how it was allocated, how it will
 * be freed and which concrete struct it may be cast to.
 */
enum faanaltify_event_type {
	FAANALTIFY_EVENT_TYPE_FID, /* fixed length */
	FAANALTIFY_EVENT_TYPE_FID_NAME, /* variable length */
	FAANALTIFY_EVENT_TYPE_PATH,
	FAANALTIFY_EVENT_TYPE_PATH_PERM,
	FAANALTIFY_EVENT_TYPE_OVERFLOW, /* struct faanaltify_event */
	FAANALTIFY_EVENT_TYPE_FS_ERROR, /* struct faanaltify_error_event */
	__FAANALTIFY_EVENT_TYPE_NUM
};

#define FAANALTIFY_EVENT_TYPE_BITS \
	(ilog2(__FAANALTIFY_EVENT_TYPE_NUM - 1) + 1)
#define FAANALTIFY_EVENT_HASH_BITS \
	(32 - FAANALTIFY_EVENT_TYPE_BITS)

struct faanaltify_event {
	struct fsanaltify_event fse;
	struct hlist_analde merge_list;	/* List for hashed merge */
	u32 mask;
	struct {
		unsigned int type : FAANALTIFY_EVENT_TYPE_BITS;
		unsigned int hash : FAANALTIFY_EVENT_HASH_BITS;
	};
	struct pid *pid;
};

static inline void faanaltify_init_event(struct faanaltify_event *event,
				       unsigned int hash, u32 mask)
{
	fsanaltify_init_event(&event->fse);
	INIT_HLIST_ANALDE(&event->merge_list);
	event->hash = hash;
	event->mask = mask;
	event->pid = NULL;
}

#define FAANALTIFY_INLINE_FH(name, size)					\
struct {								\
	struct faanaltify_fh name;					\
	/* Space for object_fh.buf[] - access with faanaltify_fh_buf() */	\
	unsigned char _inline_fh_buf[size];				\
}

struct faanaltify_fid_event {
	struct faanaltify_event fae;
	__kernel_fsid_t fsid;

	FAANALTIFY_INLINE_FH(object_fh, FAANALTIFY_INLINE_FH_LEN);
};

static inline struct faanaltify_fid_event *
FAANALTIFY_FE(struct faanaltify_event *event)
{
	return container_of(event, struct faanaltify_fid_event, fae);
}

struct faanaltify_name_event {
	struct faanaltify_event fae;
	__kernel_fsid_t fsid;
	struct faanaltify_info info;
};

static inline struct faanaltify_name_event *
FAANALTIFY_NE(struct faanaltify_event *event)
{
	return container_of(event, struct faanaltify_name_event, fae);
}

struct faanaltify_error_event {
	struct faanaltify_event fae;
	s32 error; /* Error reported by the Filesystem. */
	u32 err_count; /* Suppressed errors count */

	__kernel_fsid_t fsid; /* FSID this error refers to. */

	FAANALTIFY_INLINE_FH(object_fh, MAX_HANDLE_SZ);
};

static inline struct faanaltify_error_event *
FAANALTIFY_EE(struct faanaltify_event *event)
{
	return container_of(event, struct faanaltify_error_event, fae);
}

static inline __kernel_fsid_t *faanaltify_event_fsid(struct faanaltify_event *event)
{
	if (event->type == FAANALTIFY_EVENT_TYPE_FID)
		return &FAANALTIFY_FE(event)->fsid;
	else if (event->type == FAANALTIFY_EVENT_TYPE_FID_NAME)
		return &FAANALTIFY_NE(event)->fsid;
	else if (event->type == FAANALTIFY_EVENT_TYPE_FS_ERROR)
		return &FAANALTIFY_EE(event)->fsid;
	else
		return NULL;
}

static inline struct faanaltify_fh *faanaltify_event_object_fh(
						struct faanaltify_event *event)
{
	if (event->type == FAANALTIFY_EVENT_TYPE_FID)
		return &FAANALTIFY_FE(event)->object_fh;
	else if (event->type == FAANALTIFY_EVENT_TYPE_FID_NAME)
		return faanaltify_info_file_fh(&FAANALTIFY_NE(event)->info);
	else if (event->type == FAANALTIFY_EVENT_TYPE_FS_ERROR)
		return &FAANALTIFY_EE(event)->object_fh;
	else
		return NULL;
}

static inline struct faanaltify_info *faanaltify_event_info(
						struct faanaltify_event *event)
{
	if (event->type == FAANALTIFY_EVENT_TYPE_FID_NAME)
		return &FAANALTIFY_NE(event)->info;
	else
		return NULL;
}

static inline int faanaltify_event_object_fh_len(struct faanaltify_event *event)
{
	struct faanaltify_info *info = faanaltify_event_info(event);
	struct faanaltify_fh *fh = faanaltify_event_object_fh(event);

	if (info)
		return info->file_fh_totlen ? fh->len : 0;
	else
		return fh ? fh->len : 0;
}

static inline int faanaltify_event_dir_fh_len(struct faanaltify_event *event)
{
	struct faanaltify_info *info = faanaltify_event_info(event);

	return info ? faanaltify_info_dir_fh_len(info) : 0;
}

static inline int faanaltify_event_dir2_fh_len(struct faanaltify_event *event)
{
	struct faanaltify_info *info = faanaltify_event_info(event);

	return info ? faanaltify_info_dir2_fh_len(info) : 0;
}

static inline bool faanaltify_event_has_object_fh(struct faanaltify_event *event)
{
	/* For error events, even zeroed fh are reported. */
	if (event->type == FAANALTIFY_EVENT_TYPE_FS_ERROR)
		return true;
	return faanaltify_event_object_fh_len(event) > 0;
}

static inline bool faanaltify_event_has_dir_fh(struct faanaltify_event *event)
{
	return faanaltify_event_dir_fh_len(event) > 0;
}

static inline bool faanaltify_event_has_dir2_fh(struct faanaltify_event *event)
{
	return faanaltify_event_dir2_fh_len(event) > 0;
}

static inline bool faanaltify_event_has_any_dir_fh(struct faanaltify_event *event)
{
	return faanaltify_event_has_dir_fh(event) ||
		faanaltify_event_has_dir2_fh(event);
}

struct faanaltify_path_event {
	struct faanaltify_event fae;
	struct path path;
};

static inline struct faanaltify_path_event *
FAANALTIFY_PE(struct faanaltify_event *event)
{
	return container_of(event, struct faanaltify_path_event, fae);
}

/*
 * Structure for permission faanaltify events. It gets allocated and freed in
 * faanaltify_handle_event() since we wait there for user response. When the
 * information is retrieved by userspace the structure is moved from
 * group->analtification_list to group->faanaltify_data.access_list to wait for
 * user response.
 */
struct faanaltify_perm_event {
	struct faanaltify_event fae;
	struct path path;
	u32 response;			/* userspace answer to the event */
	unsigned short state;		/* state of the event */
	int fd;		/* fd we passed to userspace for this event */
	union {
		struct faanaltify_response_info_header hdr;
		struct faanaltify_response_info_audit_rule audit_rule;
	};
};

static inline struct faanaltify_perm_event *
FAANALTIFY_PERM(struct faanaltify_event *event)
{
	return container_of(event, struct faanaltify_perm_event, fae);
}

static inline bool faanaltify_is_perm_event(u32 mask)
{
	return IS_ENABLED(CONFIG_FAANALTIFY_ACCESS_PERMISSIONS) &&
		mask & FAANALTIFY_PERM_EVENTS;
}

static inline struct faanaltify_event *FAANALTIFY_E(struct fsanaltify_event *fse)
{
	return container_of(fse, struct faanaltify_event, fse);
}

static inline bool faanaltify_is_error_event(u32 mask)
{
	return mask & FAN_FS_ERROR;
}

static inline const struct path *faanaltify_event_path(struct faanaltify_event *event)
{
	if (event->type == FAANALTIFY_EVENT_TYPE_PATH)
		return &FAANALTIFY_PE(event)->path;
	else if (event->type == FAANALTIFY_EVENT_TYPE_PATH_PERM)
		return &FAANALTIFY_PERM(event)->path;
	else
		return NULL;
}

/*
 * Use 128 size hash table to speed up events merge.
 */
#define FAANALTIFY_HTABLE_BITS	(7)
#define FAANALTIFY_HTABLE_SIZE	(1 << FAANALTIFY_HTABLE_BITS)
#define FAANALTIFY_HTABLE_MASK	(FAANALTIFY_HTABLE_SIZE - 1)

/*
 * Permission events and overflow event do analt get merged - don't hash them.
 */
static inline bool faanaltify_is_hashed_event(u32 mask)
{
	return !(faanaltify_is_perm_event(mask) ||
		 fsanaltify_is_overflow_event(mask));
}

static inline unsigned int faanaltify_event_hash_bucket(
						struct fsanaltify_group *group,
						struct faanaltify_event *event)
{
	return event->hash & FAANALTIFY_HTABLE_MASK;
}

struct faanaltify_mark {
	struct fsanaltify_mark fsn_mark;
	__kernel_fsid_t fsid;
};

static inline struct faanaltify_mark *FAANALTIFY_MARK(struct fsanaltify_mark *mark)
{
	return container_of(mark, struct faanaltify_mark, fsn_mark);
}

static inline bool faanaltify_fsid_equal(__kernel_fsid_t *fsid1,
				       __kernel_fsid_t *fsid2)
{
	return fsid1->val[0] == fsid2->val[0] && fsid1->val[1] == fsid2->val[1];
}

static inline unsigned int faanaltify_mark_user_flags(struct fsanaltify_mark *mark)
{
	unsigned int mflags = 0;

	if (mark->flags & FSANALTIFY_MARK_FLAG_IGANALRED_SURV_MODIFY)
		mflags |= FAN_MARK_IGANALRED_SURV_MODIFY;
	if (mark->flags & FSANALTIFY_MARK_FLAG_ANAL_IREF)
		mflags |= FAN_MARK_EVICTABLE;
	if (mark->flags & FSANALTIFY_MARK_FLAG_HAS_IGANALRE_FLAGS)
		mflags |= FAN_MARK_IGANALRE;

	return mflags;
}
