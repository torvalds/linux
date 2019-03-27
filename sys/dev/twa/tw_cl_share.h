/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 * Modifications by: Manjunath Ranganathaiah
 */



#ifndef TW_CL_SHARE_H

#define TW_CL_SHARE_H


/*
 * Macros, structures and functions shared between OSL and CL,
 * and defined by CL.
 */

#define TW_CL_NULL			((TW_VOID *)0)
#define TW_CL_TRUE			1
#define TW_CL_FALSE			0

#define TW_CL_VENDOR_ID			0x13C1	/* 3ware vendor id */
#define TW_CL_DEVICE_ID_9K		0x1002	/* 9000 PCI series device id */
#define TW_CL_DEVICE_ID_9K_X		0x1003	/* 9000 PCI-X series device id */
#define TW_CL_DEVICE_ID_9K_E		0x1004  /* 9000 PCIe series device id */
#define TW_CL_DEVICE_ID_9K_SA		0x1005	/* 9000 PCIe SAS series device id */

#define TW_CL_BAR_TYPE_IO		1	/* I/O base address */
#define TW_CL_BAR_TYPE_MEM		2	/* memory base address */
#define TW_CL_BAR_TYPE_SBUF		3	/* SBUF base address */

#ifdef TW_OSL_ENCLOSURE_SUPPORT
#define TW_CL_MAX_NUM_UNITS		65	/* max # of units we support
						-- enclosure target id is 64 */
#else /* TW_OSL_ENCLOSURE_SUPPORT */
#define TW_CL_MAX_NUM_UNITS		32	/* max # of units we support */
#endif /* TW_OSL_ENCLOSURE_SUPPORT */

#define TW_CL_MAX_NUM_LUNS		255	/* max # of LUN's we support */
#define TW_CL_MAX_IO_SIZE		0x20000	/* 128K */

/*
 * Though we can support 256 simultaneous requests, we advertise as capable
 * of supporting only 255, since we want to keep one CL internal request
 * context packet always available for internal requests.
 */
#define TW_CL_MAX_SIMULTANEOUS_REQUESTS	256	/* max simult reqs supported */

#define TW_CL_MAX_32BIT_SG_ELEMENTS	109	/* max 32-bit sg elements */
#define TW_CL_MAX_64BIT_SG_ELEMENTS	72	/* max 64-bit sg elements */


/* Possible values of ctlr->flags */
#define TW_CL_64BIT_ADDRESSES	(1<<0) /* 64 bit cmdpkt & SG addresses */
#define TW_CL_64BIT_SG_LENGTH	(1<<1) /* 64 bit SG length */
#define TW_CL_START_CTLR_ONLY	(1<<2) /* Start ctlr only */
#define TW_CL_STOP_CTLR_ONLY	(1<<3) /* Stop ctlr only */
#define TW_CL_DEFERRED_INTR_USED (1<<5) /* OS Layer uses deferred intr */

/* Possible error values from the Common Layer. */
#define TW_CL_ERR_REQ_SUCCESS			0
#define TW_CL_ERR_REQ_GENERAL_FAILURE		(1<<0)
#define TW_CL_ERR_REQ_INVALID_TARGET		(1<<1)
#define TW_CL_ERR_REQ_INVALID_LUN		(1<<2)
#define TW_CL_ERR_REQ_SCSI_ERROR		(1<<3)
#define TW_CL_ERR_REQ_AUTO_SENSE_VALID		(1<<4)
#define TW_CL_ERR_REQ_BUS_RESET			(1<<5)
#define TW_CL_ERR_REQ_UNABLE_TO_SUBMIT_COMMAND	(1<<6)


/* Possible values of req_pkt->flags */
#define TW_CL_REQ_RETRY_ON_BUSY		(1<<0)
#define TW_CL_REQ_CALLBACK_FOR_SGLIST	(1<<1)


#define TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR	3
#define TW_CL_MESSAGE_SOURCE_CONTROLLER_EVENT	4
#define TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR	21
#define TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT	22
#define TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER	5
#define TW_CL_MESSAGE_SOURCE_FREEBSD_OS		8
#define TW_CL_MESSAGE_SOURCE_WINDOWS_DRIVER	7
#define TW_CL_MESSAGE_SOURCE_WINDOWS_OS		10

#define TW_CL_SEVERITY_ERROR		0x1
#define TW_CL_SEVERITY_WARNING		0x2
#define TW_CL_SEVERITY_INFO		0x3
#define TW_CL_SEVERITY_DEBUG		0x4

