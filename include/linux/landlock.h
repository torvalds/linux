/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Public types and definitions
 *
 * Copyright © 2016-2026 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _LINUX_LANDLOCK_H
#define _LINUX_LANDLOCK_H

#include <linux/types.h>
#include <uapi/linux/landlock.h>

/*
 * Access-right, scope, and permission names, shared between the audit records
 * (get_blocker() in security/landlock/audit.c) and the trace events
 * (include/trace/events/landlock.h).  A consumer defines
 * _LANDLOCK_NAME_ENTRY(mask, name) before expanding a list and undefines it
 * afterwards: audit maps each entry to a "[bit] = name" slot for O(1) lookup,
 * the trace events map it to a __print_flags() { mask, name } pair.  The bit
 * value lives only in the LANDLOCK_* UAPI constant each entry references.
 * Access-right and scope names are unprefixed; audit prepends the
 * "fs."/"net."/"scope." category.  Permission entries carry an action and a
 * domain for the qualified and bare views below.
 */
#define _LANDLOCK_ACCESS_FS_NAMES \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_EXECUTE, "execute"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_WRITE_FILE, "write_file"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_READ_FILE, "read_file"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_READ_DIR, "read_dir"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_REMOVE_DIR, "remove_dir"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_REMOVE_FILE, "remove_file"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_CHAR, "make_char"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_DIR, "make_dir"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_REG, "make_reg"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_SOCK, "make_sock"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_FIFO, "make_fifo"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_BLOCK, "make_block"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_MAKE_SYM, "make_sym"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_REFER, "refer"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_TRUNCATE, "truncate"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_IOCTL_DEV, "ioctl_dev"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_FS_RESOLVE_UNIX, "resolve_unix")

#define _LANDLOCK_ACCESS_NET_NAMES \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_NET_BIND_TCP, "bind_tcp"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_NET_CONNECT_TCP, "connect_tcp"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_NET_BIND_UDP, "bind_udp"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP, \
			     "connect_send_udp")

#define _LANDLOCK_SCOPE_NAMES \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET, \
			     "abstract_unix_socket"), \
	_LANDLOCK_NAME_ENTRY(LANDLOCK_SCOPE_SIGNAL, "signal")

#define _LANDLOCK_PERMISSION_NAMESPACE_NAME "namespace"
#define _LANDLOCK_PERMISSION_CAPABILITY_NAME "capability"

#define _LANDLOCK_PERMISSION_LIST(entry) \
	entry(LANDLOCK_PERMISSION_NAMESPACE_USE, "use", \
	      _LANDLOCK_PERMISSION_NAMESPACE_NAME), \
	entry(LANDLOCK_PERMISSION_CAPABILITY_USE, "use", \
	      _LANDLOCK_PERMISSION_CAPABILITY_NAME)

#define _LANDLOCK_PERMISSION_QUALIFIED_ENTRY(mask, action, domain) \
	_LANDLOCK_NAME_ENTRY(mask, domain "." action)

#define _LANDLOCK_PERMISSION_BARE_ENTRY(mask, action, ...) \
	_LANDLOCK_NAME_ENTRY(mask, action)

#define _LANDLOCK_PERMISSION_NAMES \
	_LANDLOCK_PERMISSION_LIST(_LANDLOCK_PERMISSION_QUALIFIED_ENTRY)

#define _LANDLOCK_PERMISSION_BLOCKER_NAMES \
	_LANDLOCK_PERMISSION_LIST(_LANDLOCK_PERMISSION_BARE_ENTRY)

#endif /* _LINUX_LANDLOCK_H */
