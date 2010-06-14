/*
  drbd.h
  Kernel module for 2.6.x Kernels

  This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

  Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
  Copyright (C) 2001-2008, Philipp Reisner <philipp.reisner@linbit.com>.
  Copyright (C) 2001-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

  drbd is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  drbd is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with drbd; see the file COPYING.  If not, write to
  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

*/
#ifndef DRBD_H
#define DRBD_H
#include <linux/connector.h>
#include <asm/types.h>

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/byteorder.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>

/* Altough the Linux source code makes a difference between
   generic endianness and the bitfields' endianness, there is no
   architecture as of Linux-2.6.24-rc4 where the bitfileds' endianness
   does not match the generic endianness. */

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define __LITTLE_ENDIAN_BITFIELD
#elif __BYTE_ORDER == __BIG_ENDIAN
#define __BIG_ENDIAN_BITFIELD
#else
# error "sorry, weird endianness on this box"
#endif

#endif


extern const char *drbd_buildtag(void);
#define REL_VERSION "8.3.8"
#define API_VERSION 88
#define PRO_VERSION_MIN 86
#define PRO_VERSION_MAX 94


enum drbd_io_error_p {
	EP_PASS_ON, /* FIXME should the better be named "Ignore"? */
	EP_CALL_HELPER,
	EP_DETACH
};

enum drbd_fencing_p {
	FP_DONT_CARE,
	FP_RESOURCE,
	FP_STONITH
};

enum drbd_disconnect_p {
	DP_RECONNECT,
	DP_DROP_NET_CONF,
	DP_FREEZE_IO
};

enum drbd_after_sb_p {
	ASB_DISCONNECT,
	ASB_DISCARD_YOUNGER_PRI,
	ASB_DISCARD_OLDER_PRI,
	ASB_DISCARD_ZERO_CHG,
	ASB_DISCARD_LEAST_CHG,
	ASB_DISCARD_LOCAL,
	ASB_DISCARD_REMOTE,
	ASB_CONSENSUS,
	ASB_DISCARD_SECONDARY,
	ASB_CALL_HELPER,
	ASB_VIOLENTLY
};

/* KEEP the order, do not delete or insert. Only append. */
enum drbd_ret_codes {
	ERR_CODE_BASE		= 100,
	NO_ERROR		= 101,
	ERR_LOCAL_ADDR		= 102,
	ERR_PEER_ADDR		= 103,
	ERR_OPEN_DISK		= 104,
	ERR_OPEN_MD_DISK	= 105,
	ERR_DISK_NOT_BDEV	= 107,
	ERR_MD_NOT_BDEV		= 108,
	ERR_DISK_TO_SMALL	= 111,
	ERR_MD_DISK_TO_SMALL	= 112,
	ERR_BDCLAIM_DISK	= 114,
	ERR_BDCLAIM_MD_DISK	= 115,
	ERR_MD_IDX_INVALID	= 116,
	ERR_IO_MD_DISK		= 118,
	ERR_MD_INVALID          = 119,
	ERR_AUTH_ALG		= 120,
	ERR_AUTH_ALG_ND		= 121,
	ERR_NOMEM		= 122,
	ERR_DISCARD		= 123,
	ERR_DISK_CONFIGURED	= 124,
	ERR_NET_CONFIGURED	= 125,
	ERR_MANDATORY_TAG	= 126,
	ERR_MINOR_INVALID	= 127,
	ERR_INTR		= 129, /* EINTR */
	ERR_RESIZE_RESYNC	= 130,
	ERR_NO_PRIMARY		= 131,
	ERR_SYNC_AFTER		= 132,
	ERR_SYNC_AFTER_CYCLE	= 133,
	ERR_PAUSE_IS_SET	= 134,
	ERR_PAUSE_IS_CLEAR	= 135,
	ERR_PACKET_NR		= 137,
	ERR_NO_DISK		= 138,
	ERR_NOT_PROTO_C		= 139,
	ERR_NOMEM_BITMAP	= 140,
	ERR_INTEGRITY_ALG	= 141, /* DRBD 8.2 only */
	ERR_INTEGRITY_ALG_ND	= 142, /* DRBD 8.2 only */
	ERR_CPU_MASK_PARSE	= 143, /* DRBD 8.2 only */
	ERR_CSUMS_ALG		= 144, /* DRBD 8.2 only */
	ERR_CSUMS_ALG_ND	= 145, /* DRBD 8.2 only */
	ERR_VERIFY_ALG		= 146, /* DRBD 8.2 only */
	ERR_VERIFY_ALG_ND	= 147, /* DRBD 8.2 only */
	ERR_CSUMS_RESYNC_RUNNING= 148, /* DRBD 8.2 only */
	ERR_VERIFY_RUNNING	= 149, /* DRBD 8.2 only */
	ERR_DATA_NOT_CURRENT	= 150,
	ERR_CONNECTED		= 151, /* DRBD 8.3 only */
	ERR_PERM		= 152,
	ERR_NEED_APV_93		= 153,

