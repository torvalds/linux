/*-
 * Data structures and definitions for CAM Control Blocks (CCBs).
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CAM_CAM_CCB_H
#define _CAM_CAM_CCB_H 1

#include <sys/queue.h>
#include <sys/cdefs.h>
#include <sys/time.h>
#include <sys/limits.h>
#ifndef _KERNEL
#include <sys/callout.h>
#endif
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_all.h>
#include <cam/ata/ata_all.h>
#include <cam/nvme/nvme_all.h>
#include <cam/mmc/mmc_all.h>

/* General allocation length definitions for CCB structures */
#define	IOCDBLEN	CAM_MAX_CDBLEN	/* Space for CDB bytes/pointer */
#define	VUHBALEN	14		/* Vendor Unique HBA length */
#define	SIM_IDLEN	16		/* ASCII string len for SIM ID */
#define	HBA_IDLEN	16		/* ASCII string len for HBA ID */
#define	DEV_IDLEN	16		/* ASCII string len for device names */
#define CCB_PERIPH_PRIV_SIZE 	2	/* size of peripheral private area */
#define CCB_SIM_PRIV_SIZE 	2	/* size of sim private area */

/* Struct definitions for CAM control blocks */

/* Common CCB header */
/* CAM CCB flags */
typedef enum {
	CAM_CDB_POINTER		= 0x00000001,/* The CDB field is a pointer    */
	CAM_QUEUE_ENABLE	= 0x00000002,/* SIM queue actions are enabled */
	CAM_CDB_LINKED		= 0x00000004,/* CCB contains a linked CDB     */
	CAM_NEGOTIATE		= 0x00000008,/*
					      * Perform transport negotiation
					      * with this command.
					      */
	CAM_DATA_ISPHYS		= 0x00000010,/* Data type with physical addrs */
	CAM_DIS_AUTOSENSE	= 0x00000020,/* Disable autosense feature     */
	CAM_DIR_BOTH		= 0x00000000,/* Data direction (00:IN/OUT)    */
	CAM_DIR_IN		= 0x00000040,/* Data direction (01:DATA IN)   */
	CAM_DIR_OUT		= 0x00000080,/* Data direction (10:DATA OUT)  */
	CAM_DIR_NONE		= 0x000000C0,/* Data direction (11:no data)   */
	CAM_DIR_MASK		= 0x000000C0,/* Data direction Mask	      */
	CAM_DATA_VADDR		= 0x00000000,/* Data type (000:Virtual)       */
	CAM_DATA_PADDR		= 0x00000010,/* Data type (001:Physical)      */
	CAM_DATA_SG		= 0x00040000,/* Data type (010:sglist)        */
	CAM_DATA_SG_PADDR	= 0x00040010,/* Data type (011:sglist phys)   */
	CAM_DATA_BIO		= 0x00200000,/* Data type (100:bio)           */
	CAM_DATA_MASK		= 0x00240010,/* Data type mask                */
	CAM_SOFT_RST_OP		= 0x00000100,/* Use Soft reset alternative    */
	CAM_ENG_SYNC		= 0x00000200,/* Flush resid bytes on complete */
	CAM_DEV_QFRZDIS		= 0x00000400,/* Disable DEV Q freezing	      */
	CAM_DEV_QFREEZE		= 0x00000800,/* Freeze DEV Q on execution     */
	CAM_HIGH_POWER		= 0x00001000,/* Command takes a lot of power  */
	CAM_SENSE_PTR		= 0x00002000,/* Sense data is a pointer	      */
	CAM_SENSE_PHYS		= 0x00004000,/* Sense pointer is physical addr*/
	CAM_TAG_ACTION_VALID	= 0x00008000,/* Use the tag action in this ccb*/
	CAM_PASS_ERR_RECOVER	= 0x00010000,/* Pass driver does err. recovery*/
	CAM_DIS_DISCONNECT	= 0x00020000,/* Disable disconnect	      */
	CAM_MSG_BUF_PHYS	= 0x00080000,/* Message buffer ptr is physical*/
	CAM_SNS_BUF_PHYS	= 0x00100000,/* Autosense data ptr is physical*/
	CAM_CDB_PHYS		= 0x00400000,/* CDB poiner is physical	      */
	CAM_ENG_SGLIST		= 0x00800000,/* SG list is for the HBA engine */

/* Phase cognizant mode flags */
	CAM_DIS_AUTOSRP		= 0x01000000,/* Disable autosave/restore ptrs */
	CAM_DIS_AUTODISC	= 0x02000000,/* Disable auto disconnect	      */
	CAM_TGT_CCB_AVAIL	= 0x04000000,/* Target CCB available	      */
	CAM_TGT_PHASE_MODE	= 0x08000000,/* The SIM runs in phase mode    */
	CAM_MSGB_VALID		= 0x10000000,/* Message buffer valid	      */
	CAM_STATUS_VALID	= 0x20000000,/* Status buffer valid	      */
	CAM_DATAB_VALID		= 0x40000000,/* Data buffer valid	      */

/* Host target Mode flags */
	CAM_SEND_SENSE		= 0x08000000,/* Send sense data with status   */
	CAM_TERM_IO		= 0x10000000,/* Terminate I/O Message sup.    */
	CAM_DISCONNECT		= 0x20000000,/* Disconnects are mandatory     */
	CAM_SEND_STATUS		= 0x40000000,/* Send status after data phase  */

	CAM_UNLOCKED		= 0x80000000 /* Call callback without lock.   */
} ccb_flags;

typedef enum {
	CAM_USER_DATA_ADDR	= 0x00000002,/* Userspace data pointers */
	CAM_SG_FORMAT_IOVEC	= 0x00000004,/* iovec instead of busdma S/G*/
	CAM_UNMAPPED_BUF	= 0x00000008 /* use unmapped I/O */
} ccb_xflags;

