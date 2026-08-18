/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

#include <linux/stat.h>
#include <uapi/linux/fcntl.h>
#include <uapi/linux/openat2.h>

/* List of all valid flags for the open/openat flags argument: */
#define VALID_OPEN_FLAGS \
	(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | \
	 O_APPEND | O_NDELAY | O_NONBLOCK | __O_SYNC | O_DSYNC | \
	 FASYNC	| O_DIRECT | O_LARGEFILE | O_DIRECTORY | O_NOFOLLOW | \
	 O_NOATIME | O_CLOEXEC | O_PATH | __O_TMPFILE | O_EMPTYPATH)

/* List of all valid flags for openat2(2)'s how->flags argument. */
#define VALID_OPENAT2_FLAGS	(VALID_OPEN_FLAGS | OPENAT2_REGULAR)

/*
 * Kernel-internal carrier for OPENAT2_REGULAR. The UAPI bit lives in the
 * upper 32 bits of open_how::flags so open()/openat() cannot encode it.
 * build_open_flags() translates it to this internal flag, which then
 * propagates through op->open_flag and f->f_flags exactly like __FMODE_EXEC.
 * do_dentry_open() strips it so userspace cannot observe it via
 * fcntl(F_GETFL).
 *
 * Bit 30 is not claimed by any O_* flag on any architecture and stays clear
 * of the sign bit of the int op->open_flag. fcntl_init() enforces that it
 * never aliases an open-flag bit.
 */
#define __O_REGULAR		(1 << 30)

/* List of all valid flags for the how->resolve argument: */
#define VALID_RESOLVE_FLAGS \
	(RESOLVE_NO_XDEV | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS | \
	 RESOLVE_BENEATH | RESOLVE_IN_ROOT | RESOLVE_CACHED)

/* List of all open_how "versions". */
#define OPEN_HOW_SIZE_VER0	24 /* sizeof first published struct */
#define OPEN_HOW_SIZE_LATEST	OPEN_HOW_SIZE_VER0

#ifndef force_o_largefile
#define force_o_largefile() (!IS_ENABLED(CONFIG_ARCH_32BIT_OFF_T))
#endif

#if BITS_PER_LONG == 32
#define IS_GETLK32(cmd)		((cmd) == F_GETLK)
#define IS_SETLK32(cmd)		((cmd) == F_SETLK)
#define IS_SETLKW32(cmd)	((cmd) == F_SETLKW)
#define IS_GETLK64(cmd)		((cmd) == F_GETLK64)
#define IS_SETLK64(cmd)		((cmd) == F_SETLK64)
#define IS_SETLKW64(cmd)	((cmd) == F_SETLKW64)
#else
#define IS_GETLK32(cmd)		(0)
#define IS_SETLK32(cmd)		(0)
#define IS_SETLKW32(cmd)	(0)
#define IS_GETLK64(cmd)		((cmd) == F_GETLK)
#define IS_SETLK64(cmd)		((cmd) == F_SETLK)
#define IS_SETLKW64(cmd)	((cmd) == F_SETLKW)
#endif /* BITS_PER_LONG == 32 */

#define IS_GETLK(cmd)	(IS_GETLK32(cmd)  || IS_GETLK64(cmd))
#define IS_SETLK(cmd)	(IS_SETLK32(cmd)  || IS_SETLK64(cmd))
#define IS_SETLKW(cmd)	(IS_SETLKW32(cmd) || IS_SETLKW64(cmd))

#endif