#define TW_CL_SEVERITY_ERROR_STRING	"ERROR"
#define TW_CL_SEVERITY_WARNING_STRING	"WARNING"
#define TW_CL_SEVERITY_INFO_STRING	"INFO"
#define TW_CL_SEVERITY_DEBUG_STRING	"DEBUG"



/*
 * Structure, a pointer to which is used as the controller handle in
 * communications between the OS Layer and the Common Layer.
 */
struct tw_cl_ctlr_handle {
	TW_VOID	*osl_ctlr_ctxt;	/* OSL's ctlr context */
	TW_VOID	*cl_ctlr_ctxt;	/* CL's ctlr context */
};


/*
 * Structure, a pointer to which is used as the request handle in
 * communications between the OS Layer and the Common Layer.
 */
struct tw_cl_req_handle {
	TW_VOID	*osl_req_ctxt;	/* OSL's request context */
	TW_VOID	*cl_req_ctxt;	/* CL's request context */
	TW_UINT8 is_io;		/* Only freeze/release simq for IOs */
};


/* Structure used to describe SCSI requests to CL. */
struct tw_cl_scsi_req_packet {
	TW_UINT32	unit;		/* unit # to send cmd to */
	TW_UINT32	lun;		/* LUN to send cmd to */
	TW_UINT8	*cdb;		/* ptr to SCSI cdb */
	TW_UINT32	cdb_len;	/* # of valid cdb bytes */
	TW_UINT32	sense_len;	/* # of bytes of valid sense info */
	TW_UINT8	*sense_data;	/* ptr to sense data, if any */
	TW_UINT32	scsi_status;	/* SCSI status returned by fw */
	TW_UINT32	sgl_entries;	/* # of SG descriptors */
	TW_UINT8	*sg_list;	/* ptr to SG list */
};


/* Structure used to describe pass through command packets to CL. */
struct tw_cl_passthru_req_packet {
	TW_UINT8	*cmd_pkt;	/* ptr to passthru cmd pkt */
	TW_UINT32	cmd_pkt_length;	/* size of cmd pkt */
	TW_UINT32	sgl_entries;	/* # of SG descriptors */
	TW_UINT8	*sg_list;	/* ptr to SG list */
};


/* Request packet submitted to the Common Layer, by the OS Layer. */
struct tw_cl_req_packet {
	TW_UINT32	cmd;		/* Common Layer cmd */
	TW_UINT32	flags;		/* flags describing request */
	TW_UINT32	status;		/* Common Layer returned status */
	TW_VOID		(*tw_osl_callback)(struct tw_cl_req_handle *req_handle);
			/* OSL routine to be called by CL on req completion */
	TW_VOID		(*tw_osl_sgl_callback)(
			struct tw_cl_req_handle *req_handle, TW_VOID *sg_list,
			TW_UINT32 *num_sgl_entries);
			/* OSL callback to get SG list. */

	union {
		struct tw_cl_scsi_req_packet		scsi_req; /* SCSI req */
		struct tw_cl_passthru_req_packet	pt_req;/*Passthru req*/
	} gen_req_pkt;
};


#pragma pack(1)
/*
 * Packet that describes an AEN/error generated by the controller,
 * Common Layer, or even the OS Layer.
 */
struct tw_cl_event_packet {
	TW_UINT32	sequence_id;
	TW_UINT32	time_stamp_sec;
	TW_UINT16	aen_code;
	TW_UINT8	severity;
	TW_UINT8	retrieved;
	TW_UINT8	repeat_count;
	TW_UINT8	parameter_len;
	TW_UINT8	parameter_data[98];
	TW_UINT32	event_src;
	TW_UINT8	severity_str[20];
};
#pragma pack()


/* Structure to link 2 adjacent elements in a list. */
struct tw_cl_link {
	struct tw_cl_link	*next;
	struct tw_cl_link	*prev;
};


#pragma pack(1)
/* Scatter/Gather list entry with 32 bit addresses. */
struct tw_cl_sg_desc32 {
	TW_UINT32	address;
	TW_UINT32	length;
};


/* Scatter/Gather list entry with 64 bit addresses. */
struct tw_cl_sg_desc64 {
	TW_UINT64	address;
	TW_UINT32	length;
};

#pragma pack()


/* Byte swap functions.  Valid only if running on big endian platforms. */
#ifdef TW_OSL_BIG_ENDIAN

#define TW_CL_SWAP16_WITH_CAST(x)					\
	((x << 8) | (x >> 8))