/* XPT Opcodes for xpt_action */
typedef enum {
/* Function code flags are bits greater than 0xff */
	XPT_FC_QUEUED		= 0x100,
				/* Non-immediate function code */
	XPT_FC_USER_CCB		= 0x200,
	XPT_FC_XPT_ONLY		= 0x400,
				/* Only for the transport layer device */
	XPT_FC_DEV_QUEUED	= 0x800 | XPT_FC_QUEUED,
				/* Passes through the device queues */
/* Common function commands: 0x00->0x0F */
	XPT_NOOP 		= 0x00,
				/* Execute Nothing */
	XPT_SCSI_IO		= 0x01 | XPT_FC_DEV_QUEUED,
				/* Execute the requested I/O operation */
	XPT_GDEV_TYPE		= 0x02,
				/* Get type information for specified device */
	XPT_GDEVLIST		= 0x03,
				/* Get a list of peripheral devices */
	XPT_PATH_INQ		= 0x04,
				/* Path routing inquiry */
	XPT_REL_SIMQ		= 0x05,
				/* Release a frozen device queue */
	XPT_SASYNC_CB		= 0x06,
				/* Set Asynchronous Callback Parameters */
	XPT_SDEV_TYPE		= 0x07,
				/* Set device type information */
	XPT_SCAN_BUS		= 0x08 | XPT_FC_QUEUED | XPT_FC_USER_CCB
				       | XPT_FC_XPT_ONLY,
				/* (Re)Scan the SCSI Bus */
	XPT_DEV_MATCH		= 0x09 | XPT_FC_XPT_ONLY,
				/* Get EDT entries matching the given pattern */
	XPT_DEBUG		= 0x0a,
				/* Turn on debugging for a bus, target or lun */
	XPT_PATH_STATS		= 0x0b,
				/* Path statistics (error counts, etc.) */
	XPT_GDEV_STATS		= 0x0c,
				/* Device statistics (error counts, etc.) */
	XPT_DEV_ADVINFO		= 0x0e,
				/* Get/Set Device advanced information */
	XPT_ASYNC		= 0x0f | XPT_FC_QUEUED | XPT_FC_USER_CCB
				       | XPT_FC_XPT_ONLY,
				/* Asynchronous event */
/* SCSI Control Functions: 0x10->0x1F */
	XPT_ABORT		= 0x10,
				/* Abort the specified CCB */
	XPT_RESET_BUS		= 0x11 | XPT_FC_XPT_ONLY,
				/* Reset the specified SCSI bus */
	XPT_RESET_DEV		= 0x12 | XPT_FC_DEV_QUEUED,
				/* Bus Device Reset the specified SCSI device */
	XPT_TERM_IO		= 0x13,
				/* Terminate the I/O process */
	XPT_SCAN_LUN		= 0x14 | XPT_FC_QUEUED | XPT_FC_USER_CCB
				       | XPT_FC_XPT_ONLY,
				/* Scan Logical Unit */
	XPT_GET_TRAN_SETTINGS	= 0x15,
				/*
				 * Get default/user transfer settings
				 * for the target
				 */
	XPT_SET_TRAN_SETTINGS	= 0x16,
				/*
				 * Set transfer rate/width
				 * negotiation settings
				 */
	XPT_CALC_GEOMETRY	= 0x17,
				/*
				 * Calculate the geometry parameters for
				 * a device give the sector size and
				 * volume size.
				 */
	XPT_ATA_IO		= 0x18 | XPT_FC_DEV_QUEUED,
				/* Execute the requested ATA I/O operation */

	XPT_GET_SIM_KNOB_OLD	= 0x18, /* Compat only */

	XPT_SET_SIM_KNOB	= 0x19,
				/*
				 * Set SIM specific knob values.
				 */

	XPT_GET_SIM_KNOB	= 0x1a,
				/*
				 * Get SIM specific knob values.
				 */

	XPT_SMP_IO		= 0x1b | XPT_FC_DEV_QUEUED,
				/* Serial Management Protocol */

	XPT_NVME_IO		= 0x1c | XPT_FC_DEV_QUEUED,
				/* Execute the requested NVMe I/O operation */

	XPT_MMC_IO		= 0x1d | XPT_FC_DEV_QUEUED,
				/* Placeholder for MMC / SD / SDIO I/O stuff */

	XPT_SCAN_TGT		= 0x1e | XPT_FC_QUEUED | XPT_FC_USER_CCB
				       | XPT_FC_XPT_ONLY,
				/* Scan Target */

	XPT_NVME_ADMIN		= 0x1f | XPT_FC_DEV_QUEUED,
				/* Execute the requested NVMe Admin operation */

/* HBA engine commands 0x20->0x2F */
	XPT_ENG_INQ		= 0x20 | XPT_FC_XPT_ONLY,
				/* HBA engine feature inquiry */
	XPT_ENG_EXEC		= 0x21 | XPT_FC_DEV_QUEUED,
				/* HBA execute engine request */

/* Target mode commands: 0x30->0x3F */
	XPT_EN_LUN		= 0x30,
				/* Enable LUN as a target */
	XPT_TARGET_IO		= 0x31 | XPT_FC_DEV_QUEUED,
				/* Execute target I/O request */
	XPT_ACCEPT_TARGET_IO	= 0x32 | XPT_FC_QUEUED | XPT_FC_USER_CCB,
				/* Accept Host Target Mode CDB */
	XPT_CONT_TARGET_IO	= 0x33 | XPT_FC_DEV_QUEUED,
				/* Continue Host Target I/O Connection */
	XPT_IMMED_NOTIFY	= 0x34 | XPT_FC_QUEUED | XPT_FC_USER_CCB,
				/* Notify Host Target driver of event (obsolete) */
	XPT_NOTIFY_ACK		= 0x35,
				/* Acknowledgement of event (obsolete) */
	XPT_IMMEDIATE_NOTIFY	= 0x36 | XPT_FC_QUEUED | XPT_FC_USER_CCB,
				/* Notify Host Target driver of event */
	XPT_NOTIFY_ACKNOWLEDGE	= 0x37 | XPT_FC_QUEUED | XPT_FC_USER_CCB,
				/* Acknowledgement of event */
	XPT_REPROBE_LUN		= 0x38 | XPT_FC_QUEUED | XPT_FC_USER_CCB,
				/* Query device capacity and notify GEOM */

/* Vendor Unique codes: 0x80->0x8F */
	XPT_VUNIQUE		= 0x80
} xpt_opcode;

#define XPT_FC_GROUP_MASK		0xF0
#define XPT_FC_GROUP(op) ((op) & XPT_FC_GROUP_MASK)
#define XPT_FC_GROUP_COMMON		0x00
#define XPT_FC_GROUP_SCSI_CONTROL	0x10
#define XPT_FC_GROUP_HBA_ENGINE		0x20
#define XPT_FC_GROUP_TMODE		0x30
#define XPT_FC_GROUP_VENDOR_UNIQUE	0x80

#define XPT_FC_IS_DEV_QUEUED(ccb) 	\
    (((ccb)->ccb_h.func_code & XPT_FC_DEV_QUEUED) == XPT_FC_DEV_QUEUED)
#define XPT_FC_IS_QUEUED(ccb) 	\
    (((ccb)->ccb_h.func_code & XPT_FC_QUEUED) != 0)

typedef enum {
	PROTO_UNKNOWN,
	PROTO_UNSPECIFIED,
	PROTO_SCSI,	/* Small Computer System Interface */
	PROTO_ATA,	/* AT Attachment */
	PROTO_ATAPI,	/* AT Attachment Packetized Interface */
	PROTO_SATAPM,	/* SATA Port Multiplier */
	PROTO_SEMB,	/* SATA Enclosure Management Bridge */
	PROTO_NVME,	/* NVME */
	PROTO_MMCSD,	/* MMC, SD, SDIO */
} cam_proto;

typedef enum {
	XPORT_UNKNOWN,
	XPORT_UNSPECIFIED,
	XPORT_SPI,	/* SCSI Parallel Interface */
	XPORT_FC,	/* Fiber Channel */
	XPORT_SSA,	/* Serial Storage Architecture */
	XPORT_USB,	/* Universal Serial Bus */
	XPORT_PPB,	/* Parallel Port Bus */
	XPORT_ATA,	/* AT Attachment */
	XPORT_SAS,	/* Serial Attached SCSI */
	XPORT_SATA,	/* Serial AT Attachment */
	XPORT_ISCSI,	/* iSCSI */
	XPORT_SRP,	/* SCSI RDMA Protocol */
	XPORT_NVME,	/* NVMe over PCIe */
	XPORT_MMCSD,	/* MMC, SD, SDIO card */
} cam_xport;

#define XPORT_IS_NVME(t)	((t) == XPORT_NVME)
#define XPORT_IS_ATA(t)		((t) == XPORT_ATA || (t) == XPORT_SATA)
#define XPORT_IS_SCSI(t)	((t) != XPORT_UNKNOWN && \
				 (t) != XPORT_UNSPECIFIED && \
				 !XPORT_IS_ATA(t) && !XPORT_IS_NVME(t))
#define XPORT_DEVSTAT_TYPE(t)	(XPORT_IS_ATA(t) ? DEVSTAT_TYPE_IF_IDE : \
				 XPORT_IS_SCSI(t) ? DEVSTAT_TYPE_IF_SCSI : \
				 DEVSTAT_TYPE_IF_OTHER)

#define PROTO_VERSION_UNKNOWN (UINT_MAX - 1)
#define PROTO_VERSION_UNSPECIFIED UINT_MAX
#define XPORT_VERSION_UNKNOWN (UINT_MAX - 1)
#define XPORT_VERSION_UNSPECIFIED UINT_MAX

typedef union {
	LIST_ENTRY(ccb_hdr) le;
	SLIST_ENTRY(ccb_hdr) sle;
	TAILQ_ENTRY(ccb_hdr) tqe;
	STAILQ_ENTRY(ccb_hdr) stqe;
} camq_entry;

typedef union {
	void		*ptr;
	u_long		field;
	u_int8_t	bytes[sizeof(uintptr_t)];
} ccb_priv_entry;

