#ifndef _FS_CEPH_AUTH_NONE_H
#define _FS_CEPH_AUTH_NONE_H

#include <linux/slab.h>
#include <linux/ceph/auth.h>

/*
 * null security mode.
 *
 * we use a single static authorizer that simply encodes our entity name
 * and global id.
 */

struct ceph_none_authorizer {
	struct ceph_authorizer base;
	char buf[128];
	int buf_len;
	char reply_buf[0];
};

struct ceph_auth_none_info {
	bool starting;
};

int ceph_auth_none_init(struct ceph_auth_client *ac);

#endif

