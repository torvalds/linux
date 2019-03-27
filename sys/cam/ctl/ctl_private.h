/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2004, 2005, 2008 Silicon Graphics International Corp.
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_private.h#7 $
 * $FreeBSD$
 */
/*
 * CAM Target Layer driver private data structures/definitions.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_PRIVATE_H_
#define	_CTL_PRIVATE_H_

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_cd.h>
#include <cam/scsi/scsi_da.h>

/*
 * SCSI vendor and product names.
 */
#define	CTL_VENDOR		"FREEBSD "
#define	CTL_DIRECT_PRODUCT	"CTLDISK         "
#define	CTL_PROCESSOR_PRODUCT	"CTLPROCESSOR    "
#define	CTL_CDROM_PRODUCT	"CTLCDROM        "
#define	CTL_UNKNOWN_PRODUCT	"CTLDEVICE       "

#define CTL_POOL_ENTRIES_OTHER_SC   200

struct ctl_io_pool {
	char			name[64];
	uint32_t		id;
	struct ctl_softc	*ctl_softc;
	struct uma_zone		*zone;
};

typedef enum {
	CTL_SER_BLOCK,
	CTL_SER_BLOCKOPT,
	CTL_SER_EXTENT,
	CTL_SER_EXTENTOPT,
	CTL_SER_EXTENTSEQ,
	CTL_SER_PASS,
	CTL_SER_SKIP
} ctl_serialize_action;

typedef enum {
	CTL_ACTION_BLOCK,
	CTL_ACTION_OVERLAP,
	CTL_ACTION_OVERLAP_TAG,
	CTL_ACTION_PASS,
	CTL_ACTION_SKIP,
	CTL_ACTION_ERROR
} ctl_action;

/*
 * WARNING:  Keep the bottom nibble here free, we OR in the data direction
 * flags for each command.
 *
 * Note:  "OK_ON_NO_LUN"   == we don't have to have a lun configured
 *        "OK_ON_BOTH"     == we have to have a lun configured
 *        "SA5"            == command has 5-bit service action at byte 1
 */
typedef enum {
	CTL_CMD_FLAG_NONE		= 0x0000,
	CTL_CMD_FLAG_NO_SENSE		= 0x0010,
	CTL_CMD_FLAG_ALLOW_ON_RESV	= 0x0020,
	CTL_CMD_FLAG_ALLOW_ON_PR_RESV	= 0x0040,
	CTL_CMD_FLAG_ALLOW_ON_PR_WRESV	= 0x0080,
	CTL_CMD_FLAG_OK_ON_PROC		= 0x0100,
	CTL_CMD_FLAG_OK_ON_DIRECT	= 0x0200,
	CTL_CMD_FLAG_OK_ON_CDROM	= 0x0400,
	CTL_CMD_FLAG_OK_ON_BOTH		= 0x0700,
	CTL_CMD_FLAG_OK_ON_NO_LUN	= 0x0800,
	CTL_CMD_FLAG_OK_ON_NO_MEDIA	= 0x1000,
	CTL_CMD_FLAG_OK_ON_STANDBY	= 0x2000,
	CTL_CMD_FLAG_OK_ON_UNAVAIL	= 0x4000,
	CTL_CMD_FLAG_SA5		= 0x8000,
	CTL_CMD_FLAG_RUN_HERE		= 0x10000
} ctl_cmd_flags;

typedef enum {
	CTL_SERIDX_TUR	= 0,
	CTL_SERIDX_READ,
	CTL_SERIDX_WRITE,
	CTL_SERIDX_UNMAP,
	CTL_SERIDX_SYNC,
	CTL_SERIDX_MD_SNS,
	CTL_SERIDX_MD_SEL,
	CTL_SERIDX_RQ_SNS,
	CTL_SERIDX_INQ,
	CTL_SERIDX_RD_CAP,
	CTL_SERIDX_RES,
	CTL_SERIDX_LOG_SNS,
	CTL_SERIDX_FORMAT,
	CTL_SERIDX_START,
	/* TBD: others to be filled in as needed */
	CTL_SERIDX_COUNT, /* LAST, not a normal code, provides # codes */
	CTL_SERIDX_INVLD = CTL_SERIDX_COUNT
} ctl_seridx;

