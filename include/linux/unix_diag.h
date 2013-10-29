#ifndef __UNIX_DIAG_H__
#define __UNIX_DIAG_H__

#include <linux/types.h>

struct unix_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
	__u16	pad;
	__u32	udiag_states;
	__u32	udiag_ino;
	__u32	udiag_show;
	__u32	udiag_cookie[2];
};

#define UDIAG_SHOW_NAME		0x00000001	/* show name (not path) */
#define UDIAG_SHOW_VFS		0x00000002	/* show VFS inode info */
#define UDIAG_SHOW_PEER		0x00000004	/* show peer socket info */
#define UDIAG_SHOW_ICONS	0x00000008	/* show pending connections */
#define UDIAG_SHOW_RQLEN	0x00000010	/* show skb receive queue len */
#define UDIAG_SHOW_MEMINFO	0x00000020	/* show memory info of a socket */

struct unix_diag_msg {
	__u8	udiag_family;
	__u8	udiag_type;
	__u8	udiag_state;
	__u8	pad;

	__u32	udiag_ino;
	__u32	udiag_cookie[2];
};

enum {
	UNIX_DIAG_NAME,
	UNIX_DIAG_VFS,
	UNIX_DIAG_PEER,
	UNIX_DIAG_ICONS,
	UNIX_DIAG_RQLEN,
	UNIX_DIAG_MEMINFO,

	__UNIX_DIAG_MAX,
};

#define UNIX_DIAG_MAX (__UNIX_DIAG_MAX - 1)

struct unix_diag_vfs {
	__u32	udiag_vfs_ino;
	__u32	udiag_vfs_dev;
};

struct unix_diag_rqlen {
	__u32	udiag_rqueue;
	__u32	udiag_wqueue;
};

#endif