typedef union {
	ccb_priv_entry	entries[CCB_PERIPH_PRIV_SIZE];
	u_int8_t	bytes[CCB_PERIPH_PRIV_SIZE * sizeof(ccb_priv_entry)];
} ccb_ppriv_area;

typedef union {
	ccb_priv_entry	entries[CCB_SIM_PRIV_SIZE];
	u_int8_t	bytes[CCB_SIM_PRIV_SIZE * sizeof(ccb_priv_entry)];
} ccb_spriv_area;

typedef struct {
	struct timeval	*etime;
	uintptr_t	sim_data;
	uintptr_t	periph_data;
} ccb_qos_area;

struct ccb_hdr {
	cam_pinfo	pinfo;		/* Info for priority scheduling */
	camq_entry	xpt_links;	/* For chaining in the XPT layer */
	camq_entry	sim_links;	/* For chaining in the SIM layer */
	camq_entry	periph_links;	/* For chaining in the type driver */
	u_int32_t	retry_count;
	void		(*cbfcnp)(struct cam_periph *, union ccb *);
					/* Callback on completion function */
	xpt_opcode	func_code;	/* XPT function code */
	u_int32_t	status;		/* Status returned by CAM subsystem */
	struct		cam_path *path;	/* Compiled path for this ccb */
	path_id_t	path_id;	/* Path ID for the request */
	target_id_t	target_id;	/* Target device ID */
	lun_id_t	target_lun;	/* Target LUN number */
	u_int32_t	flags;		/* ccb_flags */
	u_int32_t	xflags;		/* Extended flags */
	ccb_ppriv_area	periph_priv;
	ccb_spriv_area	sim_priv;
	ccb_qos_area	qos;
	u_int32_t	timeout;	/* Hard timeout value in mseconds */
	struct timeval	softtimeout;	/* Soft timeout value in sec + usec */
};

/* Get Device Information CCB */
struct ccb_getdev {
	struct	  ccb_hdr ccb_h;
	cam_proto protocol;
	struct scsi_inquiry_data inq_data;
	struct ata_params ident_data;
	u_int8_t  serial_num[252];
	u_int8_t  inq_flags;
	u_int8_t  serial_num_len;
	void *padding[2];
};

/* Device Statistics CCB */
struct ccb_getdevstats {
	struct	ccb_hdr	ccb_h;
	int	dev_openings;	/* Space left for more work on device*/
	int	dev_active;	/* Transactions running on the device */
	int	allocated;	/* CCBs allocated for the device */
	int	queued;		/* CCBs queued to be sent to the device */
	int	held;		/*
				 * CCBs held by peripheral drivers
				 * for this device
				 */
	int	maxtags;	/*
				 * Boundary conditions for number of
				 * tagged operations
				 */
	int	mintags;
	struct	timeval last_reset;	/* Time of last bus reset/loop init */
};

typedef enum {
	CAM_GDEVLIST_LAST_DEVICE,
	CAM_GDEVLIST_LIST_CHANGED,
	CAM_GDEVLIST_MORE_DEVS,
	CAM_GDEVLIST_ERROR
} ccb_getdevlist_status_e;

struct ccb_getdevlist {
	struct ccb_hdr		ccb_h;
	char 			periph_name[DEV_IDLEN];
	u_int32_t		unit_number;
	unsigned int		generation;
	u_int32_t		index;
	ccb_getdevlist_status_e	status;
};

typedef enum {
	PERIPH_MATCH_NONE	= 0x000,
	PERIPH_MATCH_PATH	= 0x001,
	PERIPH_MATCH_TARGET	= 0x002,
	PERIPH_MATCH_LUN	= 0x004,
	PERIPH_MATCH_NAME	= 0x008,
	PERIPH_MATCH_UNIT	= 0x010,
	PERIPH_MATCH_ANY	= 0x01f
} periph_pattern_flags;

struct periph_match_pattern {
	char			periph_name[DEV_IDLEN];
	u_int32_t		unit_number;
	path_id_t		path_id;
	target_id_t		target_id;
	lun_id_t		target_lun;
	periph_pattern_flags	flags;
};

typedef enum {
	DEV_MATCH_NONE		= 0x000,
	DEV_MATCH_PATH		= 0x001,
	DEV_MATCH_TARGET	= 0x002,
	DEV_MATCH_LUN		= 0x004,
	DEV_MATCH_INQUIRY	= 0x008,
	DEV_MATCH_DEVID		= 0x010,
	DEV_MATCH_ANY		= 0x00f
} dev_pattern_flags;

struct device_id_match_pattern {
	uint8_t id_len;
	uint8_t id[256];
};

struct device_match_pattern {
	path_id_t					path_id;
	target_id_t					target_id;
	lun_id_t					target_lun;
	dev_pattern_flags				flags;
	union {
		struct scsi_static_inquiry_pattern	inq_pat;
		struct device_id_match_pattern		devid_pat;
	} data;
};

typedef enum {
	BUS_MATCH_NONE		= 0x000,
	BUS_MATCH_PATH		= 0x001,
	BUS_MATCH_NAME		= 0x002,
	BUS_MATCH_UNIT		= 0x004,
	BUS_MATCH_BUS_ID	= 0x008,
	BUS_MATCH_ANY		= 0x00f
} bus_pattern_flags;

struct bus_match_pattern {
	path_id_t		path_id;
	char			dev_name[DEV_IDLEN];
	u_int32_t		unit_number;
	u_int32_t		bus_id;
	bus_pattern_flags	flags;
};

union match_pattern {
	struct periph_match_pattern	periph_pattern;
	struct device_match_pattern	device_pattern;
	struct bus_match_pattern	bus_pattern;
};

typedef enum {
	DEV_MATCH_PERIPH,
	DEV_MATCH_DEVICE,
	DEV_MATCH_BUS
} dev_match_type;

struct dev_match_pattern {
	dev_match_type		type;
	union match_pattern	pattern;
};

struct periph_match_result {
	char			periph_name[DEV_IDLEN];
	u_int32_t		unit_number;
	path_id_t		path_id;
	target_id_t		target_id;
	lun_id_t		target_lun;
};

typedef enum {
	DEV_RESULT_NOFLAG		= 0x00,
	DEV_RESULT_UNCONFIGURED		= 0x01
} dev_result_flags;

struct device_match_result {
	path_id_t			path_id;
	target_id_t			target_id;
	lun_id_t			target_lun;
	cam_proto			protocol;
	struct scsi_inquiry_data	inq_data;
	struct ata_params		ident_data;
	dev_result_flags		flags;
};

struct bus_match_result {
	path_id_t	path_id;
	char		dev_name[DEV_IDLEN];
	u_int32_t	unit_number;
	u_int32_t	bus_id;
};

union match_result {
	struct periph_match_result	periph_result;
	struct device_match_result	device_result;
	struct bus_match_result		bus_result;
};

struct dev_match_result {
	dev_match_type		type;
	union match_result	result;
};

typedef enum {
	CAM_DEV_MATCH_LAST,
	CAM_DEV_MATCH_MORE,
	CAM_DEV_MATCH_LIST_CHANGED,
	CAM_DEV_MATCH_SIZE_ERROR,
	CAM_DEV_MATCH_ERROR
} ccb_dev_match_status;

typedef enum {
	CAM_DEV_POS_NONE	= 0x000,
	CAM_DEV_POS_BUS		= 0x001,
	CAM_DEV_POS_TARGET	= 0x002,
	CAM_DEV_POS_DEVICE	= 0x004,
	CAM_DEV_POS_PERIPH	= 0x008,
	CAM_DEV_POS_PDPTR	= 0x010,
	CAM_DEV_POS_TYPEMASK	= 0xf00,
	CAM_DEV_POS_EDT		= 0x100,
	CAM_DEV_POS_PDRV	= 0x200
} dev_pos_type;

struct ccb_dm_cookie {
	void 	*bus;
	void	*target;
	void	*device;
	void	*periph;
	void	*pdrv;
};

