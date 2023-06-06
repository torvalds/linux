/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Landlock - User space API
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#ifndef _UAPI_LINUX_LANDLOCK_H
#define _UAPI_LINUX_LANDLOCK_H

#include <linux/types.h>

/**
 * struct landlock_ruleset_attr - Ruleset definition
 *
 * Argument of sys_landlock_create_ruleset().  This structure can grow in
 * future versions.
 */
struct landlock_ruleset_attr {
	/**
	 * @handled_access_fs: Bitmask of actions (cf. `Filesystem flags`_)
	 * that is handled by this ruleset and should then be forbidden if no
	 * rule explicitly allow them: it is a deny-by-default list that should
	 * contain as much Landlock access rights as possible. Indeed, all
	 * Landlock filesystem access rights that are not part of
	 * handled_access_fs are allowed.  This is needed for backward
	 * compatibility reasons.  One exception is the
	 * %LANDLOCK_ACCESS_FS_REFER access right, which is always implicitly
	 * handled, but must still be explicitly handled to add new rules with
	 * this access right.
	 */
	__u64 handled_access_fs;
};

/*
 * sys_landlock_create_ruleset() flags:
 *
 * - %LANDLOCK_CREATE_RULESET_VERSION: Get the highest supported Landlock ABI
 *   version.
 */
/* clang-format off */
#define LANDLOCK_CREATE_RULESET_VERSION			(1U << 0)
/* clang-format on */

/**
 * enum landlock_rule_type - Landlock rule type
 *
 * Argument of sys_landlock_add_rule().
 */
enum landlock_rule_type {
	/**
	 * @LANDLOCK_RULE_PATH_BENEATH: Type of a &struct
	 * landlock_path_beneath_attr .
	 */
	LANDLOCK_RULE_PATH_BENEATH = 1,
};

/**
 * struct landlock_path_beneath_attr - Path hierarchy definition
 *
 * Argument of sys_landlock_add_rule().
 */
struct landlock_path_beneath_attr {
	/**
	 * @allowed_access: Bitmask of allowed actions for this file hierarchy
	 * (cf. `Filesystem flags`_).
	 */
	__u64 allowed_access;
	/**
	 * @parent_fd: File descriptor, preferably opened with ``O_PATH``,
	 * which identifies the parent directory of a file hierarchy, or just a
	 * file.
	 */
	__s32 parent_fd;
	/*
	 * This struct is packed to avoid trailing reserved members.
	 * Cf. security/landlock/syscalls.c:build_check_abi()
	 */
} __attribute__((packed));

