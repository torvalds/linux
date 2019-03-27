/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * Copyright (c) 2014-2015 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_io.h#5 $
 * $FreeBSD$
 */
/*
 * CAM Target Layer data movement structures/interface.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_IO_H_
#define	_CTL_IO_H_

#define	CTL_MAX_CDBLEN	32
/*
 * Uncomment this next line to enable printing out times for I/Os
 * that take longer than CTL_TIME_IO_SECS seconds to get to the datamove
 * and/or done stage.
 */
#define	CTL_TIME_IO
#ifdef  CTL_TIME_IO
#define	CTL_TIME_IO_DEFAULT_SECS	90
#endif

/*
 * Uncomment this next line to enable the CTL I/O delay feature.  You
 * can delay I/O at two different points -- datamove and done.  This is
 * useful for diagnosing abort conditions (for hosts that send an abort on a
 * timeout), and for determining how long a host's timeout is.
 */
//#define	CTL_IO_DELAY

typedef enum {
	CTL_STATUS_NONE,	/* No status */
	CTL_SUCCESS,		/* Transaction completed successfully */
	CTL_CMD_TIMEOUT,	/* Command timed out, shouldn't happen here */
	CTL_SEL_TIMEOUT,	/* Selection timeout, shouldn't happen here */
	CTL_ERROR,		/* General CTL error XXX expand on this? */
	CTL_SCSI_ERROR,		/* SCSI error, look at status byte/sense data */
	CTL_CMD_ABORTED,	/* Command aborted, don't return status */
	CTL_STATUS_MASK = 0xfff,/* Mask off any status flags */
	CTL_AUTOSENSE = 0x1000	/* Autosense performed */
} ctl_io_status;

/*
 * WARNING:  Keep the data in/out/none flags where they are.  They're used
 * in conjunction with ctl_cmd_flags.  See comment above ctl_cmd_flags
 * definition in ctl_private.h.
 */
typedef enum {
	CTL_FLAG_NONE		= 0x00000000,	/* no flags */
	CTL_FLAG_DATA_IN	= 0x00000001,	/* DATA IN */
	CTL_FLAG_DATA_OUT	= 0x00000002,	/* DATA OUT */
	CTL_FLAG_DATA_NONE	= 0x00000003,	/* no data */
	CTL_FLAG_DATA_MASK	= 0x00000003,
	CTL_FLAG_DO_AUTOSENSE	= 0x00000020,	/* grab sense info */
	CTL_FLAG_USER_REQ	= 0x00000040,	/* request came from userland */
	CTL_FLAG_ALLOCATED	= 0x00000100,	/* data space allocated */
	CTL_FLAG_ABORT_STATUS	= 0x00000400,	/* return TASK ABORTED status */
	CTL_FLAG_ABORT		= 0x00000800,	/* this I/O should be aborted */
	CTL_FLAG_DMA_INPROG	= 0x00001000,	/* DMA in progress */
	CTL_FLAG_DELAY_DONE	= 0x00004000,	/* delay injection done */
	CTL_FLAG_INT_COPY	= 0x00008000,	/* internal copy, no done call*/
	CTL_FLAG_SENT_2OTHER_SC	= 0x00010000,
	CTL_FLAG_FROM_OTHER_SC	= 0x00020000,
	CTL_FLAG_IS_WAS_ON_RTR  = 0x00040000,	/* Don't rerun cmd on failover*/
	CTL_FLAG_BUS_ADDR	= 0x00080000,	/* ctl_sglist contains BUS
						   addresses, not virtual ones*/
	CTL_FLAG_IO_CONT	= 0x00100000,	/* Continue I/O instead of
						   completing */
#if 0
	CTL_FLAG_ALREADY_DONE	= 0x00200000	/* I/O already completed */
#endif
	CTL_FLAG_NO_DATAMOVE	= 0x00400000,
	CTL_FLAG_DMA_QUEUED	= 0x00800000,	/* DMA queued but not started*/
	CTL_FLAG_STATUS_QUEUED	= 0x01000000,	/* Status queued but not sent*/

	CTL_FLAG_FAILOVER	= 0x04000000,	/* Killed by a failover */
	CTL_FLAG_IO_ACTIVE	= 0x08000000,	/* I/O active on this SC */
	CTL_FLAG_STATUS_SENT	= 0x10000000,	/* Status sent by datamove */
	CTL_FLAG_SERSEQ_DONE	= 0x20000000	/* All storage I/O started */
} ctl_io_flags;


struct ctl_lba_len {
	uint64_t lba;
	uint32_t len;
};