typedef int	ctl_opfunc(struct ctl_scsiio *ctsio);

struct ctl_cmd_entry {
	ctl_opfunc		*execute;
	ctl_seridx		seridx;
	ctl_cmd_flags		flags;
	ctl_lun_error_pattern	pattern;
	uint8_t			length;		/* CDB length */
	uint8_t			usage[15];	/* Mask of allowed CDB bits
						 * after the opcode byte. */
};

typedef enum {
	CTL_LUN_NONE		= 0x000,
	CTL_LUN_CONTROL		= 0x001,
	CTL_LUN_RESERVED	= 0x002,
	CTL_LUN_INVALID		= 0x004,
	CTL_LUN_DISABLED	= 0x008,
	CTL_LUN_MALLOCED	= 0x010,
	CTL_LUN_STOPPED		= 0x020,
	CTL_LUN_NO_MEDIA	= 0x040,
	CTL_LUN_EJECTED		= 0x080,
	CTL_LUN_PR_RESERVED	= 0x100,
	CTL_LUN_PRIMARY_SC	= 0x200,
	CTL_LUN_READONLY	= 0x800,
	CTL_LUN_PEER_SC_PRIMARY	= 0x1000,
	CTL_LUN_REMOVABLE	= 0x2000
} ctl_lun_flags;

typedef enum {
	CTLBLOCK_FLAG_NONE	= 0x00,
	CTLBLOCK_FLAG_INVALID	= 0x01
} ctlblock_flags;

union ctl_softcs {
	struct ctl_softc	*ctl_softc;
	struct ctlblock_softc	*ctlblock_softc;
};

/*
 * Mode page defaults.
 */
#if 0
/*
 * These values make Solaris trim off some of the capacity.
 */
#define	CTL_DEFAULT_SECTORS_PER_TRACK	63
#define	CTL_DEFAULT_HEADS		255
/*
 * These values seem to work okay.
 */
#define	CTL_DEFAULT_SECTORS_PER_TRACK	63
#define	CTL_DEFAULT_HEADS		16
/*
 * These values work reasonably well.
 */
#define	CTL_DEFAULT_SECTORS_PER_TRACK	512
#define	CTL_DEFAULT_HEADS		64
#endif

/*
 * Solaris is somewhat picky about how many heads and sectors per track you
 * have defined in mode pages 3 and 4.  These values seem to cause Solaris
 * to get the capacity more or less right when you run the format tool.
 * They still have problems when dealing with devices larger than 1TB,
 * but there isn't anything we can do about that.
 *
 * For smaller LUN sizes, this ends up causing the number of cylinders to
 * work out to 0.  Solaris actually recognizes that and comes up with its
 * own bogus geometry to fit the actual capacity of the drive.  They really
 * should just give up on geometry and stick to the read capacity
 * information alone for modern disk drives.
 *
 * One thing worth mentioning about Solaris' mkfs command is that it
 * doesn't like sectors per track values larger than 256.  512 seems to
 * work okay for format, but causes problems when you try to make a
 * filesystem.
 *
 * Another caveat about these values:  the product of these two values
 * really should be a power of 2.  This is because of the simplistic
 * shift-based calculation that we have to use on the i386 platform to
 * calculate the number of cylinders here.  (If you use a divide, you end
 * up calling __udivdi3(), which is a hardware FP call on the PC.  On the
 * XScale, it is done in software, so you can do that from inside the
 * kernel.)
 *
 * So for the current values (256 S/T, 128 H), we get 32768, which works
 * very nicely for calculating cylinders.
 *
 * If you want to change these values so that their product is no longer a
 * power of 2, re-visit the calculation in ctl_init_page_index().  You may
 * need to make it a bit more complicated to get the number of cylinders
 * right.
 */
#define	CTL_DEFAULT_SECTORS_PER_TRACK	256
#define	CTL_DEFAULT_HEADS		128

#define	CTL_DEFAULT_ROTATION_RATE	SVPD_NON_ROTATING

struct ctl_page_index;

typedef int	ctl_modesen_handler(struct ctl_scsiio *ctsio,
				    struct ctl_page_index *page_index,
				    int pc);