struct ccb_dev_position {
	u_int			generations[4];
#define	CAM_BUS_GENERATION	0x00
#define CAM_TARGET_GENERATION	0x01
#define CAM_DEV_GENERATION	0x02
#define CAM_PERIPH_GENERATION	0x03
	dev_pos_type		position_type;
	struct ccb_dm_cookie	cookie;
};

struct ccb_dev_match {
	struct ccb_hdr			ccb_h;
	ccb_dev_match_status		status;
	u_int32_t			num_patterns;
	u_int32_t			pattern_buf_len;
	struct dev_match_pattern	*patterns;
	u_int32_t			num_matches;
	u_int32_t			match_buf_len;
	struct dev_match_result		*matches;
	struct ccb_dev_position		pos;
};

/*
 * Definitions for the path inquiry CCB fields.
 */
#define CAM_VERSION	0x19	/* Hex value for current version */

typedef enum {
	PI_MDP_ABLE	= 0x80,	/* Supports MDP message */
	PI_WIDE_32	= 0x40,	/* Supports 32 bit wide SCSI */
	PI_WIDE_16	= 0x20, /* Supports 16 bit wide SCSI */
	PI_SDTR_ABLE	= 0x10,	/* Supports SDTR message */
	PI_LINKED_CDB	= 0x08, /* Supports linked CDBs */
	PI_SATAPM	= 0x04,	/* Supports SATA PM */
	PI_TAG_ABLE	= 0x02,	/* Supports tag queue messages */
	PI_SOFT_RST	= 0x01	/* Supports soft reset alternative */
} pi_inqflag;

typedef enum {
	PIT_PROCESSOR	= 0x80,	/* Target mode processor mode */
	PIT_PHASE	= 0x40,	/* Target mode phase cog. mode */
	PIT_DISCONNECT	= 0x20,	/* Disconnects supported in target mode */
	PIT_TERM_IO	= 0x10,	/* Terminate I/O message supported in TM */
	PIT_GRP_6	= 0x08,	/* Group 6 commands supported */
	PIT_GRP_7	= 0x04	/* Group 7 commands supported */
} pi_tmflag;

typedef enum {
	PIM_ATA_EXT	= 0x200,/* ATA requests can understand ata_ext requests */
	PIM_EXTLUNS	= 0x100,/* 64bit extended LUNs supported */
	PIM_SCANHILO	= 0x80,	/* Bus scans from high ID to low ID */
	PIM_NOREMOVE	= 0x40,	/* Removeable devices not included in scan */
	PIM_NOINITIATOR	= 0x20,	/* Initiator role not supported. */
	PIM_NOBUSRESET	= 0x10,	/* User has disabled initial BUS RESET */
	PIM_NO_6_BYTE	= 0x08,	/* Do not send 6-byte commands */
	PIM_SEQSCAN	= 0x04,	/* Do bus scans sequentially, not in parallel */
	PIM_UNMAPPED	= 0x02,
	PIM_NOSCAN	= 0x01	/* SIM does its own scanning */
} pi_miscflag;

/* Path Inquiry CCB */
struct ccb_pathinq_settings_spi {
	u_int8_t ppr_options;
};

struct ccb_pathinq_settings_fc {
	u_int64_t wwnn;		/* world wide node name */
	u_int64_t wwpn;		/* world wide port name */
	u_int32_t port;		/* 24 bit port id, if known */
	u_int32_t bitrate;	/* Mbps */
};

struct ccb_pathinq_settings_sas {
	u_int32_t bitrate;	/* Mbps */
};

struct ccb_pathinq_settings_nvme {
	uint32_t nsid;		/* Namespace ID for this path */
	uint32_t domain;
	uint8_t  bus;
	uint8_t  slot;
	uint8_t  function;
	uint8_t  extra;
};

#define	PATHINQ_SETTINGS_SIZE	128

struct ccb_pathinq {
	struct 	    ccb_hdr ccb_h;
	u_int8_t    version_num;	/* Version number for the SIM/HBA */
	u_int8_t    hba_inquiry;	/* Mimic of INQ byte 7 for the HBA */
	u_int16_t   target_sprt;	/* Flags for target mode support */
	u_int32_t   hba_misc;		/* Misc HBA features */
	u_int16_t   hba_eng_cnt;	/* HBA engine count */
					/* Vendor Unique capabilities */
	u_int8_t    vuhba_flags[VUHBALEN];
	u_int32_t   max_target;		/* Maximum supported Target */
	u_int32_t   max_lun;		/* Maximum supported Lun */
	u_int32_t   async_flags;	/* Installed Async handlers */
	path_id_t   hpath_id;		/* Highest Path ID in the subsystem */
	target_id_t initiator_id;	/* ID of the HBA on the SCSI bus */
	char	    sim_vid[SIM_IDLEN];	/* Vendor ID of the SIM */
	char	    hba_vid[HBA_IDLEN];	/* Vendor ID of the HBA */
	char 	    dev_name[DEV_IDLEN];/* Device name for SIM */
	u_int32_t   unit_number;	/* Unit number for SIM */
	u_int32_t   bus_id;		/* Bus ID for SIM */
	u_int32_t   base_transfer_speed;/* Base bus speed in KB/sec */
	cam_proto   protocol;
	u_int	    protocol_version;
	cam_xport   transport;
	u_int	    transport_version;
	union {
		struct ccb_pathinq_settings_spi spi;
		struct ccb_pathinq_settings_fc fc;
		struct ccb_pathinq_settings_sas sas;
		struct ccb_pathinq_settings_nvme nvme;
		char ccb_pathinq_settings_opaque[PATHINQ_SETTINGS_SIZE];
	} xport_specific;
	u_int		maxio;		/* Max supported I/O size, in bytes. */
	u_int16_t	hba_vendor;	/* HBA vendor ID */
	u_int16_t	hba_device;	/* HBA device ID */
	u_int16_t	hba_subvendor;	/* HBA subvendor ID */
	u_int16_t	hba_subdevice;	/* HBA subdevice ID */
};

/* Path Statistics CCB */
struct ccb_pathstats {
	struct	ccb_hdr	ccb_h;
	struct	timeval last_reset;	/* Time of last bus reset/loop init */
};

typedef enum {
	SMP_FLAG_NONE		= 0x00,
	SMP_FLAG_REQ_SG		= 0x01,
	SMP_FLAG_RSP_SG		= 0x02
} ccb_smp_pass_flags;

/*
 * Serial Management Protocol CCB
 * XXX Currently the semantics for this CCB are that it is executed either
 * by the addressed device, or that device's parent (i.e. an expander for
 * any device on an expander) if the addressed device doesn't support SMP.
 * Later, once we have the ability to probe SMP-only devices and put them
 * in CAM's topology, the CCB will only be executed by the addressed device
 * if possible.
 */
struct ccb_smpio {
	struct ccb_hdr		ccb_h;
	uint8_t			*smp_request;
	int			smp_request_len;
	uint16_t		smp_request_sglist_cnt;
	uint8_t			*smp_response;
	int			smp_response_len;
	uint16_t		smp_response_sglist_cnt;
	ccb_smp_pass_flags	flags;
};

typedef union {
	u_int8_t *sense_ptr;		/*
					 * Pointer to storage
					 * for sense information
					 */
	                                /* Storage Area for sense information */
	struct	 scsi_sense_data sense_buf;
} sense_t;

typedef union {
	u_int8_t  *cdb_ptr;		/* Pointer to the CDB bytes to send */
					/* Area for the CDB send */
	u_int8_t  cdb_bytes[IOCDBLEN];
} cdb_t;

/*
 * SCSI I/O Request CCB used for the XPT_SCSI_IO and XPT_CONT_TARGET_IO
 * function codes.
 */