struct ctl_lba_len_flags {
	uint64_t lba;
	uint32_t len;
	uint32_t flags;
#define CTL_LLF_FUA	0x04000000
#define CTL_LLF_DPO	0x08000000
#define CTL_LLF_READ	0x10000000
#define CTL_LLF_WRITE	0x20000000
#define CTL_LLF_VERIFY	0x40000000
#define CTL_LLF_COMPARE	0x80000000
};

struct ctl_ptr_len_flags {
	uint8_t		*ptr;
	uint32_t	len;
	uint32_t	flags;
};

union ctl_priv {
	uint8_t		bytes[sizeof(uint64_t) * 2];
	uint64_t	integer;
	uint64_t	integers[2];
	void		*ptr;
	void		*ptrs[2];
};

/*
 * Number of CTL private areas.
 */
#define	CTL_NUM_PRIV	6

/*
 * Which private area are we using for a particular piece of data?
 */
#define	CTL_PRIV_LUN		0	/* CTL LUN pointer goes here */
#define	CTL_PRIV_LBA_LEN	1	/* Decoded LBA/len for read/write*/
#define	CTL_PRIV_MODEPAGE	1	/* Modepage info for config write */
#define	CTL_PRIV_BACKEND	2	/* Reserved for block, RAIDCore */
#define	CTL_PRIV_BACKEND_LUN	3	/* Backend LUN pointer */
#define	CTL_PRIV_FRONTEND	4	/* Frontend storage */
#define	CTL_PRIV_FRONTEND2	5	/* Another frontend storage */

#define CTL_LUN(io)	((io)->io_hdr.ctl_private[CTL_PRIV_LUN].ptrs[0])
#define CTL_SOFTC(io)	((io)->io_hdr.ctl_private[CTL_PRIV_LUN].ptrs[1])
#define CTL_BACKEND_LUN(io)	((io)->io_hdr.ctl_private[CTL_PRIV_BACKEND_LUN].ptrs[0])
#define CTL_PORT(io)	(((struct ctl_softc *)CTL_SOFTC(io))->	\
    ctl_ports[(io)->io_hdr.nexus.targ_port])

/*
 * These are used only on Originating SC in XFER mode, where requests don't
 * ever reach backends, so we can reuse backend's private storage.
 */
#define CTL_RSGL(io)	((io)->io_hdr.ctl_private[CTL_PRIV_BACKEND].ptrs[0])
#define CTL_LSGL(io)	((io)->io_hdr.ctl_private[CTL_PRIV_BACKEND].ptrs[1])
#define CTL_RSGLT(io)	((struct ctl_sg_entry *)CTL_RSGL(io))
#define CTL_LSGLT(io)	((struct ctl_sg_entry *)CTL_LSGL(io))

#define CTL_INVALID_PORTNAME 0xFF
#define CTL_UNMAPPED_IID     0xFF

struct ctl_sg_entry {
	void	*addr;
	size_t	len;
};

typedef enum {
	CTL_IO_NONE,
	CTL_IO_SCSI,
	CTL_IO_TASK,
} ctl_io_type;

struct ctl_nexus {
	uint32_t initid;		/* Initiator ID */
	uint32_t targ_port;		/* Target port, filled in by PORT */
	uint32_t targ_lun;		/* Destination lun */
	uint32_t targ_mapped_lun;	/* Destination lun CTL-wide */
};

typedef enum {
	CTL_MSG_SERIALIZE,
	CTL_MSG_R2R,
	CTL_MSG_FINISH_IO,
	CTL_MSG_BAD_JUJU,
	CTL_MSG_MANAGE_TASKS,
	CTL_MSG_PERS_ACTION,
	CTL_MSG_DATAMOVE,
	CTL_MSG_DATAMOVE_DONE,
	CTL_MSG_UA,			/* Set/clear UA on secondary. */
	CTL_MSG_PORT_SYNC,		/* Information about port. */
	CTL_MSG_LUN_SYNC,		/* Information about LUN. */
	CTL_MSG_IID_SYNC,		/* Information about initiator. */
	CTL_MSG_LOGIN,			/* Information about HA peer. */
	CTL_MSG_MODE_SYNC,		/* Mode page current content. */
	CTL_MSG_FAILOVER		/* Fake, never sent though the wire */
} ctl_msg_type;

struct ctl_scsiio;