	/* insert new ones above this line */
	AFTER_LAST_ERR_CODE
};

#define DRBD_PROT_A   1
#define DRBD_PROT_B   2
#define DRBD_PROT_C   3

enum drbd_role {
	R_UNKNOWN = 0,
	R_PRIMARY = 1,     /* role */
	R_SECONDARY = 2,   /* role */
	R_MASK = 3,
};

/* The order of these constants is important.
 * The lower ones (<C_WF_REPORT_PARAMS) indicate
 * that there is no socket!
 * >=C_WF_REPORT_PARAMS ==> There is a socket
 */
enum drbd_conns {
	C_STANDALONE,
	C_DISCONNECTING,  /* Temporal state on the way to StandAlone. */
	C_UNCONNECTED,    /* >= C_UNCONNECTED -> inc_net() succeeds */

	/* These temporal states are all used on the way
	 * from >= C_CONNECTED to Unconnected.
	 * The 'disconnect reason' states
	 * I do not allow to change beween them. */
	C_TIMEOUT,
	C_BROKEN_PIPE,
	C_NETWORK_FAILURE,
	C_PROTOCOL_ERROR,
	C_TEAR_DOWN,

	C_WF_CONNECTION,
	C_WF_REPORT_PARAMS, /* we have a socket */
	C_CONNECTED,      /* we have introduced each other */
	C_STARTING_SYNC_S,  /* starting full sync by admin request. */
	C_STARTING_SYNC_T,  /* stariing full sync by admin request. */
	C_WF_BITMAP_S,
	C_WF_BITMAP_T,
	C_WF_SYNC_UUID,

	/* All SyncStates are tested with this comparison
	 * xx >= C_SYNC_SOURCE && xx <= C_PAUSED_SYNC_T */
	C_SYNC_SOURCE,
	C_SYNC_TARGET,
	C_VERIFY_S,
	C_VERIFY_T,
	C_PAUSED_SYNC_S,
	C_PAUSED_SYNC_T,
	C_MASK = 31
};

enum drbd_disk_state {
	D_DISKLESS,
	D_ATTACHING,      /* In the process of reading the meta-data */
	D_FAILED,         /* Becomes D_DISKLESS as soon as we told it the peer */
			/* when >= D_FAILED it is legal to access mdev->bc */
	D_NEGOTIATING,    /* Late attaching state, we need to talk to the peer */
	D_INCONSISTENT,
	D_OUTDATED,
	D_UNKNOWN,       /* Only used for the peer, never for myself */
	D_CONSISTENT,     /* Might be D_OUTDATED, might be D_UP_TO_DATE ... */
	D_UP_TO_DATE,       /* Only this disk state allows applications' IO ! */
	D_MASK = 15
};

union drbd_state {
/* According to gcc's docs is the ...
 * The order of allocation of bit-fields within a unit (C90 6.5.2.1, C99 6.7.2.1).
 * Determined by ABI.
 * pointed out by Maxim Uvarov q<muvarov@ru.mvista.com>
 * even though we transmit as "cpu_to_be32(state)",
 * the offsets of the bitfields still need to be swapped
 * on different endianess.
 */
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned role:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned peer:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned conn:5 ;   /* 17/32	 cstates */
		unsigned disk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned pdsk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned susp:1 ;   /* 2/2	 IO suspended  no/yes */
		unsigned aftr_isp:1 ; /* isp .. imposed sync pause */
		unsigned peer_isp:1 ;
		unsigned user_isp:1 ;
		unsigned _pad:11;   /* 0	 unused */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned _pad:11;   /* 0	 unused */
		unsigned user_isp:1 ;
		unsigned peer_isp:1 ;
		unsigned aftr_isp:1 ; /* isp .. imposed sync pause */
		unsigned susp:1 ;   /* 2/2	 IO suspended  no/yes */
		unsigned pdsk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned disk:4 ;   /* 8/16	 from D_DISKLESS to D_UP_TO_DATE */
		unsigned conn:5 ;   /* 17/32	 cstates */
		unsigned peer:2 ;   /* 3/4	 primary/secondary/unknown */
		unsigned role:2 ;   /* 3/4	 primary/secondary/unknown */
#else
# error "this endianess is not supported"
#endif
	};
	unsigned int i;
};