typedef int	ctl_modesel_handler(struct ctl_scsiio *ctsio,
				    struct ctl_page_index *page_index,
				    uint8_t *page_ptr);

typedef enum {
	CTL_PAGE_FLAG_NONE	 = 0x00,
	CTL_PAGE_FLAG_DIRECT	 = 0x01,
	CTL_PAGE_FLAG_PROC	 = 0x02,
	CTL_PAGE_FLAG_CDROM	 = 0x04,
	CTL_PAGE_FLAG_ALL	 = 0x07
} ctl_page_flags;

struct ctl_page_index {
	uint8_t			page_code;
	uint8_t			subpage;
	uint16_t		page_len;
	uint8_t			*page_data;
	ctl_page_flags		page_flags;
	ctl_modesen_handler	*sense_handler;
	ctl_modesel_handler	*select_handler;
};

#define	CTL_PAGE_CURRENT	0x00
#define	CTL_PAGE_CHANGEABLE	0x01
#define	CTL_PAGE_DEFAULT	0x02
#define	CTL_PAGE_SAVED		0x03

#define CTL_NUM_LBP_PARAMS	4
#define CTL_NUM_LBP_THRESH	4
#define CTL_LBP_EXPONENT	11	/* 2048 sectors */
#define CTL_LBP_PERIOD		10	/* 10 seconds */
#define CTL_LBP_UA_PERIOD	300	/* 5 minutes */

struct ctl_logical_block_provisioning_page {
	struct scsi_logical_block_provisioning_page	main;
	struct scsi_logical_block_provisioning_page_descr descr[CTL_NUM_LBP_THRESH];
};

static const struct ctl_page_index page_index_template[] = {
	{SMS_RW_ERROR_RECOVERY_PAGE, 0, sizeof(struct scsi_da_rw_recovery_page), NULL,
	 CTL_PAGE_FLAG_DIRECT | CTL_PAGE_FLAG_CDROM, NULL, ctl_default_page_handler},
	{SMS_FORMAT_DEVICE_PAGE, 0, sizeof(struct scsi_format_page), NULL,
	 CTL_PAGE_FLAG_DIRECT, NULL, NULL},
	{SMS_RIGID_DISK_PAGE, 0, sizeof(struct scsi_rigid_disk_page), NULL,
	 CTL_PAGE_FLAG_DIRECT, NULL, NULL},
	{SMS_VERIFY_ERROR_RECOVERY_PAGE, 0, sizeof(struct scsi_da_verify_recovery_page), NULL,
	 CTL_PAGE_FLAG_DIRECT | CTL_PAGE_FLAG_CDROM, NULL, ctl_default_page_handler},
	{SMS_CACHING_PAGE, 0, sizeof(struct scsi_caching_page), NULL,
	 CTL_PAGE_FLAG_DIRECT | CTL_PAGE_FLAG_CDROM,
	 NULL, ctl_default_page_handler},
	{SMS_CONTROL_MODE_PAGE, 0, sizeof(struct scsi_control_page), NULL,
	 CTL_PAGE_FLAG_ALL, NULL, ctl_default_page_handler},
	{SMS_CONTROL_MODE_PAGE | SMPH_SPF, 0x01,
	 sizeof(struct scsi_control_ext_page), NULL,
	 CTL_PAGE_FLAG_ALL, NULL, ctl_default_page_handler},
	{SMS_INFO_EXCEPTIONS_PAGE, 0, sizeof(struct scsi_info_exceptions_page), NULL,
	 CTL_PAGE_FLAG_ALL, NULL, ctl_ie_page_handler},
	{SMS_INFO_EXCEPTIONS_PAGE | SMPH_SPF, 0x02,
	 sizeof(struct ctl_logical_block_provisioning_page), NULL,
	 CTL_PAGE_FLAG_DIRECT, NULL, ctl_default_page_handler},
	{SMS_CDDVD_CAPS_PAGE, 0,
	 sizeof(struct scsi_cddvd_capabilities_page), NULL,
	 CTL_PAGE_FLAG_CDROM, NULL, NULL},
};

#define	CTL_NUM_MODE_PAGES sizeof(page_index_template)/   \
			   sizeof(page_index_template[0])