struct ccb_scsiio {
	struct	   ccb_hdr ccb_h;
	union	   ccb *next_ccb;	/* Ptr for next CCB for action */
	u_int8_t   *req_map;		/* Ptr to mapping info */
	u_int8_t   *data_ptr;		/* Ptr to the data buf/SG list */
	u_int32_t  dxfer_len;		/* Data transfer length */
					/* Autosense storage */
	struct     scsi_sense_data sense_data;
	u_int8_t   sense_len;		/* Number of bytes to autosense */
	u_int8_t   cdb_len;		/* Number of bytes for the CDB */
	u_int16_t  sglist_cnt;		/* Number of SG list entries */
	u_int8_t   scsi_status;		/* Returned SCSI status */
	u_int8_t   sense_resid;		/* Autosense resid length: 2's comp */
	u_int32_t  resid;		/* Transfer residual length: 2's comp */
	cdb_t	   cdb_io;		/* Union for CDB bytes/pointer */
	u_int8_t   *msg_ptr;		/* Pointer to the message buffer */
	u_int16_t  msg_len;		/* Number of bytes for the Message */
	u_int8_t   tag_action;		/* What to do for tag queueing */
	/*
	 * The tag action should be either the define below (to send a
	 * non-tagged transaction) or one of the defined scsi tag messages
	 * from scsi_message.h.
	 */
#define		CAM_TAG_ACTION_NONE	0x00
	u_int	   tag_id;		/* tag id from initator (target mode) */
	u_int	   init_id;		/* initiator id of who selected */
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	struct bio *bio;		/* Associated bio */
#endif
};

