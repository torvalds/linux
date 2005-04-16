/*
 *  ncp_mount.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#ifndef _LINUX_NCP_MOUNT_H
#define _LINUX_NCP_MOUNT_H

#include <linux/types.h>
#include <linux/ncp.h>

#define NCP_MOUNT_VERSION 3	/* Binary */

/* Values for flags */
#define NCP_MOUNT_SOFT		0x0001
#define NCP_MOUNT_INTR		0x0002
#define NCP_MOUNT_STRONG	0x0004	/* enable delete/rename of r/o files */
#define NCP_MOUNT_NO_OS2	0x0008	/* do not use OS/2 (LONG) namespace */
#define NCP_MOUNT_NO_NFS	0x0010	/* do not use NFS namespace */
#define NCP_MOUNT_EXTRAS	0x0020
#define NCP_MOUNT_SYMLINKS	0x0040	/* enable symlinks */
#define NCP_MOUNT_NFS_EXTRAS	0x0080	/* Enable use of NFS NS meta-info */

struct ncp_mount_data {
	int version;
	unsigned int ncp_fd;	/* The socket to the ncp port */
	__kernel_uid_t mounted_uid;	/* Who may umount() this filesystem? */
	__kernel_pid_t wdog_pid;		/* Who cares for our watchdog packets? */

	unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
	unsigned int time_out;	/* How long should I wait after
				   sending a NCP request? */
	unsigned int retry_count;	/* And how often should I retry? */
	unsigned int flags;

	__kernel_uid_t uid;
	__kernel_gid_t gid;
	__kernel_mode_t file_mode;
	__kernel_mode_t dir_mode;
};

#define NCP_MOUNT_VERSION_V4	(4)	/* Binary or text */

struct ncp_mount_data_v4 {
	int version;
	unsigned long flags;	/* NCP_MOUNT_* flags */
	/* MIPS uses long __kernel_uid_t, but... */
	/* we neever pass -1, so it is safe */
	unsigned long mounted_uid;	/* Who may umount() this filesystem? */
	/* MIPS uses long __kernel_pid_t */
	long wdog_pid;		/* Who cares for our watchdog packets? */

	unsigned int ncp_fd;	/* The socket to the ncp port */
	unsigned int time_out;	/* How long should I wait after
				   sending a NCP request? */
	unsigned int retry_count;	/* And how often should I retry? */

	/* MIPS uses long __kernel_uid_t... */
	/* we never pass -1, so it is safe */
	unsigned long uid;
	unsigned long gid;
	/* MIPS uses unsigned long __kernel_mode_t */
	unsigned long file_mode;
	unsigned long dir_mode;
};

#define NCP_MOUNT_VERSION_V5	(5)	/* Text only */

#ifdef __KERNEL__

struct ncp_mount_data_kernel {
	unsigned long    flags;		/* NCP_MOUNT_* flags */
	unsigned int	 int_flags;	/* internal flags */
#define NCP_IMOUNT_LOGGEDIN_POSSIBLE	0x0001
	__kernel_uid32_t mounted_uid;	/* Who may umount() this filesystem? */
	__kernel_pid_t   wdog_pid;		/* Who cares for our watchdog packets? */
	unsigned int     ncp_fd;	/* The socket to the ncp port */
	unsigned int     time_out;	/* How long should I wait after
					   sending a NCP request? */
	unsigned int     retry_count;	/* And how often should I retry? */
	unsigned char	 mounted_vol[NCP_VOLNAME_LEN + 1];
	__kernel_uid32_t uid;
	__kernel_gid32_t gid;
	__kernel_mode_t  file_mode;
	__kernel_mode_t  dir_mode;
	int		 info_fd;
};

#endif /* __KERNEL__ */

#endif