enum drbd_state_ret_codes {
	SS_CW_NO_NEED = 4,
	SS_CW_SUCCESS = 3,
	SS_NOTHING_TO_DO = 2,
	SS_SUCCESS = 1,
	SS_UNKNOWN_ERROR = 0, /* Used to sleep longer in _drbd_request_state */
	SS_TWO_PRIMARIES = -1,
	SS_NO_UP_TO_DATE_DISK = -2,
	SS_NO_LOCAL_DISK = -4,
	SS_NO_REMOTE_DISK = -5,
	SS_CONNECTED_OUTDATES = -6,
	SS_PRIMARY_NOP = -7,
	SS_RESYNC_RUNNING = -8,
	SS_ALREADY_STANDALONE = -9,
	SS_CW_FAILED_BY_PEER = -10,
	SS_IS_DISKLESS = -11,
	SS_DEVICE_IN_USE = -12,
	SS_NO_NET_CONFIG = -13,
	SS_NO_VERIFY_ALG = -14,       /* drbd-8.2 only */
	SS_NEED_CONNECTION = -15,    /* drbd-8.2 only */
	SS_LOWER_THAN_OUTDATED = -16,
	SS_NOT_SUPPORTED = -17,      /* drbd-8.2 only */
	SS_IN_TRANSIENT_STATE = -18,  /* Retry after the next state change */
	SS_CONCURRENT_ST_CHG = -19,   /* Concurrent cluster side state change! */
	SS_AFTER_LAST_ERROR = -20,    /* Keep this at bottom */
};

/* from drbd_strings.c */
extern const char *drbd_conn_str(enum drbd_conns);
extern const char *drbd_role_str(enum drbd_role);
extern const char *drbd_disk_str(enum drbd_disk_state);
extern const char *drbd_set_st_err_str(enum drbd_state_ret_codes);

#define SHARED_SECRET_MAX 64

#define MDF_CONSISTENT		(1 << 0)
#define MDF_PRIMARY_IND		(1 << 1)
#define MDF_CONNECTED_IND	(1 << 2)
#define MDF_FULL_SYNC		(1 << 3)
#define MDF_WAS_UP_TO_DATE	(1 << 4)
#define MDF_PEER_OUT_DATED	(1 << 5)
#define MDF_CRASHED_PRIMARY     (1 << 6)

enum drbd_uuid_index {
	UI_CURRENT,
	UI_BITMAP,
	UI_HISTORY_START,
	UI_HISTORY_END,
	UI_SIZE,      /* nl-packet: number of dirty bits */
	UI_FLAGS,     /* nl-packet: flags */
	UI_EXTENDED_SIZE   /* Everything. */
};

enum drbd_timeout_flag {
	UT_DEFAULT      = 0,
	UT_DEGRADED     = 1,
	UT_PEER_OUTDATED = 2,
};

#define UUID_JUST_CREATED ((__u64)4)

#define DRBD_MAGIC 0x83740267
#define BE_DRBD_MAGIC __constant_cpu_to_be32(DRBD_MAGIC)

/* these are of type "int" */
#define DRBD_MD_INDEX_INTERNAL -1
#define DRBD_MD_INDEX_FLEX_EXT -2
#define DRBD_MD_INDEX_FLEX_INT -3

/* Start of the new netlink/connector stuff */

#define DRBD_NL_CREATE_DEVICE 0x01
#define DRBD_NL_SET_DEFAULTS  0x02


/* For searching a vacant cn_idx value */
#define CN_IDX_STEP			6977

struct drbd_nl_cfg_req {
	int packet_type;
	unsigned int drbd_minor;
	int flags;
	unsigned short tag_list[];
};

struct drbd_nl_cfg_reply {
	int packet_type;
	unsigned int minor;
	int ret_code; /* enum ret_code or set_st_err_t */
	unsigned short tag_list[]; /* only used with get_* calls */
};

#endif