static __inline uint8_t *
scsiio_cdb_ptr(struct ccb_scsiio *ccb)
{
	return ((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
	    ccb->cdb_io.cdb_ptr : ccb->cdb_io.cdb_bytes);
}

/*
 * ATA I/O Request CCB used for the XPT_ATA_IO function code.
 */
struct ccb_ataio {
	struct	   ccb_hdr ccb_h;
	union	   ccb *next_ccb;	/* Ptr for next CCB for action */
	struct ata_cmd	cmd;		/* ATA command register set */
	struct ata_res	res;		/* ATA result register set */
	u_int8_t   *data_ptr;		/* Ptr to the data buf/SG list */
	u_int32_t  dxfer_len;		/* Data transfer length */
	u_int32_t  resid;		/* Transfer residual length: 2's comp */
	u_int8_t   ata_flags;		/* Flags for the rest of the buffer */
#define ATA_FLAG_AUX 0x1
	uint32_t   aux;
	uint32_t   unused;
};

/*
 * MMC I/O Request CCB used for the XPT_MMC_IO function code.
 */
struct ccb_mmcio {
	struct	   ccb_hdr ccb_h;
	union	   ccb *next_ccb;	/* Ptr for next CCB for action */
	struct mmc_command cmd;
        struct mmc_command stop;
};

struct ccb_accept_tio {
	struct	   ccb_hdr ccb_h;
	cdb_t	   cdb_io;		/* Union for CDB bytes/pointer */
	u_int8_t   cdb_len;		/* Number of bytes for the CDB */
	u_int8_t   tag_action;		/* What to do for tag queueing */
	u_int8_t   sense_len;		/* Number of bytes of Sense Data */
	u_int      tag_id;		/* tag id from initator (target mode) */
	u_int      init_id;		/* initiator id of who selected */
	struct     scsi_sense_data sense_data;
};

static __inline uint8_t *
atio_cdb_ptr(struct ccb_accept_tio *ccb)
{
	return ((ccb->ccb_h.flags & CAM_CDB_POINTER) ?
	    ccb->cdb_io.cdb_ptr : ccb->cdb_io.cdb_bytes);
}

/* Release SIM Queue */
struct ccb_relsim {
	struct ccb_hdr ccb_h;
	u_int32_t      release_flags;
#define RELSIM_ADJUST_OPENINGS		0x01
#define RELSIM_RELEASE_AFTER_TIMEOUT	0x02
#define RELSIM_RELEASE_AFTER_CMDCMPLT	0x04
#define RELSIM_RELEASE_AFTER_QEMPTY	0x08
	u_int32_t      openings;
	u_int32_t      release_timeout;	/* Abstract argument. */
	u_int32_t      qfrozen_cnt;
};

/*
 * NVMe I/O Request CCB used for the XPT_NVME_IO and XPT_NVME_ADMIN function codes.
 */
struct ccb_nvmeio {
	struct	   ccb_hdr ccb_h;
	union	   ccb *next_ccb;	/* Ptr for next CCB for action */
	struct nvme_command cmd;	/* NVME command, per NVME standard */
	struct nvme_completion cpl;	/* NVME completion, per NVME standard */
	uint8_t   *data_ptr;		/* Ptr to the data buf/SG list */
	uint32_t  dxfer_len;		/* Data transfer length */
	uint16_t  sglist_cnt;		/* Number of SG list entries */
	uint16_t  unused;		/* padding for removed uint32_t */
};

/*
 * Definitions for the asynchronous callback CCB fields.
 */
typedef enum {
	AC_UNIT_ATTENTION	= 0x4000,/* Device reported UNIT ATTENTION */
	AC_ADVINFO_CHANGED	= 0x2000,/* Advance info might have changes */
	AC_CONTRACT		= 0x1000,/* A contractual callback */
	AC_GETDEV_CHANGED	= 0x800,/* Getdev info might have changed */
	AC_INQ_CHANGED		= 0x400,/* Inquiry info might have changed */
	AC_TRANSFER_NEG		= 0x200,/* New transfer settings in effect */
	AC_LOST_DEVICE		= 0x100,/* A device went away */
	AC_FOUND_DEVICE		= 0x080,/* A new device was found */
	AC_PATH_DEREGISTERED	= 0x040,/* A path has de-registered */
	AC_PATH_REGISTERED	= 0x020,/* A new path has been registered */
	AC_SENT_BDR		= 0x010,/* A BDR message was sent to target */
	AC_SCSI_AEN		= 0x008,/* A SCSI AEN has been received */
	AC_UNSOL_RESEL		= 0x002,/* Unsolicited reselection occurred */
	AC_BUS_RESET		= 0x001	/* A SCSI bus reset occurred */
} ac_code;

typedef void ac_callback_t (void *softc, u_int32_t code,
			    struct cam_path *path, void *args);

/*
 * Generic Asynchronous callbacks.
 *
 * Generic arguments passed bac which are then interpreted between a per-system
 * contract number.
 */
#define	AC_CONTRACT_DATA_MAX (128 - sizeof (u_int64_t))
struct ac_contract {
	u_int64_t	contract_number;
	u_int8_t	contract_data[AC_CONTRACT_DATA_MAX];
};

#define	AC_CONTRACT_DEV_CHG	1
struct ac_device_changed {
	u_int64_t	wwpn;
	u_int32_t	port;
	target_id_t	target;
	u_int8_t	arrived;
};

/* Set Asynchronous Callback CCB */
struct ccb_setasync {
	struct ccb_hdr	 ccb_h;
	u_int32_t	 event_enable;	/* Async Event enables */
	ac_callback_t	*callback;
	void		*callback_arg;
};

/* Set Device Type CCB */
struct ccb_setdev {
	struct	   ccb_hdr ccb_h;
	u_int8_t   dev_type;	/* Value for dev type field in EDT */
};

/* SCSI Control Functions */

/* Abort XPT request CCB */
struct ccb_abort {
	struct 	ccb_hdr ccb_h;
	union	ccb *abort_ccb;	/* Pointer to CCB to abort */
};

/* Reset SCSI Bus CCB */
struct ccb_resetbus {
	struct	ccb_hdr ccb_h;
};

/* Reset SCSI Device CCB */
struct ccb_resetdev {
	struct	ccb_hdr ccb_h;
};

/* Terminate I/O Process Request CCB */
struct ccb_termio {
	struct	ccb_hdr ccb_h;
	union	ccb *termio_ccb;	/* Pointer to CCB to terminate */
};

typedef enum {
	CTS_TYPE_CURRENT_SETTINGS,
	CTS_TYPE_USER_SETTINGS
} cts_type;

struct ccb_trans_settings_scsi
{
	u_int	valid;	/* Which fields to honor */
#define	CTS_SCSI_VALID_TQ		0x01
	u_int	flags;
#define	CTS_SCSI_FLAGS_TAG_ENB		0x01
};

struct ccb_trans_settings_ata
{
	u_int	valid;	/* Which fields to honor */
#define	CTS_ATA_VALID_TQ		0x01
	u_int	flags;
#define	CTS_ATA_FLAGS_TAG_ENB		0x01
};

struct ccb_trans_settings_spi
{
	u_int	  valid;	/* Which fields to honor */
#define	CTS_SPI_VALID_SYNC_RATE		0x01
#define	CTS_SPI_VALID_SYNC_OFFSET	0x02
#define	CTS_SPI_VALID_BUS_WIDTH		0x04
#define	CTS_SPI_VALID_DISC		0x08
#define CTS_SPI_VALID_PPR_OPTIONS	0x10
	u_int	flags;
#define	CTS_SPI_FLAGS_DISC_ENB		0x01
	u_int	sync_period;
	u_int	sync_offset;
	u_int	bus_width;
	u_int	ppr_options;
};

struct ccb_trans_settings_fc {
	u_int     	valid;		/* Which fields to honor */
#define	CTS_FC_VALID_WWNN		0x8000
#define	CTS_FC_VALID_WWPN		0x4000
#define	CTS_FC_VALID_PORT		0x2000
#define	CTS_FC_VALID_SPEED		0x1000
	u_int64_t	wwnn;		/* world wide node name */
	u_int64_t 	wwpn;		/* world wide port name */
	u_int32_t 	port;		/* 24 bit port id, if known */
	u_int32_t 	bitrate;	/* Mbps */
};

struct ccb_trans_settings_sas {
	u_int     	valid;		/* Which fields to honor */
#define	CTS_SAS_VALID_SPEED		0x1000
	u_int32_t 	bitrate;	/* Mbps */
};

struct ccb_trans_settings_pata {
	u_int     	valid;		/* Which fields to honor */
#define	CTS_ATA_VALID_MODE		0x01
#define	CTS_ATA_VALID_BYTECOUNT		0x02
#define	CTS_ATA_VALID_ATAPI		0x20
#define	CTS_ATA_VALID_CAPS		0x40
	int		mode;		/* Mode */
	u_int 		bytecount;	/* Length of PIO transaction */
	u_int 		atapi;		/* Length of ATAPI CDB */
	u_int 		caps;		/* Device and host SATA caps. */
#define	CTS_ATA_CAPS_H			0x0000ffff
#define	CTS_ATA_CAPS_H_DMA48		0x00000001 /* 48-bit DMA */
#define	CTS_ATA_CAPS_D			0xffff0000
};

struct ccb_trans_settings_sata {
	u_int     	valid;		/* Which fields to honor */
#define	CTS_SATA_VALID_MODE		0x01
#define	CTS_SATA_VALID_BYTECOUNT	0x02
#define	CTS_SATA_VALID_REVISION		0x04
#define	CTS_SATA_VALID_PM		0x08
#define	CTS_SATA_VALID_TAGS		0x10
#define	CTS_SATA_VALID_ATAPI		0x20
#define	CTS_SATA_VALID_CAPS		0x40
	int		mode;		/* Legacy PATA mode */
	u_int 		bytecount;	/* Length of PIO transaction */
	int		revision;	/* SATA revision */
	u_int 		pm_present;	/* PM is present (XPT->SIM) */
	u_int 		tags;		/* Number of allowed tags */
	u_int 		atapi;		/* Length of ATAPI CDB */
	u_int 		caps;		/* Device and host SATA caps. */
#define	CTS_SATA_CAPS_H			0x0000ffff
#define	CTS_SATA_CAPS_H_PMREQ		0x00000001
#define	CTS_SATA_CAPS_H_APST		0x00000002
#define	CTS_SATA_CAPS_H_DMAAA		0x00000010 /* Auto-activation */
#define	CTS_SATA_CAPS_H_AN		0x00000020 /* Async. notification */
#define	CTS_SATA_CAPS_D			0xffff0000
#define	CTS_SATA_CAPS_D_PMREQ		0x00010000
#define	CTS_SATA_CAPS_D_APST		0x00020000
};

struct ccb_trans_settings_nvme 
{
	u_int     	valid;		/* Which fields to honor */
#define CTS_NVME_VALID_SPEC	0x01
#define CTS_NVME_VALID_CAPS	0x02
#define CTS_NVME_VALID_LINK	0x04
	uint32_t	spec;		/* NVMe spec implemented -- same as vs register */
	uint32_t	max_xfer;	/* Max transfer size (0 -> unlimited */
	uint32_t	caps;
	uint8_t		lanes;		/* Number of PCIe lanes */
	uint8_t		speed;		/* PCIe generation for each lane */
	uint8_t		max_lanes;	/* Number of PCIe lanes */
	uint8_t		max_speed;	/* PCIe generation for each lane */
};

#include <cam/mmc/mmc_bus.h>
struct ccb_trans_settings_mmc {
	struct mmc_ios ios;
#define MMC_CLK		(1 << 1)
#define MMC_VDD		(1 << 2)
#define MMC_CS		(1 << 3)
#define MMC_BW		(1 << 4)
#define MMC_PM		(1 << 5)
#define MMC_BT		(1 << 6)
#define MMC_BM		(1 << 7)
	uint32_t ios_valid;
/* The folowing is used only for GET_TRAN_SETTINGS */
	uint32_t	host_ocr;
	int host_f_min;
	int host_f_max;
#define MMC_CAP_4_BIT_DATA	(1 << 0) /* Can do 4-bit data transfers */
#define MMC_CAP_8_BIT_DATA	(1 << 1) /* Can do 8-bit data transfers */
#define MMC_CAP_HSPEED		(1 << 2) /* Can do High Speed transfers */
	uint32_t host_caps;
};

/* Get/Set transfer rate/width/disconnection/tag queueing settings */
struct ccb_trans_settings {
	struct	  ccb_hdr ccb_h;
	cts_type  type;		/* Current or User settings */
	cam_proto protocol;
	u_int	  protocol_version;
	cam_xport transport;
	u_int	  transport_version;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_trans_settings_ata ata;
		struct ccb_trans_settings_scsi scsi;
		struct ccb_trans_settings_nvme nvme;
		struct ccb_trans_settings_mmc mmc;
	} proto_specific;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_trans_settings_spi spi;
		struct ccb_trans_settings_fc fc;
		struct ccb_trans_settings_sas sas;
		struct ccb_trans_settings_pata ata;
		struct ccb_trans_settings_sata sata;
		struct ccb_trans_settings_nvme nvme;
	} xport_specific;
};


/*
 * Calculate the geometry parameters for a device
 * give the block size and volume size in blocks.
 */
struct ccb_calc_geometry {
	struct	  ccb_hdr ccb_h;
	u_int32_t block_size;
	u_int64_t volume_size;
	u_int32_t cylinders;
	u_int8_t  heads;
	u_int8_t  secs_per_track;
};

/*
 * Set or get SIM (and transport) specific knobs
 */

#define	KNOB_VALID_ADDRESS	0x1
#define	KNOB_VALID_ROLE		0x2


#define	KNOB_ROLE_NONE		0x0
#define	KNOB_ROLE_INITIATOR	0x1
#define	KNOB_ROLE_TARGET	0x2
#define	KNOB_ROLE_BOTH		0x3

struct ccb_sim_knob_settings_spi {
	u_int		valid;
	u_int		initiator_id;
	u_int		role;
};

struct ccb_sim_knob_settings_fc {
	u_int		valid;
	u_int64_t	wwnn;		/* world wide node name */
	u_int64_t 	wwpn;		/* world wide port name */
	u_int		role;
};

struct ccb_sim_knob_settings_sas {
	u_int		valid;
	u_int64_t	wwnn;		/* world wide node name */
	u_int		role;
};
#define	KNOB_SETTINGS_SIZE	128