struct ctl_io_hdr {
	uint32_t	  version;	/* interface version XXX */
	ctl_io_type	  io_type;	/* task I/O, SCSI I/O, etc. */
	ctl_msg_type	  msg_type;
	struct ctl_nexus  nexus;	/* Initiator, port, target, lun */
	uint32_t	  iid_indx;	/* the index into the iid mapping */
	uint32_t	  flags;	/* transaction flags */
	uint32_t	  status;	/* transaction status */
	uint32_t	  port_status;	/* trans status, set by PORT, 0 = good*/
	uint32_t	  timeout;	/* timeout in ms */
	uint32_t	  retries;	/* retry count */
#ifdef CTL_IO_DELAY
	struct callout	  delay_callout;
#endif /* CTL_IO_DELAY */
#ifdef CTL_TIME_IO
	time_t		  start_time;	/* I/O start time */
	struct bintime	  start_bt;	/* Timer start ticks */
	struct bintime	  dma_start_bt;	/* DMA start ticks */
	struct bintime	  dma_bt;	/* DMA total ticks */
#endif /* CTL_TIME_IO */
	uint32_t	  num_dmas;	/* Number of DMAs */
	union ctl_io	  *remote_io;	/* I/O counterpart on remote HA side */
	union ctl_io	  *blocker;	/* I/O blocking this one */
	void		  *pool;	/* I/O pool */
	union ctl_priv	  ctl_private[CTL_NUM_PRIV];/* CTL private area */
	TAILQ_HEAD(, ctl_io_hdr) blocked_queue;	/* I/Os blocked by this one */
	STAILQ_ENTRY(ctl_io_hdr) links;	/* linked list pointer */
	TAILQ_ENTRY(ctl_io_hdr) ooa_links;	/* ooa_queue links */
	TAILQ_ENTRY(ctl_io_hdr) blocked_links;	/* blocked_queue links */
};

typedef enum {
	CTL_TAG_UNTAGGED,
	CTL_TAG_SIMPLE,
	CTL_TAG_ORDERED,
	CTL_TAG_HEAD_OF_QUEUE,
	CTL_TAG_ACA
} ctl_tag_type;

union ctl_io;

/*
 * SCSI passthrough I/O structure for the CAM Target Layer.  Note
 * that some of these fields are here for completeness, but they aren't
 * used in the CTL implementation.  e.g., timeout and retries won't be
 * used.
 *
 * Note:  Make sure the io_hdr is *always* the first element in this
 * structure.
 */
struct ctl_scsiio {
	struct ctl_io_hdr io_hdr;	/* common to all I/O types */

	/*
	 * The ext_* fields are generally intended for frontend use; CTL itself
	 * doesn't modify or use them.
	 */
	uint32_t   ext_sg_entries;	/* 0 = no S/G list, > 0 = num entries */
	uint8_t	   *ext_data_ptr;	/* data buffer or S/G list */
	uint32_t   ext_data_len;	/* Data transfer length */
	uint32_t   ext_data_filled;	/* Amount of data filled so far */

	/*
	 * The number of scatter/gather entries in the list pointed to
	 * by kern_data_ptr.  0 means there is no list, just a data pointer.
	 */
	uint32_t   kern_sg_entries;

	uint32_t   rem_sg_entries;	/* Unused. */

	/*
	 * The data pointer or a pointer to the scatter/gather list.
	 */
	uint8_t    *kern_data_ptr;

	/*
	 * Length of the data buffer or scatter/gather list.  It's also
	 * the length of this particular piece of the data transfer,
	 * ie. number of bytes expected to be transferred by the current
	 * invocation of frontend's datamove() callback.  It's always
	 * less than or equal to kern_total_len.
	 */
	uint32_t   kern_data_len;

	/*
	 * Total length of data to be transferred during this particular
	 * SCSI command, as decoded from SCSI CDB.
	 */
	uint32_t   kern_total_len;

	/*
	 * Amount of data left after the current data transfer.
	 */
	uint32_t   kern_data_resid;

	/*
	 * Byte offset of this transfer, equal to the amount of data
	 * already transferred for this SCSI command during previous
	 * datamove() invocations.
	 */
	uint32_t   kern_rel_offset;

	struct     scsi_sense_data sense_data;	/* sense data */
	uint8_t	   sense_len;		/* Returned sense length */
	uint8_t	   scsi_status;		/* SCSI status byte */
	uint8_t	   sense_residual;	/* Unused. */
	uint32_t   residual;		/* Unused */
	uint32_t   tag_num;		/* tag number */
	ctl_tag_type tag_type;		/* simple, ordered, head of queue,etc.*/
	uint8_t    cdb_len;		/* CDB length */
	uint8_t	   cdb[CTL_MAX_CDBLEN];	/* CDB */
	int	   (*be_move_done)(union ctl_io *io); /* called by fe */
	int        (*io_cont)(union ctl_io *io); /* to continue processing */
};

