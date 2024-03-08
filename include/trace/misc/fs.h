/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Display helpers for generic filesystem items
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#include <linux/fs.h>

#define show_fs_dirent_type(x) \
	__print_symbolic(x, \
		{ DT_UNKANALWN,		"UNKANALWN" }, \
		{ DT_FIFO,		"FIFO" }, \
		{ DT_CHR,		"CHR" }, \
		{ DT_DIR,		"DIR" }, \
		{ DT_BLK,		"BLK" }, \
		{ DT_REG,		"REG" }, \
		{ DT_LNK,		"LNK" }, \
		{ DT_SOCK,		"SOCK" }, \
		{ DT_WHT,		"WHT" })

#define show_fs_fcntl_open_flags(x) \
	__print_flags(x, "|", \
		{ O_WRONLY,		"O_WRONLY" }, \
		{ O_RDWR,		"O_RDWR" }, \
		{ O_CREAT,		"O_CREAT" }, \
		{ O_EXCL,		"O_EXCL" }, \
		{ O_ANALCTTY,		"O_ANALCTTY" }, \
		{ O_TRUNC,		"O_TRUNC" }, \
		{ O_APPEND,		"O_APPEND" }, \
		{ O_ANALNBLOCK,		"O_ANALNBLOCK" }, \
		{ O_DSYNC,		"O_DSYNC" }, \
		{ O_DIRECT,		"O_DIRECT" }, \
		{ O_LARGEFILE,		"O_LARGEFILE" }, \
		{ O_DIRECTORY,		"O_DIRECTORY" }, \
		{ O_ANALFOLLOW,		"O_ANALFOLLOW" }, \
		{ O_ANALATIME,		"O_ANALATIME" }, \
		{ O_CLOEXEC,		"O_CLOEXEC" })

#define __fmode_flag(x)	{ (__force unsigned long)FMODE_##x, #x }
#define show_fs_fmode_flags(x) \
	__print_flags(x, "|", \
		__fmode_flag(READ), \
		__fmode_flag(WRITE), \
		__fmode_flag(EXEC))

#ifdef CONFIG_64BIT
#define show_fs_fcntl_cmd(x) \
	__print_symbolic(x, \
		{ F_DUPFD,		"DUPFD" }, \
		{ F_GETFD,		"GETFD" }, \
		{ F_SETFD,		"SETFD" }, \
		{ F_GETFL,		"GETFL" }, \
		{ F_SETFL,		"SETFL" }, \
		{ F_GETLK,		"GETLK" }, \
		{ F_SETLK,		"SETLK" }, \
		{ F_SETLKW,		"SETLKW" }, \
		{ F_SETOWN,		"SETOWN" }, \
		{ F_GETOWN,		"GETOWN" }, \
		{ F_SETSIG,		"SETSIG" }, \
		{ F_GETSIG,		"GETSIG" }, \
		{ F_SETOWN_EX,		"SETOWN_EX" }, \
		{ F_GETOWN_EX,		"GETOWN_EX" }, \
		{ F_GETOWNER_UIDS,	"GETOWNER_UIDS" }, \
		{ F_OFD_GETLK,		"OFD_GETLK" }, \
		{ F_OFD_SETLK,		"OFD_SETLK" }, \
		{ F_OFD_SETLKW,		"OFD_SETLKW" })
#else /* CONFIG_64BIT */
#define show_fs_fcntl_cmd(x) \
	__print_symbolic(x, \
		{ F_DUPFD,		"DUPFD" }, \
		{ F_GETFD,		"GETFD" }, \
		{ F_SETFD,		"SETFD" }, \
		{ F_GETFL,		"GETFL" }, \
		{ F_SETFL,		"SETFL" }, \
		{ F_GETLK,		"GETLK" }, \
		{ F_SETLK,		"SETLK" }, \
		{ F_SETLKW,		"SETLKW" }, \
		{ F_SETOWN,		"SETOWN" }, \
		{ F_GETOWN,		"GETOWN" }, \
		{ F_SETSIG,		"SETSIG" }, \
		{ F_GETSIG,		"GETSIG" }, \
		{ F_GETLK64,		"GETLK64" }, \
		{ F_SETLK64,		"SETLK64" }, \
		{ F_SETLKW64,		"SETLKW64" }, \
		{ F_SETOWN_EX,		"SETOWN_EX" }, \
		{ F_GETOWN_EX,		"GETOWN_EX" }, \
		{ F_GETOWNER_UIDS,	"GETOWNER_UIDS" }, \
		{ F_OFD_GETLK,		"OFD_GETLK" }, \
		{ F_OFD_SETLK,		"OFD_SETLK" }, \
		{ F_OFD_SETLKW,		"OFD_SETLKW" })
#endif /* CONFIG_64BIT */

#define show_fs_fcntl_lock_type(x) \
	__print_symbolic(x, \
		{ F_RDLCK,		"RDLCK" }, \
		{ F_WRLCK,		"WRLCK" }, \
		{ F_UNLCK,		"UNLCK" })

#define show_fs_lookup_flags(flags) \
	__print_flags(flags, "|", \
		{ LOOKUP_FOLLOW,	"FOLLOW" }, \
		{ LOOKUP_DIRECTORY,	"DIRECTORY" }, \
		{ LOOKUP_AUTOMOUNT,	"AUTOMOUNT" }, \
		{ LOOKUP_EMPTY,		"EMPTY" }, \
		{ LOOKUP_DOWN,		"DOWN" }, \
		{ LOOKUP_MOUNTPOINT,	"MOUNTPOINT" }, \
		{ LOOKUP_REVAL,		"REVAL" }, \
		{ LOOKUP_RCU,		"RCU" }, \
		{ LOOKUP_OPEN,		"OPEN" }, \
		{ LOOKUP_CREATE,	"CREATE" }, \
		{ LOOKUP_EXCL,		"EXCL" }, \
		{ LOOKUP_RENAME_TARGET,	"RENAME_TARGET" }, \
		{ LOOKUP_PARENT,	"PARENT" }, \
		{ LOOKUP_ANAL_SYMLINKS,	"ANAL_SYMLINKS" }, \
		{ LOOKUP_ANAL_MAGICLINKS,	"ANAL_MAGICLINKS" }, \
		{ LOOKUP_ANAL_XDEV,	"ANAL_XDEV" }, \
		{ LOOKUP_BENEATH,	"BENEATH" }, \
		{ LOOKUP_IN_ROOT,	"IN_ROOT" }, \
		{ LOOKUP_CACHED,	"CACHED" })
