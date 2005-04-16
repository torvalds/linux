/*
  File: linux/xattr_acl.h

  (extended attribute representation of access control lists)

  (C) 2000 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

#ifndef _LINUX_XATTR_ACL_H
#define _LINUX_XATTR_ACL_H

#include <linux/posix_acl.h>

#define XATTR_NAME_ACL_ACCESS	"system.posix_acl_access"
#define XATTR_NAME_ACL_DEFAULT	"system.posix_acl_default"

#define XATTR_ACL_VERSION	0x0002

typedef struct {
	__u16		e_tag;
	__u16		e_perm;
	__u32		e_id;
} xattr_acl_entry;

typedef struct {
	__u32		a_version;
	xattr_acl_entry	a_entries[0];
} xattr_acl_header;

static inline size_t xattr_acl_size(int count)
{
	return sizeof(xattr_acl_header) + count * sizeof(xattr_acl_entry);
}

static inline int xattr_acl_count(size_t size)
{
	if (size < sizeof(xattr_acl_header))
		return -1;
	size -= sizeof(xattr_acl_header);
	if (size % sizeof(xattr_acl_entry))
		return -1;
	return size / sizeof(xattr_acl_entry);
}

struct posix_acl * posix_acl_from_xattr(const void *value, size_t size);
int posix_acl_to_xattr(const struct posix_acl *acl, void *buffer, size_t size);



#endif /* _LINUX_XATTR_ACL_H */