struct ctl_mode_pages {
	struct scsi_da_rw_recovery_page	rw_er_page[4];
	struct scsi_format_page		format_page[4];
	struct scsi_rigid_disk_page	rigid_disk_page[4];
	struct scsi_da_verify_recovery_page	verify_er_page[4];
	struct scsi_caching_page	caching_page[4];
	struct scsi_control_page	control_page[4];
	struct scsi_control_ext_page	control_ext_page[4];
	struct scsi_info_exceptions_page ie_page[4];
	struct ctl_logical_block_provisioning_page lbp_page[4];
	struct scsi_cddvd_capabilities_page cddvd_page[4];
	struct ctl_page_index		index[CTL_NUM_MODE_PAGES];
};

#define	MODE_RWER	mode_pages.rw_er_page[CTL_PAGE_CURRENT]
#define	MODE_FMT	mode_pages.format_page[CTL_PAGE_CURRENT]
#define	MODE_RDISK	mode_pages.rigid_disk_page[CTL_PAGE_CURRENT]
#define	MODE_VER	mode_pages.verify_er_page[CTL_PAGE_CURRENT]
#define	MODE_CACHING	mode_pages.caching_page[CTL_PAGE_CURRENT]
#define	MODE_CTRL	mode_pages.control_page[CTL_PAGE_CURRENT]
#define	MODE_CTRLE	mode_pages.control_ext_page[CTL_PAGE_CURRENT]
#define	MODE_IE		mode_pages.ie_page[CTL_PAGE_CURRENT]
#define	MODE_LBP	mode_pages.lbp_page[CTL_PAGE_CURRENT]
#define	MODE_CDDVD	mode_pages.cddvd_page[CTL_PAGE_CURRENT]

static const struct ctl_page_index log_page_index_template[] = {
	{SLS_SUPPORTED_PAGES_PAGE, 0, 0, NULL,
	 CTL_PAGE_FLAG_ALL, NULL, NULL},
	{SLS_SUPPORTED_PAGES_PAGE, SLS_SUPPORTED_SUBPAGES_SUBPAGE, 0, NULL,
	 CTL_PAGE_FLAG_ALL, NULL, NULL},
	{SLS_LOGICAL_BLOCK_PROVISIONING, 0, 0, NULL,
	 CTL_PAGE_FLAG_DIRECT, ctl_lbp_log_sense_handler, NULL},
	{SLS_STAT_AND_PERF, 0, 0, NULL,
	 CTL_PAGE_FLAG_ALL, ctl_sap_log_sense_handler, NULL},
	{SLS_IE_PAGE, 0, 0, NULL,
	 CTL_PAGE_FLAG_ALL, ctl_ie_log_sense_handler, NULL},
};

#define	CTL_NUM_LOG_PAGES sizeof(log_page_index_template)/   \
			  sizeof(log_page_index_template[0])

struct ctl_log_pages {
	uint8_t				pages_page[CTL_NUM_LOG_PAGES];
	uint8_t				subpages_page[CTL_NUM_LOG_PAGES * 2];
	uint8_t				lbp_page[12*CTL_NUM_LBP_PARAMS];
	struct stat_page {
		struct scsi_log_stat_and_perf sap;
		struct scsi_log_idle_time it;
		struct scsi_log_time_interval ti;
	} stat_page;
	struct scsi_log_informational_exceptions	ie_page;
	struct ctl_page_index		index[CTL_NUM_LOG_PAGES];
};

struct ctl_lun_delay_info {
	ctl_delay_type		datamove_type;
	uint32_t		datamove_delay;
	ctl_delay_type		done_type;
	uint32_t		done_delay;
};

#define CTL_PR_ALL_REGISTRANTS  0xFFFFFFFF
#define CTL_PR_NO_RESERVATION   0xFFFFFFF0

struct ctl_devid {
	int		len;
	uint8_t		data[];
};

#define NUM_HA_SHELVES		2

#define CTL_WRITE_BUFFER_SIZE	262144

