/* 32-bit compatibility syscall for 64-bit systems
 *
 * Copyright (C) 2004-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/syscalls.h>
#include <linux/keyctl.h>
#include <linux/compat.h>
#include <linux/slab.h>
#include "internal.h"

/*
 * Instantiate a key with the specified compatibility multipart payload and
 * link the key into the destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * If successful, 0 will be returned.
 */
long compat_keyctl_instantiate_key_iov(
	key_serial_t id,
	const struct compat_iovec __user *_payload_iov,
	unsigned ioc,
	key_serial_t ringid)
{
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	long ret;

	if (_payload_iov == 0 || ioc == 0)
		goto no_payload;

	ret = compat_rw_copy_check_uvector(WRITE, _payload_iov, ioc,
					   ARRAY_SIZE(iovstack),
					   iovstack, &iov, 1);
	if (ret < 0)
		return ret;
	if (ret == 0)
		goto no_payload_free;

	ret = keyctl_instantiate_key_common(id, iov, ioc, ret, ringid);

	if (iov != iovstack)
		kfree(iov);
	return ret;

no_payload_free:
	if (iov != iovstack)
		kfree(iov);
no_payload:
	return keyctl_instantiate_key_common(id, NULL, 0, 0, ringid);
}

/*
 * The key control system call, 32-bit compatibility version for 64-bit archs
 *
 * This should only be called if the 64-bit arch uses weird pointers in 32-bit
 * mode or doesn't guarantee that the top 32-bits of the argument registers on
 * taking a 32-bit syscall are zero.  If you can, you should call sys_keyctl()
 * directly.
 */
asmlinkage long compat_sys_keyctl(u32 option,
				  u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	switch (option) {
	case KEYCTL_GET_KEYRING_ID:
		return keyctl_get_keyring_ID(arg2, arg3);

	case KEYCTL_JOIN_SESSION_KEYRING:
		return keyctl_join_session_keyring(compat_ptr(arg2));

	case KEYCTL_UPDATE:
		return keyctl_update_key(arg2, compat_ptr(arg3), arg4);

	case KEYCTL_REVOKE:
		return keyctl_revoke_key(arg2);

	case KEYCTL_DESCRIBE:
		return keyctl_describe_key(arg2, compat_ptr(arg3), arg4);

	case KEYCTL_CLEAR:
		return keyctl_keyring_clear(arg2);

	case KEYCTL_LINK:
		return keyctl_keyring_link(arg2, arg3);

	case KEYCTL_UNLINK:
		return keyctl_keyring_unlink(arg2, arg3);

	case KEYCTL_SEARCH:
		return keyctl_keyring_search(arg2, compat_ptr(arg3),
					     compat_ptr(arg4), arg5);

	case KEYCTL_READ:
		return keyctl_read_key(arg2, compat_ptr(arg3), arg4);

	case KEYCTL_CHOWN:
		return keyctl_chown_key(arg2, arg3, arg4);

	case KEYCTL_SETPERM:
		return keyctl_setperm_key(arg2, arg3);

	case KEYCTL_INSTANTIATE:
		return keyctl_instantiate_key(arg2, compat_ptr(arg3), arg4,
					      arg5);

	case KEYCTL_NEGATE:
		return keyctl_negate_key(arg2, arg3, arg4);

	case KEYCTL_SET_REQKEY_KEYRING:
		return keyctl_set_reqkey_keyring(arg2);

	case KEYCTL_SET_TIMEOUT:
		return keyctl_set_timeout(arg2, arg3);

	case KEYCTL_ASSUME_AUTHORITY:
		return keyctl_assume_authority(arg2);

	case KEYCTL_GET_SECURITY:
		return keyctl_get_security(arg2, compat_ptr(arg3), arg4);

	case KEYCTL_SESSION_TO_PARENT:
		return keyctl_session_to_parent();

	case KEYCTL_REJECT:
		return keyctl_reject_key(arg2, arg3, arg4, arg5);

	case KEYCTL_INSTANTIATE_IOV:
		return compat_keyctl_instantiate_key_iov(
			arg2, compat_ptr(arg3), arg4, arg5);

	case KEYCTL_INVALIDATE:
		return keyctl_invalidate_key(arg2);

	default:
		return -EOPNOTSUPP;
	}
}
