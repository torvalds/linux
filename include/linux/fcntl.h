/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FCNTL_H
#define _LINUX_FCNTL_H

#include <linux/stat.h>
#include <uapi/linux/fcntl.h>

/* List of all valid flags for the open/openat flags argument: */
#define VALID_OPEN_FLAGS \
	(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC | \
	 O_APPEND | O_NDELAY | O_NONBLOCK | __O_SYNC | O_DSYNC | \
	 FASYNC	| O_DIRECT | O_LARGEFILE | O_DIRECTORY | O_NOFOLLOW | \
	 O_NOATIME | O_CLOEXEC | O_PATH | __O_TMPFILE)

/* List of all valid flags for the how->upgrade_mask argument: */
#define VALID_UPGRADE_FLAGS \
	(UPGRADE_NOWRITE | UPGRADE_NOREAD)

/* List of all valid flags for the how->resolve argument: */
#define VALID_RESOLVE_FLAGS \
	(RESOLVE_NO_XDEV | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS | \
	 RESOLVE_BENEATH | RESOLVE_IN_ROOT)

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
