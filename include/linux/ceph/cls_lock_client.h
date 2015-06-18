#ifndef _LINUX_CEPH_CLS_LOCK_CLIENT_H
#define _LINUX_CEPH_CLS_LOCK_CLIENT_H

#include <linux/ceph/osd_client.h>

enum ceph_cls_lock_type {
	CEPH_CLS_LOCK_NONE = 0,
	CEPH_CLS_LOCK_EXCLUSIVE = 1,
	CEPH_CLS_LOCK_SHARED = 2,
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

#endif