#define TW_CL_SWAP32_WITH_CAST(x)					\
	((x << 24) | ((x << 8) & (0xFF0000)) |				\
	((x >> 8) & (0xFF00)) | (x >> 24))


#define TW_CL_SWAP64_WITH_CAST(x)					\
	((((TW_UINT64)(TW_CL_SWAP32(((TW_UINT32 *)(&(x)))[1]))) << 32) |\
	((TW_UINT32)(TW_CL_SWAP32(((TW_UINT32 *)(&(x)))[0]))))


#else /* TW_OSL_BIG_ENDIAN */

#define TW_CL_SWAP16_WITH_CAST(x)	x
#define TW_CL_SWAP32_WITH_CAST(x)	x
#define TW_CL_SWAP64_WITH_CAST(x)	x

#endif /* TW_OSL_BIG_ENDIAN */

#define TW_CL_SWAP16(x)		TW_CL_SWAP16_WITH_CAST((TW_UINT16)(x))
#define TW_CL_SWAP32(x)		TW_CL_SWAP32_WITH_CAST((TW_UINT32)(x))
#define TW_CL_SWAP64(x)		TW_CL_SWAP64_WITH_CAST((TW_UINT64)(x))


/* Queue manipulation functions. */

/* Initialize a queue. */
#define TW_CL_Q_INIT(head)	do {		\
	(head)->prev = (head)->next = head;	\
} while (0)


/* Insert an item at the head of the queue. */
#define TW_CL_Q_INSERT_HEAD(head, item)	do {	\
	(item)->next = (head)->next;		\
	(item)->prev = head;			\
	(head)->next->prev = item;		\
	(head)->next = item;			\
} while (0)


/* Insert an item at the tail of the queue. */
#define	TW_CL_Q_INSERT_TAIL(head, item)	do {	\
	(item)->next = head;			\
	(item)->prev = (head)->prev;		\
	(head)->prev->next = item;		\
	(head)->prev = item;			\
} while (0)


/* Remove an item from the head of the queue. */
#define TW_CL_Q_REMOVE_ITEM(head, item)	do {	\
	(item)->prev->next = (item)->next;	\
	(item)->next->prev = (item)->prev;	\
} while (0)


/* Retrieve the item at the head of the queue. */
#define TW_CL_Q_FIRST_ITEM(head)		\
	(((head)->next != head) ? ((head)->next) : TW_CL_NULL)


/* Retrieve the item at the tail of the queue. */
#define TW_CL_Q_LAST_ITEM(head)			\
	(((head)->prev != head) ? ((head)->prev) : TW_CL_NULL)


/* Retrieve the item next to a given item in the queue. */
#define TW_CL_Q_NEXT_ITEM(head, item)		\
	(((item)->next != head) ? ((item)->next) : TW_CL_NULL)


/* Retrieve the item previous to a given item in the queue. */
#define TW_CL_Q_PREV_ITEM(head, item)		\
	(((item)->prev != head) ? ((item)->prev) : TW_CL_NULL)


/* Determine the offset of a field from the head of the structure it is in. */
#define	TW_CL_STRUCT_OFFSET(struct_type, field)	\
	(TW_INT8 *)(&((struct_type *)0)->field)


/*
 * Determine the address of the head of a structure, given the address of a
 * field within it.
 */
#define TW_CL_STRUCT_HEAD(addr, struct_type, field)	\
	(struct_type *)((TW_INT8 *)addr -		\
	TW_CL_STRUCT_OFFSET(struct_type, field))



#ifndef TW_BUILDING_API

#include "tw_osl_inline.h"



/*
 * The following are extern declarations of OS Layer defined functions called
 * by the Common Layer.  If any function has been defined as a macro in
 * tw_osl_share.h, we will not make the extern declaration here.
 */

#ifndef tw_osl_breakpoint
/* Allows setting breakpoints in the CL code for debugging purposes. */
extern TW_VOID	tw_osl_breakpoint(TW_VOID);
#endif


#ifndef tw_osl_timeout
/* Start OS timeout() routine after controller reset sequence */
extern TW_VOID	tw_osl_timeout(struct tw_cl_req_handle *req_handle);
#endif

#ifndef tw_osl_untimeout
/* Stop OS timeout() routine during controller reset sequence */
extern TW_VOID	tw_osl_untimeout(struct tw_cl_req_handle *req_handle);
#endif


#ifndef tw_osl_cur_func
/* Text name of current function. */
extern TW_INT8	*tw_osl_cur_func(TW_VOID);
#endif


