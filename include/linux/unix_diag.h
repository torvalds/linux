#ifndef __UNIX_DIAG_H__
#define __UNIX_DIAG_H__

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

	UNIX_DIAG_MAX,
};

#endif
