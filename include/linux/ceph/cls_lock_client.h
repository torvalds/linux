/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CEPH_CLS_LOCK_CLIENT_H
#define _LINUX_CEPH_CLS_LOCK_CLIENT_H

#include <linux/ceph/osd_client.h>

enum ceph_cls_lock_type {
	CEPH_CLS_LOCK_NONE = 0,
	CEPH_CLS_LOCK_EXCLUSIVE = 1,
	CEPH_CLS_LOCK_SHARED = 2,
};

struct ceph_locker_id {
	struct ceph_entity_name name;	/* locker's client name */
	char *cookie;			/* locker's cookie */
};

struct ceph_locker_info {
	struct ceph_entity_addr addr;	/* locker's address */
};

struct ceph_locker {
	struct ceph_locker_id id;
	struct ceph_locker_info info;
};

int ceph_cls_lock(struct ceph_osd_client *osdc,
		  struct ceph_object_id *oid,
		  struct ceph_object_locator *oloc,
		  char *lock_name, u8 type, char *cookie,
		  char *tag, char *desc, u8 flags);
int ceph_cls_unlock(struct ceph_osd_client *osdc,
		    struct ceph_object_id *oid,
		    struct ceph_object_locator *oloc,
		    char *lock_name, char *cookie);
int ceph_cls_break_lock(struct ceph_osd_client *osdc,
			struct ceph_object_id *oid,
			struct ceph_object_locator *oloc,
			char *lock_name, char *cookie,
			struct ceph_entity_name *locker);
int ceph_cls_set_cookie(struct ceph_osd_client *osdc,
			struct ceph_object_id *oid,
			struct ceph_object_locator *oloc,
			char *lock_name, u8 type, char *old_cookie,
			char *tag, char *new_cookie);

void ceph_free_lockers(struct ceph_locker *lockers, u32 num_lockers);

int ceph_cls_lock_info(struct ceph_osd_client *osdc,
		       struct ceph_object_id *oid,
		       struct ceph_object_locator *oloc,
		       char *lock_name, u8 *type, char **tag,
		       struct ceph_locker **lockers, u32 *num_lockers);

int ceph_cls_assert_locked(struct ceph_osd_request *req, int which,
			   char *lock_name, u8 type, char *cookie, char *tag);

#endif
