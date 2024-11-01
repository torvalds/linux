// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 IBM Corporation
 *
 * Author:
 * Mimi Zohar <zohar@us.ibm.com>
 */

#include <linux/xattr.h>
#include <linux/evm.h>

int posix_xattr_acl(const char *xattr)
{
	int xattr_len = strlen(xattr);

	if ((strlen(XATTR_NAME_POSIX_ACL_ACCESS) == xattr_len)
	     && (strncmp(XATTR_NAME_POSIX_ACL_ACCESS, xattr, xattr_len) == 0))
		return 1;
	if ((strlen(XATTR_NAME_POSIX_ACL_DEFAULT) == xattr_len)
	     && (strncmp(XATTR_NAME_POSIX_ACL_DEFAULT, xattr, xattr_len) == 0))
		return 1;
	return 0;
}