#ifdef TW_OSL_DEBUG
#ifndef tw_osl_dbg_printf
/* Print to syslog/event log/debug console, as applicable. */
extern TW_INT32 tw_osl_dbg_printf(struct tw_cl_ctlr_handle *ctlr_handle,
	const TW_INT8 *fmt, ...);
#endif
#endif /* TW_OSL_DEBUG */


#ifndef tw_osl_delay
/* Cause a delay of usecs micro-seconds. */
extern TW_VOID	tw_osl_delay(TW_INT32 usecs);
#endif


#ifndef tw_osl_destroy_lock
/* Create/initialize a lock for CL's use. */
extern TW_VOID	tw_osl_destroy_lock(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_LOCK_HANDLE *lock);
#endif


#ifndef tw_osl_free_lock
/* Free a previously held lock. */
extern TW_VOID	tw_osl_free_lock(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_LOCK_HANDLE *lock);
#endif


#ifndef tw_osl_get_local_time
/* Get local time. */
extern TW_TIME	tw_osl_get_local_time(TW_VOID);
#endif


#ifndef tw_osl_get_lock
/* Acquire a lock. */
extern TW_VOID	tw_osl_get_lock(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_LOCK_HANDLE *lock);
#endif


#ifndef tw_osl_init_lock
/* Create/initialize a lock for CL's use. */
extern TW_VOID	tw_osl_init_lock(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_INT8 *lock_name, TW_LOCK_HANDLE *lock);
#endif


#ifndef tw_osl_memcpy
/* Copy 'size' bytes from 'src' to 'dest'. */
extern TW_VOID	tw_osl_memcpy(TW_VOID *src, TW_VOID *dest, TW_INT32 size);
#endif


#ifndef tw_osl_memzero
/* Zero 'size' bytes starting at 'addr'. */
extern TW_VOID	tw_osl_memzero(TW_VOID *addr, TW_INT32 size);
#endif


#ifndef tw_osl_notify_event
/* Notify OSL of a controller/CL (or even OSL) event. */
extern TW_VOID	tw_osl_notify_event(struct tw_cl_ctlr_handle *ctlr_handle,
	struct tw_cl_event_packet *event);
#endif


#ifdef TW_OSL_PCI_CONFIG_ACCESSIBLE
#ifndef tw_osl_read_pci_config
/* Read 'size' bytes from 'offset' in the PCI config space. */
extern TW_UINT32 tw_osl_read_pci_config(
	struct tw_cl_ctlr_handle *ctlr_handle, TW_INT32 offset, TW_INT32 size);
#endif
#endif /* TW_OSL_PCI_CONFIG_ACCESSIBLE */


#ifndef tw_osl_read_reg
/* Read 'size' bytes at 'offset' from base address of this controller. */
extern TW_UINT32 tw_osl_read_reg(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_INT32 offset, TW_INT32 size);
#endif


#ifndef tw_osl_scan_bus
/* Request OSL for a bus scan. */
extern TW_VOID	tw_osl_scan_bus(struct tw_cl_ctlr_handle *ctlr_handle);
#endif


#ifdef TW_OSL_CAN_SLEEP
#ifndef tw_osl_sleep
/* Sleep for 'timeout' ms or until woken up (by tw_osl_wakeup). */
extern TW_INT32	tw_osl_sleep(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_SLEEP_HANDLE *sleep_handle, TW_INT32 timeout);
#endif
#endif /* TW_OSL_CAN_SLEEP */


#ifndef tw_osl_sprintf
/* Standard sprintf. */
extern TW_INT32	tw_osl_sprintf(TW_INT8 *dest, const TW_INT8 *fmt, ...);
#endif


#ifndef tw_osl_strcpy
/* Copy string 'src' to 'dest'. */
extern TW_INT8	*tw_osl_strcpy(TW_INT8 *dest, TW_INT8 *src);
#endif


#ifndef tw_osl_strlen
/* Return length of string pointed at by 'str'. */
extern TW_INT32	tw_osl_strlen(TW_VOID *str);
#endif

#ifndef tw_osl_vsprintf
/* Standard vsprintf. */
extern TW_INT32	tw_osl_vsprintf(TW_INT8 *dest, const TW_INT8 *fmt, va_list ap);
#endif


#ifdef TW_OSL_CAN_SLEEP
#ifndef tw_osl_wakeup
/* Wake up a thread sleeping by a call to tw_osl_sleep. */
extern TW_VOID	tw_osl_wakeup(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_SLEEP_HANDLE *sleep_handle);
#endif
#endif /* TW_OSL_CAN_SLEEP */