typedef enum {
	CTL_TASK_ABORT_TASK,
	CTL_TASK_ABORT_TASK_SET,
	CTL_TASK_CLEAR_ACA,
	CTL_TASK_CLEAR_TASK_SET,
	CTL_TASK_I_T_NEXUS_RESET,
	CTL_TASK_LUN_RESET,
	CTL_TASK_TARGET_RESET,
	CTL_TASK_BUS_RESET,
	CTL_TASK_PORT_LOGIN,
	CTL_TASK_PORT_LOGOUT,
	CTL_TASK_QUERY_TASK,
	CTL_TASK_QUERY_TASK_SET,
	CTL_TASK_QUERY_ASYNC_EVENT
} ctl_task_type;

typedef enum {
	CTL_TASK_FUNCTION_COMPLETE,
	CTL_TASK_FUNCTION_SUCCEEDED,
	CTL_TASK_FUNCTION_REJECTED,
	CTL_TASK_LUN_DOES_NOT_EXIST,
	CTL_TASK_FUNCTION_NOT_SUPPORTED
} ctl_task_status;

/*
 * Task management I/O structure.  Aborts, bus resets, etc., are sent using
 * this structure.
 *
 * Note:  Make sure the io_hdr is *always* the first element in this
 * structure.
 */
struct ctl_taskio {
	struct ctl_io_hdr	io_hdr;      /* common to all I/O types */
	ctl_task_type		task_action; /* Target Reset, Abort, etc.  */
	uint32_t		tag_num;     /* tag number */
	ctl_tag_type		tag_type;    /* simple, ordered, etc. */
	uint8_t			task_status; /* Complete, Succeeded, etc. */
	uint8_t			task_resp[3];/* Response information */
};


/*
 * HA link messages.
 */
#define	CTL_HA_VERSION		3

/*
 * Used for CTL_MSG_LOGIN.
 */
struct ctl_ha_msg_login {
	ctl_msg_type		msg_type;
	int			version;
	int			ha_mode;
	int			ha_id;
	int			max_luns;
	int			max_ports;
	int			max_init_per_port;
};

typedef enum {
	CTL_PR_REG_KEY,
	CTL_PR_UNREG_KEY,
	CTL_PR_PREEMPT,
	CTL_PR_CLEAR,
	CTL_PR_RESERVE,
	CTL_PR_RELEASE
} ctl_pr_action;

/*
 * The PR info is specifically for sending Persistent Reserve actions
 * to the other SC which it must also act on.
 *
 * Note:  Make sure the io_hdr is *always* the first element in this
 * structure.
 */
struct ctl_pr_info {
	ctl_pr_action		action;
	uint8_t			sa_res_key[8];
	uint8_t			res_type;
	uint32_t		residx;
};

struct ctl_ha_msg_hdr {
	ctl_msg_type		msg_type;
	uint32_t		status;	     /* transaction status */
	union ctl_io		*original_sc;
	union ctl_io		*serializing_sc;
	struct ctl_nexus	nexus;	     /* Initiator, port, target, lun */
};

#define	CTL_HA_MAX_SG_ENTRIES	16
#define	CTL_HA_DATAMOVE_SEGMENT	131072

/*
 * Used for CTL_MSG_PERS_ACTION.
 */
struct ctl_ha_msg_pr {
	struct ctl_ha_msg_hdr	hdr;
	struct ctl_pr_info	pr_info;
};

/*
 * Used for CTL_MSG_UA.
 */
struct ctl_ha_msg_ua {
	struct ctl_ha_msg_hdr	hdr;
	int			ua_all;
	int			ua_set;
	int			ua_type;
	uint8_t			ua_info[8];
};

/*
 * The S/G handling here is a little different than the standard ctl_scsiio
 * structure, because we can't pass data by reference in between controllers.
 * The S/G list in the ctl_scsiio struct is normally passed in the
 * kern_data_ptr field.  So kern_sg_entries here will always be non-zero,
 * even if there is only one entry.
 *
 * Used for CTL_MSG_DATAMOVE.
 */
struct ctl_ha_msg_dt {
	struct ctl_ha_msg_hdr	hdr;
	ctl_io_flags		flags;  /* Only I/O flags are used here */
	uint32_t		sg_sequence;     /* S/G portion number  */
	uint8_t			sg_last;         /* last S/G batch = 1 */
	uint32_t		sent_sg_entries; /* previous S/G count */
	uint32_t		cur_sg_entries;  /* current S/G entries */
	uint32_t		kern_sg_entries; /* total S/G entries */
	uint32_t		kern_data_len;   /* Length of this S/G list */
	uint32_t		kern_total_len;  /* Total length of this
						    transaction */
	uint32_t		kern_data_resid; /* Length left to transfer
						    after this*/
	uint32_t		kern_rel_offset; /* Byte Offset of this
						    transfer */
	struct ctl_sg_entry	sg_list[CTL_HA_MAX_SG_ENTRIES];
};

