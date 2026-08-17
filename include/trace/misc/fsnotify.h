/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Display helpers for fsnotify events
 */

#include <linux/fsnotify_backend.h>

#define show_fsnotify_mask(mask) \
	__print_flags(mask, "|", \
		{ FS_ACCESS,		"ACCESS" }, \
		{ FS_MODIFY,		"MODIFY" }, \
		{ FS_ATTRIB,		"ATTRIB" }, \
		{ FS_CLOSE_WRITE,	"CLOSE_WRITE" }, \
		{ FS_CLOSE_NOWRITE,	"CLOSE_NOWRITE" }, \
		{ FS_OPEN,		"OPEN" }, \
		{ FS_MOVED_FROM,	"MOVED_FROM" }, \
		{ FS_MOVED_TO,		"MOVED_TO" }, \
		{ FS_CREATE,		"CREATE" }, \
		{ FS_DELETE,		"DELETE" }, \
		{ FS_DELETE_SELF,	"DELETE_SELF" }, \
		{ FS_MOVE_SELF,		"MOVE_SELF" }, \
		{ FS_OPEN_EXEC,		"OPEN_EXEC" }, \
		{ FS_UNMOUNT,		"UNMOUNT" }, \
		{ FS_Q_OVERFLOW,	"Q_OVERFLOW" }, \
		{ FS_ERROR,		"ERROR" }, \
		{ FS_OPEN_PERM,		"OPEN_PERM" }, \
		{ FS_ACCESS_PERM,	"ACCESS_PERM" }, \
		{ FS_OPEN_EXEC_PERM,	"OPEN_EXEC_PERM" }, \
		{ FS_PRE_ACCESS,	"PRE_ACCESS" }, \
		{ FS_MNT_ATTACH,	"MNT_ATTACH" }, \
		{ FS_MNT_DETACH,	"MNT_DETACH" }, \
		{ FS_EVENT_ON_CHILD,	"EVENT_ON_CHILD" }, \
		{ FS_RENAME,		"RENAME" }, \
		{ FS_DN_MULTISHOT,	"DN_MULTISHOT" }, \
		{ FS_ISDIR,		"ISDIR" })