#ifdef TW_OSL_PCI_CONFIG_ACCESSIBLE
#ifndef tw_osl_write_pci_config
/* Write 'value' of 'size' bytes at 'offset' in the PCI config space. */
extern TW_VOID	tw_osl_write_pci_config(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_INT32 offset, TW_INT32 value, TW_INT32 size);
#endif
#endif /* TW_OSL_PCI_CONFIG_ACCESSIBLE */


#ifndef tw_osl_write_reg
/*
 * Write 'value' of 'size' (max 4) bytes at 'offset' from base address of
 * this controller.
 */
extern TW_VOID	tw_osl_write_reg(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_INT32 offset, TW_INT32 value, TW_INT32 size);
#endif



/* Functions in the Common Layer */

/* Creates and queues AEN's.  Also notifies OS Layer. */
extern TW_VOID tw_cl_create_event(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_UINT8 queue_event, TW_UINT8 event_src, TW_UINT16 event_code,
	TW_UINT8 severity, TW_UINT8 *severity_str, TW_UINT8 *event_desc,
	TW_UINT8 *event_specific_desc, ...);

/* Indicates whether a ctlr is supported by CL. */
extern TW_INT32	tw_cl_ctlr_supported(TW_INT32 vendor_id, TW_INT32 device_id);


/* Submit a firmware cmd packet. */
extern TW_INT32	tw_cl_fw_passthru(struct tw_cl_ctlr_handle *ctlr_handle,
	struct tw_cl_req_packet *req_pkt, struct tw_cl_req_handle *req_handle);


/* Find out how much memory CL needs. */
extern TW_INT32	tw_cl_get_mem_requirements(
	struct tw_cl_ctlr_handle *ctlr_handle, TW_UINT32 flags,
	TW_INT32 device_id, TW_INT32 max_simult_reqs, TW_INT32 max_aens,
	TW_UINT32 *alignment, TW_UINT32 *sg_size_factor,
	TW_UINT32 *non_dma_mem_size, TW_UINT32 *dma_mem_size
	);


/* Return PCI BAR info. */
extern TW_INT32 tw_cl_get_pci_bar_info(TW_INT32 device_id, TW_INT32 bar_type,
	TW_INT32 *bar_num, TW_INT32 *bar0_offset, TW_INT32 *bar_size);


/* Initialize Common Layer for a given controller. */
extern TW_INT32	tw_cl_init_ctlr(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_UINT32 flags, TW_INT32 device_id, TW_INT32 max_simult_reqs,
	TW_INT32 max_aens, TW_VOID *non_dma_mem, TW_VOID *dma_mem,
	TW_UINT64 dma_mem_phys
	);


extern TW_VOID  tw_cl_set_reset_needed(struct tw_cl_ctlr_handle *ctlr_handle);
extern TW_INT32 tw_cl_is_reset_needed(struct tw_cl_ctlr_handle *ctlr_handle);
extern TW_INT32 tw_cl_is_active(struct tw_cl_ctlr_handle *ctlr_handle);

/* CL's interrupt handler. */
extern TW_INT32	tw_cl_interrupt(struct tw_cl_ctlr_handle *ctlr_handle);


/* CL's ioctl handler. */
extern TW_INT32	tw_cl_ioctl(struct tw_cl_ctlr_handle *ctlr_handle,
	u_long cmd, TW_VOID *buf);


#ifdef TW_OSL_DEBUG
/* Print CL's state/statistics for a controller. */
extern TW_VOID	tw_cl_print_ctlr_stats(struct tw_cl_ctlr_handle *ctlr_handle);

/* Prints CL internal details of a given request. */
extern TW_VOID	tw_cl_print_req_info(struct tw_cl_req_handle *req_handle);
#endif /* TW_OSL_DEBUG */


/* Soft reset controller. */
extern TW_INT32	tw_cl_reset_ctlr(struct tw_cl_ctlr_handle *ctlr_handle);


#ifdef TW_OSL_DEBUG
/* Reset CL's statistics for a controller. */
extern TW_VOID	tw_cl_reset_stats(struct tw_cl_ctlr_handle *ctlr_handle);
#endif /* TW_OSL_DEBUG */


/* Stop a controller. */
extern TW_INT32	tw_cl_shutdown_ctlr(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_UINT32 flags);


/* Submit a SCSI I/O request. */
extern TW_INT32	tw_cl_start_io(struct tw_cl_ctlr_handle *ctlr_handle,
	struct tw_cl_req_packet *req_pkt, struct tw_cl_req_handle *req_handle);


#endif /* TW_BUILDING_API */

#endif /* TW_CL_SHARE_H */
