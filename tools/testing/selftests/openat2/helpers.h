// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 */

#ifndef __RESOLVEAT_H__
#define __RESOLVEAT_H__

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/types.h>
#include "../kselftest.h"

#define ARRAY_LEN(X) (sizeof (X) / sizeof (*(X)))
#define BUILD_BUG_ON(e) ((void)(sizeof(struct { int:(-!!(e)); })))

#ifndef SYS_openat2
#ifndef __NR_openat2
#define __NR_openat2 437
#endif /* __NR_openat2 */
#define SYS_openat2 __NR_openat2
#endif /* SYS_openat2 */

/*
 * Arguments for how openat2(2) should open the target path. If @resolve is
 * zero, then openat2(2) operates very similarly to openat(2).
 *
 * However, unlike openat(2), unknown bits in @flags result in -EINVAL rather
 * than being silently ignored. @mode must be zero unless one of {O_CREAT,
 * O_TMPFILE} are set.
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

#define OPEN_HOW_SIZE_VER0	24 /* sizeof first published struct */
#define OPEN_HOW_SIZE_LATEST	OPEN_HOW_SIZE_VER0

bool needs_openat2(const struct open_how *how);

#ifndef RESOLVE_IN_ROOT
/* how->resolve flags for openat2(2). */
#define RESOLVE_NO_XDEV		0x01 /* Block mount-point crossings
					(includes bind-mounts). */
#define RESOLVE_NO_MAGICLINKS	0x02 /* Block traversal through procfs-style
					"magic-links". */
#define RESOLVE_NO_SYMLINKS	0x04 /* Block traversal through all symlinks
					(implies OEXT_NO_MAGICLINKS) */
#define RESOLVE_BENEATH		0x08 /* Block "lexical" trickery like
					"..", symlinks, and absolute
					paths which escape the dirfd. */
#define RESOLVE_IN_ROOT		0x10 /* Make all jumps to "/" and ".."
					be scoped inside the dirfd
					(similar to chroot(2)). */
#endif /* RESOLVE_IN_ROOT */

#define E_func(func, ...)						      \
	do {								      \
		errno = 0;						      \
		if (func(__VA_ARGS__) < 0)				      \
			ksft_exit_fail_msg("%s:%d %s failed - errno:%d\n",    \
					   __FILE__, __LINE__, #func, errno); \
	} while (0)

#define E_asprintf(...)		E_func(asprintf,	__VA_ARGS__)
#define E_chmod(...)		E_func(chmod,		__VA_ARGS__)
#define E_dup2(...)		E_func(dup2,		__VA_ARGS__)
#define E_fchdir(...)		E_func(fchdir,		__VA_ARGS__)
#define E_fstatat(...)		E_func(fstatat,		__VA_ARGS__)
#define E_kill(...)		E_func(kill,		__VA_ARGS__)
#define E_mkdirat(...)		E_func(mkdirat,		__VA_ARGS__)
#define E_mount(...)		E_func(mount,		__VA_ARGS__)
#define E_prctl(...)		E_func(prctl,		__VA_ARGS__)
#define E_readlink(...)		E_func(readlink,	__VA_ARGS__)
#define E_setresuid(...)	E_func(setresuid,	__VA_ARGS__)
#define E_symlinkat(...)	E_func(symlinkat,	__VA_ARGS__)
#define E_touchat(...)		E_func(touchat,		__VA_ARGS__)
#define E_unshare(...)		E_func(unshare,		__VA_ARGS__)

#define E_assert(expr, msg, ...)					\
	do {								\
		if (!(expr))						\
			ksft_exit_fail_msg("ASSERT(%s:%d) failed (%s): " msg "\n", \
					   __FILE__, __LINE__, #expr, ##__VA_ARGS__); \
	} while (0)

int raw_openat2(int dfd, const char *path, void *how, size_t size);
int sys_openat2(int dfd, const char *path, struct open_how *how);
int sys_openat(int dfd, const char *path, struct open_how *how);
int sys_renameat2(int olddirfd, const char *oldpath,
		  int newdirfd, const char *newpath, unsigned int flags);

int touchat(int dfd, const char *path);
char *fdreadlink(int fd);
bool fdequal(int fd, int dfd, const char *path);

extern bool openat2_supported;

#endif /* __RESOLVEAT_H__ */
