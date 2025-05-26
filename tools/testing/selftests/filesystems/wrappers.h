// SPDX-License-Identifier: GPL-2.0
//
#ifndef __SELFTEST_OVERLAYFS_WRAPPERS_H__
#define __SELFTEST_OVERLAYFS_WRAPPERS_H__

#define _GNU_SOURCE

#include <linux/types.h>
#include <linux/mount.h>
#include <sys/syscall.h>

#ifndef STATX_MNT_ID_UNIQUE
#define STATX_MNT_ID_UNIQUE 0x00004000U /* Want/got extended stx_mount_id */
#endif

static inline int sys_fsopen(const char *fsname, unsigned int flags)
{
	return syscall(__NR_fsopen, fsname, flags);
}

static inline int sys_fsconfig(int fd, unsigned int cmd, const char *key,
			       const char *value, int aux)
{
	return syscall(__NR_fsconfig, fd, cmd, key, value, aux);
}

static inline int sys_fsmount(int fd, unsigned int flags,
			      unsigned int attr_flags)
{
	return syscall(__NR_fsmount, fd, flags, attr_flags);
}

static inline int sys_mount(const char *src, const char *tgt, const char *fst,
			    unsigned long flags, const void *data)
{
	return syscall(__NR_mount, src, tgt, fst, flags, data);
}

#ifndef MOVE_MOUNT_F_EMPTY_PATH
#define MOVE_MOUNT_F_EMPTY_PATH 0x00000004 /* Empty from path permitted */
#endif

#ifndef MOVE_MOUNT_T_EMPTY_PATH
#define MOVE_MOUNT_T_EMPTY_PATH 0x00000040 /* Empty to path permitted */
#endif

#ifndef __NR_move_mount
	#if defined __alpha__
		#define __NR_move_mount 539
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_move_mount 4429
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_move_mount 6429
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_move_mount 5429
		#endif
	#else
		#define __NR_move_mount 429
	#endif
#endif

static inline int sys_move_mount(int from_dfd, const char *from_pathname,
				 int to_dfd, const char *to_pathname,
				 unsigned int flags)
{
	return syscall(__NR_move_mount, from_dfd, from_pathname, to_dfd,
		       to_pathname, flags);
}

#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE 1
#endif

#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

#ifndef AT_RECURSIVE
#define AT_RECURSIVE 0x8000 /* Apply to the entire subtree */
#endif

#ifndef __NR_open_tree
	#if defined __alpha__
		#define __NR_open_tree 538
	#elif defined _MIPS_SIM
		#if _MIPS_SIM == _MIPS_SIM_ABI32	/* o32 */
			#define __NR_open_tree 4428
		#endif
		#if _MIPS_SIM == _MIPS_SIM_NABI32	/* n32 */
			#define __NR_open_tree 6428
		#endif
		#if _MIPS_SIM == _MIPS_SIM_ABI64	/* n64 */
			#define __NR_open_tree 5428
		#endif
	#else
		#define __NR_open_tree 428
	#endif
#endif

static inline int sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(__NR_open_tree, dfd, filename, flags);
}

#endif