struct tpc_list;
struct ctl_lun {
	struct mtx			lun_lock;
	uint64_t			lun;
	ctl_lun_flags			flags;
	STAILQ_HEAD(,ctl_error_desc)	error_list;
	uint64_t			error_serial;
	struct ctl_softc		*ctl_softc;
	struct ctl_be_lun		*be_lun;
	struct ctl_backend_driver	*backend;
	struct ctl_lun_delay_info	delay_info;
#ifdef CTL_TIME_IO
	sbintime_t			idle_time;
	sbintime_t			last_busy;
#endif
	TAILQ_HEAD(ctl_ooaq, ctl_io_hdr)  ooa_queue;
	STAILQ_ENTRY(ctl_lun)		links;
	struct scsi_sense_data		**pending_sense;
	ctl_ua_type			**pending_ua;
	uint8_t				ua_tpt_info[8];
	time_t				lasttpt;
	uint8_t				ie_asc;	/* Informational exceptions */
	uint8_t				ie_ascq;
	int				ie_reported;	/* Already reported */
	uint32_t			ie_reportcnt;	/* REPORT COUNT */
	struct callout			ie_callout;	/* INTERVAL TIMER */
	struct ctl_mode_pages		mode_pages;
	struct ctl_log_pages		log_pages;
	struct ctl_io_stats		stats;
	uint32_t			res_idx;
	uint32_t			pr_generation;
	uint64_t			**pr_keys;
	int				pr_key_count;
	uint32_t			pr_res_idx;
	uint8_t				pr_res_type;
	int				prevent_count;
	uint32_t			*prevent;
	uint8_t				*write_buffer;
	struct ctl_devid		*lun_devid;
	TAILQ_HEAD(tpc_lists, tpc_list) tpc_lists;
};

typedef enum {
	CTL_FLAG_ACTIVE_SHELF	= 0x04
} ctl_gen_flags;

#define CTL_MAX_THREADS		16

struct ctl_thread {
	struct mtx_padalign queue_lock;
	struct ctl_softc	*ctl_softc;
	struct thread		*thread;
	STAILQ_HEAD(, ctl_io_hdr) incoming_queue;
	STAILQ_HEAD(, ctl_io_hdr) rtr_queue;
	STAILQ_HEAD(, ctl_io_hdr) done_queue;
	STAILQ_HEAD(, ctl_io_hdr) isc_queue;
};

struct tpc_token;
struct ctl_softc {
	struct mtx		ctl_lock;
	struct cdev		*dev;
	int			num_luns;
	ctl_gen_flags		flags;
	ctl_ha_mode		ha_mode;
	int			ha_id;
	int			is_single;
	ctl_ha_link_state	ha_link;
	int			port_min;
	int			port_max;
	int			port_cnt;
	int			init_min;
	int			init_max;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	void			*othersc_pool;
	struct proc		*ctl_proc;
	uint32_t		*ctl_lun_mask;
	struct ctl_lun		**ctl_luns;
	uint32_t		*ctl_port_mask;
	STAILQ_HEAD(, ctl_lun)	lun_list;
	STAILQ_HEAD(, ctl_be_lun)	pending_lun_queue;
	uint32_t		num_frontends;
	STAILQ_HEAD(, ctl_frontend)	fe_list;
	uint32_t		num_ports;
	STAILQ_HEAD(, ctl_port)	port_list;
	struct ctl_port		**ctl_ports;
	uint32_t		num_backends;
	STAILQ_HEAD(, ctl_backend_driver)	be_list;
	struct uma_zone		*io_zone;
	uint32_t		cur_pool_id;
	int			shutdown;
	struct ctl_thread	threads[CTL_MAX_THREADS];
	struct thread		*lun_thread;
	struct thread		*thresh_thread;
	TAILQ_HEAD(tpc_tokens, tpc_token)	tpc_tokens;
	struct callout		tpc_timeout;
	struct mtx		tpc_lock;
};

#ifdef _KERNEL

extern const struct ctl_cmd_entry ctl_cmd_table[256];

uint32_t ctl_get_initindex(struct ctl_nexus *nexus);
int ctl_lun_map_init(struct ctl_port *port);
int ctl_lun_map_deinit(struct ctl_port *port);
int ctl_lun_map_set(struct ctl_port *port, uint32_t plun, uint32_t glun);
int ctl_lun_map_unset(struct ctl_port *port, uint32_t plun);
uint32_t ctl_lun_map_from_port(struct ctl_port *port, uint32_t plun);
uint32_t ctl_lun_map_to_port(struct ctl_port *port, uint32_t glun);
int ctl_pool_create(struct ctl_softc *ctl_softc, const char *pool_name,
		    uint32_t total_ctl_io, void **npool);
