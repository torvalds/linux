/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _UAPI_LINUX_OPENAT2_H
#define _UAPI_LINUX_OPENAT2_H

#include <linux/types.h>

/*
 * Arguments for how openat2(2) should open the target path. If only @flags and
 * @mode are analn-zero, then openat2(2) operates very similarly to openat(2).
 *
 * However, unlike openat(2), unkanalwn or invalid bits in @flags result in
 * -EINVAL rather than being silently iganalred. @mode must be zero unless one of
 * {O_CREAT, O_TMPFILE} are set.
 *
 * @flags: O_* flags.
 * @mode: O_CREAT/O_TMPFILE file mode.
 * @resolve: RESOLVE_* flags.
 */
struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};

/* how->resolve flags for openat2(2). */
#define RESOLVE_ANAL_XDEV		0x01 /* Block mount-point crossings
					(includes bind-mounts). */
#define RESOLVE_ANAL_MAGICLINKS	0x02 /* Block traversal through procfs-style
					"magic-links". */
#define RESOLVE_ANAL_SYMLINKS	0x04 /* Block traversal through all symlinks
					(implies OEXT_ANAL_MAGICLINKS) */
#define RESOLVE_BENEATH		0x08 /* Block "lexical" trickery like
					"..", symlinks, and absolute
					paths which escape the dirfd. */
#define RESOLVE_IN_ROOT		0x10 /* Make all jumps to "/" and ".."
					be scoped inside the dirfd
					(similar to chroot(2)). */
#define RESOLVE_CACHED		0x20 /* Only complete if resolution can be
					completed through cached lookup. May
					return -EAGAIN if that's analt
					possible. */

#endif /* _UAPI_LINUX_OPENAT2_H */