struct ccb_sim_knob {
	struct	  ccb_hdr ccb_h;
	union {
		u_int  valid;	/* Which fields to honor */
		struct ccb_sim_knob_settings_spi spi;
		struct ccb_sim_knob_settings_fc fc;
		struct ccb_sim_knob_settings_sas sas;
		char pad[KNOB_SETTINGS_SIZE];
	} xport_specific;
};

/*
 * Rescan the given bus, or bus/target/lun
 */
struct ccb_rescan {
	struct	ccb_hdr ccb_h;
	cam_flags	flags;
};

/*
 * Turn on debugging for the given bus, bus/target, or bus/target/lun.
 */
struct ccb_debug {
	struct	ccb_hdr ccb_h;
	cam_debug_flags flags;
};

/* Target mode structures. */

struct ccb_en_lun {
	struct	  ccb_hdr ccb_h;
	u_int16_t grp6_len;		/* Group 6 VU CDB length */
	u_int16_t grp7_len;		/* Group 7 VU CDB length */
	u_int8_t  enable;
};

/* old, barely used immediate notify, binary compatibility */
struct ccb_immed_notify {
	struct	  ccb_hdr ccb_h;
	struct    scsi_sense_data sense_data;
	u_int8_t  sense_len;		/* Number of bytes in sense buffer */
	u_int8_t  initiator_id;		/* Id of initiator that selected */
	u_int8_t  message_args[7];	/* Message Arguments */
};

struct ccb_notify_ack {
	struct	  ccb_hdr ccb_h;
	u_int16_t seq_id;		/* Sequence identifier */
	u_int8_t  event;		/* Event flags */
};

struct ccb_immediate_notify {
	struct    ccb_hdr ccb_h;
	u_int     tag_id;		/* Tag for immediate notify */
	u_int     seq_id;		/* Tag for target of notify */
	u_int     initiator_id;		/* Initiator Identifier */
	u_int     arg;			/* Function specific */
};

struct ccb_notify_acknowledge {
	struct    ccb_hdr ccb_h;
	u_int     tag_id;		/* Tag for immediate notify */
	u_int     seq_id;		/* Tar for target of notify */
	u_int     initiator_id;		/* Initiator Identifier */
	u_int     arg;			/* Response information */
	/*
	 * Lower byte of arg is one of RESPONSE CODE values defined below
	 * (subset of response codes from SPL-4 and FCP-4 specifications),
	 * upper 3 bytes is code-specific ADDITIONAL RESPONSE INFORMATION.
	 */
#define	CAM_RSP_TMF_COMPLETE		0x00
#define	CAM_RSP_TMF_REJECTED		0x04
#define	CAM_RSP_TMF_FAILED		0x05
#define	CAM_RSP_TMF_SUCCEEDED		0x08
#define	CAM_RSP_TMF_INCORRECT_LUN	0x09
};

/* HBA engine structures. */

typedef enum {
	EIT_BUFFER,	/* Engine type: buffer memory */
	EIT_LOSSLESS,	/* Engine type: lossless compression */
	EIT_LOSSY,	/* Engine type: lossy compression */
	EIT_ENCRYPT	/* Engine type: encryption */
} ei_type;

typedef enum {
	EAD_VUNIQUE,	/* Engine algorithm ID: vendor unique */
	EAD_LZ1V1,	/* Engine algorithm ID: LZ1 var.1 */
	EAD_LZ2V1,	/* Engine algorithm ID: LZ2 var.1 */
	EAD_LZ2V2	/* Engine algorithm ID: LZ2 var.2 */
} ei_algo;

struct ccb_eng_inq {
	struct	  ccb_hdr ccb_h;
	u_int16_t eng_num;	/* The engine number for this inquiry */
	ei_type   eng_type;	/* Returned engine type */
	ei_algo   eng_algo;	/* Returned engine algorithm type */
	u_int32_t eng_memeory;	/* Returned engine memory size */
};

struct ccb_eng_exec {	/* This structure must match SCSIIO size */
	struct	  ccb_hdr ccb_h;
	u_int8_t  *pdrv_ptr;	/* Ptr used by the peripheral driver */
	u_int8_t  *req_map;	/* Ptr for mapping info on the req. */
	u_int8_t  *data_ptr;	/* Pointer to the data buf/SG list */
	u_int32_t dxfer_len;	/* Data transfer length */
	u_int8_t  *engdata_ptr;	/* Pointer to the engine buffer data */
	u_int16_t sglist_cnt;	/* Num of scatter gather list entries */
	u_int32_t dmax_len;	/* Destination data maximum length */
	u_int32_t dest_len;	/* Destination data length */
	int32_t	  src_resid;	/* Source residual length: 2's comp */
	u_int32_t timeout;	/* Timeout value */
	u_int16_t eng_num;	/* Engine number for this request */
	u_int16_t vu_flags;	/* Vendor Unique flags */
};

/*
 * Definitions for the timeout field in the SCSI I/O CCB.
 */
#define	CAM_TIME_DEFAULT	0x00000000	/* Use SIM default value */
#define	CAM_TIME_INFINITY	0xFFFFFFFF	/* Infinite timeout */

#define	CAM_SUCCESS	0	/* For signaling general success */
#define	CAM_FAILURE	1	/* For signaling general failure */

#define CAM_FALSE	0
#define CAM_TRUE	1

#define XPT_CCB_INVALID	-1	/* for signaling a bad CCB to free */

/*
 * CCB for working with advanced device information.  This operates in a fashion
 * similar to XPT_GDEV_TYPE.  Specify the target in ccb_h, the buffer
 * type requested, and provide a buffer size/buffer to write to.  If the
 * buffer is too small, provsiz will be larger than bufsiz.
 */
struct ccb_dev_advinfo {
	struct ccb_hdr ccb_h;
	uint32_t flags;
#define	CDAI_FLAG_NONE		0x0	/* No flags set */
#define	CDAI_FLAG_STORE		0x1	/* If set, action becomes store */
	uint32_t buftype;		/* IN: Type of data being requested */
	/* NB: buftype is interpreted on a per-transport basis */
#define	CDAI_TYPE_SCSI_DEVID	1
#define	CDAI_TYPE_SERIAL_NUM	2
#define	CDAI_TYPE_PHYS_PATH	3
#define	CDAI_TYPE_RCAPLONG	4
#define	CDAI_TYPE_EXT_INQ	5
#define	CDAI_TYPE_NVME_CNTRL	6	/* NVMe Identify Controller data */
#define	CDAI_TYPE_NVME_NS	7	/* NVMe Identify Namespace data */
#define	CDAI_TYPE_MMC_PARAMS	8	/* MMC/SD ident */
	off_t bufsiz;			/* IN: Size of external buffer */
#define	CAM_SCSI_DEVID_MAXLEN	65536	/* length in buffer is an uint16_t */
	off_t provsiz;			/* OUT: Size required/used */
	uint8_t *buf;			/* IN/OUT: Buffer for requested data */
};

/*
 * CCB for sending async events
 */
struct ccb_async {
	struct ccb_hdr ccb_h;
	uint32_t async_code;
	off_t async_arg_size;
	void *async_arg_ptr;
};

/*
 * Union of all CCB types for kernel space allocation.  This union should
 * never be used for manipulating CCBs - its only use is for the allocation
 * and deallocation of raw CCB space and is the return type of xpt_ccb_alloc
 * and the argument to xpt_ccb_free.
 */