void ctl_pool_free(struct ctl_io_pool *pool);
int ctl_scsi_release(struct ctl_scsiio *ctsio);
int ctl_scsi_reserve(struct ctl_scsiio *ctsio);
int ctl_start_stop(struct ctl_scsiio *ctsio);
int ctl_prevent_allow(struct ctl_scsiio *ctsio);
int ctl_sync_cache(struct ctl_scsiio *ctsio);
int ctl_format(struct ctl_scsiio *ctsio);
int ctl_read_buffer(struct ctl_scsiio *ctsio);
int ctl_write_buffer(struct ctl_scsiio *ctsio);
int ctl_write_same(struct ctl_scsiio *ctsio);
int ctl_unmap(struct ctl_scsiio *ctsio);
int ctl_mode_select(struct ctl_scsiio *ctsio);
int ctl_mode_sense(struct ctl_scsiio *ctsio);
int ctl_log_sense(struct ctl_scsiio *ctsio);
int ctl_read_capacity(struct ctl_scsiio *ctsio);
int ctl_read_capacity_16(struct ctl_scsiio *ctsio);
int ctl_read_defect(struct ctl_scsiio *ctsio);
int ctl_read_toc(struct ctl_scsiio *ctsio);
int ctl_read_write(struct ctl_scsiio *ctsio);
int ctl_cnw(struct ctl_scsiio *ctsio);
int ctl_report_luns(struct ctl_scsiio *ctsio);
int ctl_request_sense(struct ctl_scsiio *ctsio);
int ctl_tur(struct ctl_scsiio *ctsio);
int ctl_verify(struct ctl_scsiio *ctsio);
int ctl_inquiry(struct ctl_scsiio *ctsio);
int ctl_get_config(struct ctl_scsiio *ctsio);
int ctl_get_event_status(struct ctl_scsiio *ctsio);
int ctl_mechanism_status(struct ctl_scsiio *ctsio);
int ctl_persistent_reserve_in(struct ctl_scsiio *ctsio);
int ctl_persistent_reserve_out(struct ctl_scsiio *ctsio);
int ctl_report_tagret_port_groups(struct ctl_scsiio *ctsio);
int ctl_report_supported_opcodes(struct ctl_scsiio *ctsio);
int ctl_report_supported_tmf(struct ctl_scsiio *ctsio);
int ctl_report_timestamp(struct ctl_scsiio *ctsio);
int ctl_get_lba_status(struct ctl_scsiio *ctsio);

void ctl_tpc_init(struct ctl_softc *softc);
void ctl_tpc_shutdown(struct ctl_softc *softc);
void ctl_tpc_lun_init(struct ctl_lun *lun);
void ctl_tpc_lun_clear(struct ctl_lun *lun, uint32_t initidx);
void ctl_tpc_lun_shutdown(struct ctl_lun *lun);
int ctl_inquiry_evpd_tpc(struct ctl_scsiio *ctsio, int alloc_len);
int ctl_receive_copy_status_lid1(struct ctl_scsiio *ctsio);
int ctl_receive_copy_failure_details(struct ctl_scsiio *ctsio);
int ctl_receive_copy_status_lid4(struct ctl_scsiio *ctsio);
int ctl_receive_copy_operating_parameters(struct ctl_scsiio *ctsio);
int ctl_extended_copy_lid1(struct ctl_scsiio *ctsio);
int ctl_extended_copy_lid4(struct ctl_scsiio *ctsio);
int ctl_copy_operation_abort(struct ctl_scsiio *ctsio);
int ctl_populate_token(struct ctl_scsiio *ctsio);
int ctl_write_using_token(struct ctl_scsiio *ctsio);
int ctl_receive_rod_token_information(struct ctl_scsiio *ctsio);
int ctl_report_all_rod_tokens(struct ctl_scsiio *ctsio);

#endif	/* _KERNEL */

#endif	/* _CTL_PRIVATE_H_ */

/*
 * vim: ts=8
 */