/**
 * DOC: fs_access
 *
 * A set of actions on kernel objects may be defined by an attribute (e.g.
 * &struct landlock_path_beneath_attr) including a bitmask of access.
 *
 * Filesystem flags
 * ~~~~~~~~~~~~~~~~
 *
 * These flags enable to restrict a sandboxed process to a set of actions on
 * files and directories.  Files or directories opened before the sandboxing
 * are not subject to these restrictions.
 *
 * A file can only receive these access rights:
 *
 * - %LANDLOCK_ACCESS_FS_EXECUTE: Execute a file.
 * - %LANDLOCK_ACCESS_FS_WRITE_FILE: Open a file with write access. Note that
 *   you might additionally need the %LANDLOCK_ACCESS_FS_TRUNCATE right in order
 *   to overwrite files with :manpage:`open(2)` using ``O_TRUNC`` or
 *   :manpage:`creat(2)`.
 * - %LANDLOCK_ACCESS_FS_READ_FILE: Open a file with read access.
 * - %LANDLOCK_ACCESS_FS_TRUNCATE: Truncate a file with :manpage:`truncate(2)`,
 *   :manpage:`ftruncate(2)`, :manpage:`creat(2)`, or :manpage:`open(2)` with
 *   ``O_TRUNC``. Whether an opened file can be truncated with
 *   :manpage:`ftruncate(2)` is determined during :manpage:`open(2)`, in the
 *   same way as read and write permissions are checked during
 *   :manpage:`open(2)` using %LANDLOCK_ACCESS_FS_READ_FILE and
 *   %LANDLOCK_ACCESS_FS_WRITE_FILE. This access right is available since the
 *   third version of the Landlock ABI.
 *
 * A directory can receive access rights related to files or directories.  The
 * following access right is applied to the directory itself, and the
 * directories beneath it:
 *
 * - %LANDLOCK_ACCESS_FS_READ_DIR: Open a directory or list its content.
 *
 * However, the following access rights only apply to the content of a
 * directory, not the directory itself:
 *
 * - %LANDLOCK_ACCESS_FS_REMOVE_DIR: Remove an empty directory or rename one.
 * - %LANDLOCK_ACCESS_FS_REMOVE_FILE: Unlink (or rename) a file.
 * - %LANDLOCK_ACCESS_FS_MAKE_CHAR: Create (or rename or link) a character
 *   device.
 * - %LANDLOCK_ACCESS_FS_MAKE_DIR: Create (or rename) a directory.
 * - %LANDLOCK_ACCESS_FS_MAKE_REG: Create (or rename or link) a regular file.
 * - %LANDLOCK_ACCESS_FS_MAKE_SOCK: Create (or rename or link) a UNIX domain
 *   socket.
 * - %LANDLOCK_ACCESS_FS_MAKE_FIFO: Create (or rename or link) a named pipe.
 * - %LANDLOCK_ACCESS_FS_MAKE_BLOCK: Create (or rename or link) a block device.
 * - %LANDLOCK_ACCESS_FS_MAKE_SYM: Create (or rename or link) a symbolic link.
 * - %LANDLOCK_ACCESS_FS_REFER: Link or rename a file from or to a different
 *   directory (i.e. reparent a file hierarchy).
 *
 *   This access right is available since the second version of the Landlock
 *   ABI.
 *
 *   This is the only access right which is denied by default by any ruleset,
 *   even if the right is not specified as handled at ruleset creation time.
 *   The only way to make a ruleset grant this right is to explicitly allow it
 *   for a specific directory by adding a matching rule to the ruleset.
 *
 *   In particular, when using the first Landlock ABI version, Landlock will
 *   always deny attempts to reparent files between different directories.
 *
 *   In addition to the source and destination directories having the
 *   %LANDLOCK_ACCESS_FS_REFER access right, the attempted link or rename
 *   operation must meet the following constraints:
 *
 *   * The reparented file may not gain more access rights in the destination
 *     directory than it previously had in the source directory.  If this is
 *     attempted, the operation results in an ``EXDEV`` error.
 *
 *   * When linking or renaming, the ``LANDLOCK_ACCESS_FS_MAKE_*`` right for the
 *     respective file type must be granted for the destination directory.
 *     Otherwise, the operation results in an ``EACCES`` error.
 *
 *   * When renaming, the ``LANDLOCK_ACCESS_FS_REMOVE_*`` right for the
 *     respective file type must be granted for the source directory.  Otherwise,
 *     the operation results in an ``EACCES`` error.
 *
 *   If multiple requirements are not met, the ``EACCES`` error code takes
 *   precedence over ``EXDEV``.
 *
 * .. warning::
 *
 *   It is currently not possible to restrict some file-related actions
 *   accessible through these syscall families: :manpage:`chdir(2)`,
 *   :manpage:`stat(2)`, :manpage:`flock(2)`, :manpage:`chmod(2)`,
 *   :manpage:`chown(2)`, :manpage:`setxattr(2)`, :manpage:`utime(2)`,
 *   :manpage:`ioctl(2)`, :manpage:`fcntl(2)`, :manpage:`access(2)`.
 *   Future Landlock evolutions will enable to restrict them.
 */
/* clang-format off */
#define LANDLOCK_ACCESS_FS_EXECUTE			(1ULL << 0)
#define LANDLOCK_ACCESS_FS_WRITE_FILE			(1ULL << 1)
#define LANDLOCK_ACCESS_FS_READ_FILE			(1ULL << 2)
#define LANDLOCK_ACCESS_FS_READ_DIR			(1ULL << 3)
#define LANDLOCK_ACCESS_FS_REMOVE_DIR			(1ULL << 4)
#define LANDLOCK_ACCESS_FS_REMOVE_FILE			(1ULL << 5)
#define LANDLOCK_ACCESS_FS_MAKE_CHAR			(1ULL << 6)
#define LANDLOCK_ACCESS_FS_MAKE_DIR			(1ULL << 7)
#define LANDLOCK_ACCESS_FS_MAKE_REG			(1ULL << 8)
#define LANDLOCK_ACCESS_FS_MAKE_SOCK			(1ULL << 9)
#define LANDLOCK_ACCESS_FS_MAKE_FIFO			(1ULL << 10)
#define LANDLOCK_ACCESS_FS_MAKE_BLOCK			(1ULL << 11)
#define LANDLOCK_ACCESS_FS_MAKE_SYM			(1ULL << 12)
#define LANDLOCK_ACCESS_FS_REFER			(1ULL << 13)
#define LANDLOCK_ACCESS_FS_TRUNCATE			(1ULL << 14)
/* clang-format on */

#endif /* _UAPI_LINUX_LANDLOCK_H */