union ccb {
	struct	ccb_hdr			ccb_h;	/* For convenience */
	struct	ccb_scsiio		csio;
	struct	ccb_getdev		cgd;
	struct	ccb_getdevlist		cgdl;
	struct	ccb_pathinq		cpi;
	struct	ccb_relsim		crs;
	struct	ccb_setasync		csa;
	struct	ccb_setdev		csd;
	struct	ccb_pathstats		cpis;
	struct	ccb_getdevstats		cgds;
	struct	ccb_dev_match		cdm;
	struct	ccb_trans_settings	cts;
	struct	ccb_calc_geometry	ccg;
	struct	ccb_sim_knob		knob;
	struct	ccb_abort		cab;
	struct	ccb_resetbus		crb;
	struct	ccb_resetdev		crd;
	struct	ccb_termio		tio;
	struct	ccb_accept_tio		atio;
	struct	ccb_scsiio		ctio;
	struct	ccb_en_lun		cel;
	struct	ccb_immed_notify	cin;
	struct	ccb_notify_ack		cna;
	struct	ccb_immediate_notify	cin1;
	struct	ccb_notify_acknowledge	cna2;
	struct	ccb_eng_inq		cei;
	struct	ccb_eng_exec		cee;
	struct	ccb_smpio		smpio;
	struct 	ccb_rescan		crcn;
	struct  ccb_debug		cdbg;
	struct	ccb_ataio		ataio;
	struct	ccb_dev_advinfo		cdai;
	struct	ccb_async		casync;
	struct	ccb_nvmeio		nvmeio;
	struct	ccb_mmcio		mmcio;
};

#define CCB_CLEAR_ALL_EXCEPT_HDR(ccbp)			\
	bzero((char *)(ccbp) + sizeof((ccbp)->ccb_h),	\
	    sizeof(*(ccbp)) - sizeof((ccbp)->ccb_h))

__BEGIN_DECLS
static __inline void
cam_fill_csio(struct ccb_scsiio *csio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int32_t flags, u_int8_t tag_action,
	      u_int8_t *data_ptr, u_int32_t dxfer_len,
	      u_int8_t sense_len, u_int8_t cdb_len,
	      u_int32_t timeout)
{
	csio->ccb_h.func_code = XPT_SCSI_IO;
	csio->ccb_h.flags = flags;
	csio->ccb_h.xflags = 0;
	csio->ccb_h.retry_count = retries;
	csio->ccb_h.cbfcnp = cbfcnp;
	csio->ccb_h.timeout = timeout;
	csio->data_ptr = data_ptr;
	csio->dxfer_len = dxfer_len;
	csio->sense_len = sense_len;
	csio->cdb_len = cdb_len;
	csio->tag_action = tag_action;
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	csio->bio = NULL;
#endif
}

static __inline void
cam_fill_ctio(struct ccb_scsiio *csio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int32_t flags, u_int tag_action, u_int tag_id,
	      u_int init_id, u_int scsi_status, u_int8_t *data_ptr,
	      u_int32_t dxfer_len, u_int32_t timeout)
{
	csio->ccb_h.func_code = XPT_CONT_TARGET_IO;
	csio->ccb_h.flags = flags;
	csio->ccb_h.xflags = 0;
	csio->ccb_h.retry_count = retries;
	csio->ccb_h.cbfcnp = cbfcnp;
	csio->ccb_h.timeout = timeout;
	csio->data_ptr = data_ptr;
	csio->dxfer_len = dxfer_len;
	csio->scsi_status = scsi_status;
	csio->tag_action = tag_action;
	csio->tag_id = tag_id;
	csio->init_id = init_id;
}

static __inline void
cam_fill_ataio(struct ccb_ataio *ataio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int32_t flags, u_int tag_action __unused,
	      u_int8_t *data_ptr, u_int32_t dxfer_len,
	      u_int32_t timeout)
{
	ataio->ccb_h.func_code = XPT_ATA_IO;
	ataio->ccb_h.flags = flags;
	ataio->ccb_h.retry_count = retries;
	ataio->ccb_h.cbfcnp = cbfcnp;
	ataio->ccb_h.timeout = timeout;
	ataio->data_ptr = data_ptr;
	ataio->dxfer_len = dxfer_len;
	ataio->ata_flags = 0;
}

static __inline void
cam_fill_smpio(struct ccb_smpio *smpio, uint32_t retries,
	       void (*cbfcnp)(struct cam_periph *, union ccb *), uint32_t flags,
	       uint8_t *smp_request, int smp_request_len,
	       uint8_t *smp_response, int smp_response_len,
	       uint32_t timeout)
{
#ifdef _KERNEL
	KASSERT((flags & CAM_DIR_MASK) == CAM_DIR_BOTH,
		("direction != CAM_DIR_BOTH"));
	KASSERT((smp_request != NULL) && (smp_response != NULL),
		("need valid request and response buffers"));
	KASSERT((smp_request_len != 0) && (smp_response_len != 0),
		("need non-zero request and response lengths"));
#endif /*_KERNEL*/
	smpio->ccb_h.func_code = XPT_SMP_IO;
	smpio->ccb_h.flags = flags;
	smpio->ccb_h.retry_count = retries;
	smpio->ccb_h.cbfcnp = cbfcnp;
	smpio->ccb_h.timeout = timeout;
	smpio->smp_request = smp_request;
	smpio->smp_request_len = smp_request_len;
	smpio->smp_response = smp_response;
	smpio->smp_response_len = smp_response_len;
}

static __inline void
cam_fill_mmcio(struct ccb_mmcio *mmcio, uint32_t retries,
	       void (*cbfcnp)(struct cam_periph *, union ccb *), uint32_t flags,
	       uint32_t mmc_opcode, uint32_t mmc_arg, uint32_t mmc_flags,
	       struct mmc_data *mmc_d,
	       uint32_t timeout)
{
	mmcio->ccb_h.func_code = XPT_MMC_IO;
	mmcio->ccb_h.flags = flags;
	mmcio->ccb_h.retry_count = retries;
	mmcio->ccb_h.cbfcnp = cbfcnp;
	mmcio->ccb_h.timeout = timeout;
	mmcio->cmd.opcode = mmc_opcode;
	mmcio->cmd.arg = mmc_arg;
	mmcio->cmd.flags = mmc_flags;
	mmcio->stop.opcode = 0;
	mmcio->stop.arg = 0;
	mmcio->stop.flags = 0;
	if (mmc_d != NULL) {
		mmcio->cmd.data = mmc_d;
	} else
		mmcio->cmd.data = NULL;
	mmcio->cmd.resp[0] = 0;
	mmcio->cmd.resp[1] = 0;
	mmcio->cmd.resp[2] = 0;
	mmcio->cmd.resp[3] = 0;
}

static __inline void
cam_set_ccbstatus(union ccb *ccb, cam_status status)
{
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	ccb->ccb_h.status |= status;
}

static __inline cam_status
cam_ccb_status(union ccb *ccb)
{
	return ((cam_status)(ccb->ccb_h.status & CAM_STATUS_MASK));
}

void cam_calc_geometry(struct ccb_calc_geometry *ccg, int extended);

static __inline void
cam_fill_nvmeio(struct ccb_nvmeio *nvmeio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int32_t flags, u_int8_t *data_ptr, u_int32_t dxfer_len,
	      u_int32_t timeout)
{
	nvmeio->ccb_h.func_code = XPT_NVME_IO;
	nvmeio->ccb_h.flags = flags;
	nvmeio->ccb_h.retry_count = retries;
	nvmeio->ccb_h.cbfcnp = cbfcnp;
	nvmeio->ccb_h.timeout = timeout;
	nvmeio->data_ptr = data_ptr;
	nvmeio->dxfer_len = dxfer_len;
}

static __inline void
cam_fill_nvmeadmin(struct ccb_nvmeio *nvmeio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int32_t flags, u_int8_t *data_ptr, u_int32_t dxfer_len,
	      u_int32_t timeout)
{
	nvmeio->ccb_h.func_code = XPT_NVME_ADMIN;
	nvmeio->ccb_h.flags = flags;
	nvmeio->ccb_h.retry_count = retries;
	nvmeio->ccb_h.cbfcnp = cbfcnp;
	nvmeio->ccb_h.timeout = timeout;
	nvmeio->data_ptr = data_ptr;
	nvmeio->dxfer_len = dxfer_len;
}
__END_DECLS

#endif /* _CAM_CAM_CCB_H */