/*
 * Used for CTL_MSG_SERIALIZE, CTL_MSG_FINISH_IO, CTL_MSG_BAD_JUJU,
 * and CTL_MSG_DATAMOVE_DONE.
 */
struct ctl_ha_msg_scsi {
	struct ctl_ha_msg_hdr	hdr;
	uint32_t		tag_num;     /* tag number */
	ctl_tag_type		tag_type;    /* simple, ordered, etc. */
	uint8_t			cdb[CTL_MAX_CDBLEN];	/* CDB */
	uint8_t			cdb_len;	/* CDB length */
	uint8_t			scsi_status; /* SCSI status byte */
	uint8_t			sense_len;   /* Returned sense length */
	uint32_t		port_status; /* trans status, set by FETD,
						0 = good*/
	uint32_t		kern_data_resid; /* for DATAMOVE_DONE */
	struct scsi_sense_data	sense_data;  /* sense data */
};

/* 
 * Used for CTL_MSG_MANAGE_TASKS.
 */
struct ctl_ha_msg_task {
	struct ctl_ha_msg_hdr	hdr;
	ctl_task_type		task_action; /* Target Reset, Abort, etc.  */
	uint32_t		tag_num;     /* tag number */
	ctl_tag_type		tag_type;    /* simple, ordered, etc. */
};

/*
 * Used for CTL_MSG_PORT_SYNC.
 */
struct ctl_ha_msg_port {
	struct ctl_ha_msg_hdr	hdr;
	int			port_type;
	int			physical_port;
	int			virtual_port;
	int			status;
	int			name_len;
	int			lun_map_len;
	int			port_devid_len;
	int			target_devid_len;
	int			init_devid_len;
	uint8_t			data[];
};

/*
 * Used for CTL_MSG_LUN_SYNC.
 */
struct ctl_ha_msg_lun {
	struct ctl_ha_msg_hdr	hdr;
	int			flags;
	unsigned int		pr_generation;
	uint32_t		pr_res_idx;
	uint8_t			pr_res_type;
	int			lun_devid_len;
	int			pr_key_count;
	uint8_t			data[];
};

struct ctl_ha_msg_lun_pr_key {
	uint32_t		pr_iid;
	uint64_t		pr_key;
};

/*
 * Used for CTL_MSG_IID_SYNC.
 */
struct ctl_ha_msg_iid {
	struct ctl_ha_msg_hdr	hdr;
	int			in_use;
	int			name_len;
	uint64_t		wwpn;
	uint8_t			data[];
};

/*
 * Used for CTL_MSG_MODE_SYNC.
 */
struct ctl_ha_msg_mode {
	struct ctl_ha_msg_hdr	hdr;
	uint8_t			page_code;
	uint8_t			subpage;
	uint16_t		page_len;
	uint8_t			data[];
};

union ctl_ha_msg {
	struct ctl_ha_msg_hdr	hdr;
	struct ctl_ha_msg_task	task;
	struct ctl_ha_msg_scsi	scsi;
	struct ctl_ha_msg_dt	dt;
	struct ctl_ha_msg_pr	pr;
	struct ctl_ha_msg_ua	ua;
	struct ctl_ha_msg_port	port;
	struct ctl_ha_msg_lun	lun;
	struct ctl_ha_msg_iid	iid;
	struct ctl_ha_msg_login	login;
	struct ctl_ha_msg_mode	mode;
};

struct ctl_prio {
	struct ctl_io_hdr	io_hdr;
	struct ctl_ha_msg_pr	pr_msg;
};

union ctl_io {
	struct ctl_io_hdr	io_hdr;	/* common to all I/O types */
	struct ctl_scsiio	scsiio;	/* Normal SCSI commands */
	struct ctl_taskio	taskio;	/* SCSI task management/reset */
	struct ctl_prio		presio;	/* update per. res info on other SC */
};

#ifdef _KERNEL

union ctl_io *ctl_alloc_io(void *pool_ref);
union ctl_io *ctl_alloc_io_nowait(void *pool_ref);
void ctl_free_io(union ctl_io *io);
void ctl_zero_io(union ctl_io *io);

#endif /* _KERNEL */

#endif	/* _CTL_IO_H_ */

/*
 * vim: ts=8
 */
