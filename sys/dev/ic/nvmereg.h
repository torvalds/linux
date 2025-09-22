/*	$OpenBSD: nvmereg.h,v 1.16 2024/09/13 09:57:34 jmatthew Exp $ */

/*
 * Copyright (c) 2014 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define NVME_CAP	0x0000	/* Controller Capabilities */
#define  NVME_CAP_MPSMAX(_r)	(12 + (((_r) >> 52) & 0xf)) /* shift */
#define  NVME_CAP_MPSMIN(_r)	(12 + (((_r) >> 48) & 0xf)) /* shift */
#define  NVME_CAP_CSS(_r)	(((_r) >> 37) & 0x7f)
#define  NVME_CAP_CSS_NVM	(1 << 0)
#define  NVME_CAP_NSSRS(_r)	ISSET((_r), (1ULL << 36))
#define  NVME_CAP_DSTRD(_r)	(1 << (2 + (((_r) >> 32) & 0xf))) /* bytes */
#define  NVME_CAP_TO(_r)	(500 * (((_r) >> 24) & 0xff)) /* ms */
#define  NVME_CAP_AMS(_r)	(((_r) >> 17) & 0x3)
#define  NVME_CAP_AMS_WRR	(1 << 0)
#define  NVME_CAP_AMS_VENDOR	(1 << 1)
#define  NVME_CAP_CQR(_r)	ISSET((_r), (1 << 16))
#define  NVME_CAP_MQES(_r)	(((_r) & 0xffff) + 1)
#define NVME_CAP_LO	0x0000
#define NVME_CAP_HI	0x0004
#define NVME_VS		0x0008	/* Version */
#define  NVME_VS_MJR(_r)	(((_r) & 0xffff0000) >> 16)
#define  NVME_VS_MNR(_r)	(((_r) & 0x0000ff00) >> 8)
#define NVME_INTMS	0x000c	/* Interrupt Mask Set */
#define NVME_INTMC	0x0010	/* Interrupt Mask Clear */
#define NVME_CC		0x0014	/* Controller Configuration */
#define  NVME_CC_IOCQES(_v)	(((_v) & 0xf) << 20)
#define  NVME_CC_IOCQES_MASK	NVME_CC_IOCQES(0xf)
#define  NVME_CC_IOCQES_R(_v)	(((_v) >> 20) & 0xf)
#define  NVME_CC_IOSQES(_v)	(((_v) & 0xf) << 16)
#define  NVME_CC_IOSQES_MASK	NVME_CC_IOSQES(0xf)
#define  NVME_CC_IOSQES_R(_v)	(((_v) >> 16) & 0xf)
#define  NVME_CC_SHN(_v)	(((_v) & 0x3) << 14)
#define  NVME_CC_SHN_MASK	NVME_CC_SHN(0x3)
#define  NVME_CC_SHN_R(_v)	(((_v) >> 15) & 0x3)
#define  NVME_CC_SHN_NONE	0
#define  NVME_CC_SHN_NORMAL	1
#define  NVME_CC_SHN_ABRUPT	2
#define  NVME_CC_AMS(_v)	(((_v) & 0x7) << 11)
#define  NVME_CC_AMS_MASK	NVME_CC_AMS(0x7)
#define  NVME_CC_AMS_R(_v)	(((_v) >> 11) & 0xf)
#define  NVME_CC_AMS_RR		0 /* round-robin */
#define  NVME_CC_AMS_WRR_U	1 /* weighted round-robin w/ urgent */
#define  NVME_CC_AMS_VENDOR	7 /* vendor */
#define  NVME_CC_MPS(_v)	((((_v) - 12) & 0xf) << 7)
#define  NVME_CC_MPS_MASK	(0xf << 7)
#define  NVME_CC_MPS_R(_v)	(12 + (((_v) >> 7) & 0xf))
#define  NVME_CC_CSS(_v)	(((_v) & 0x7) << 4)
#define  NVME_CC_CSS_MASK	NVME_CC_CSS(0x7)
#define  NVME_CC_CSS_R(_v)	(((_v) >> 4) & 0x7)
#define  NVME_CC_CSS_NVM	0
#define  NVME_CC_EN		(1 << 0)
#define NVME_CSTS	0x001c	/* Controller Status */
#define  NVME_CSTS_SHST_MASK	(0x3 << 2)
#define  NVME_CSTS_SHST_NONE	(0x0 << 2) /* normal operation */
#define  NVME_CSTS_SHST_WAIT	(0x1 << 2) /* shutdown processing occurring */
#define  NVME_CSTS_SHST_DONE	(0x2 << 2) /* shutdown processing complete */
#define  NVME_CSTS_CFS		(1 << 1)
#define  NVME_CSTS_RDY		(1 << 0)
#define NVME_NSSR	0x0020	/* NVM Subsystem Reset (Optional) */
#define NVME_AQA	0x0024	/* Admin Queue Attributes */
				/* Admin Completion Queue Size */
#define  NVME_AQA_ACQS(_v)	(((_v) - 1) << 16)
				/* Admin Submission Queue Size */
#define  NVME_AQA_ASQS(_v)	(((_v) - 1) << 0)
#define NVME_ASQ	0x0028	/* Admin Submission Queue Base Address */
#define NVME_ACQ	0x0030	/* Admin Completion Queue Base Address */

#define NVME_ADMIN_Q		0
/* Submission Queue Tail Doorbell */
#define NVME_SQTDBL(_q, _s)	(0x1000 + (2 * (_q) + 0) * (_s))
/* Completion Queue Head Doorbell */
#define NVME_CQHDBL(_q, _s)	(0x1000 + (2 * (_q) + 1) * (_s))

struct nvme_sge {
	u_int8_t	id;
	u_int8_t	_reserved[15];
} __packed __aligned(8);

struct nvme_sge_data {
	u_int8_t	id;
	u_int8_t	_reserved[3];

	u_int32_t	length;

	u_int64_t	address;
} __packed __aligned(8);

struct nvme_sge_bit_bucket {
	u_int8_t	id;
	u_int8_t	_reserved[3];

	u_int32_t	length;

	u_int64_t	address;
} __packed __aligned(8);

struct nvme_sqe {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int16_t	cid;

	u_int32_t	nsid;

	u_int8_t	_reserved[8];

	u_int64_t	mptr;

	union {
		u_int64_t	prp[2];
		struct nvme_sge	sge;
	} __packed	entry;

	u_int32_t	cdw10;
	u_int32_t	cdw11;
	u_int32_t	cdw12;
	u_int32_t	cdw13;
	u_int32_t	cdw14;
	u_int32_t	cdw15;
} __packed __aligned(8);

struct nvme_sqe_q {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int16_t	cid;

	u_int8_t	_reserved1[20];

	u_int64_t	prp1;

	u_int8_t	_reserved2[8];

	u_int16_t	qid;
	u_int16_t	qsize;

	u_int8_t	qflags;
#define NVM_SQE_SQ_QPRIO_URG	(0x0 << 1)
#define NVM_SQE_SQ_QPRIO_HI	(0x1 << 1)
#define NVM_SQE_SQ_QPRIO_MED	(0x2 << 1)
#define NVM_SQE_SQ_QPRIO_LOW	(0x3 << 1)
#define NVM_SQE_CQ_IEN		(1 << 1)
#define NVM_SQE_Q_PC		(1 << 0)
	u_int8_t	_reserved3;
	u_int16_t	cqid; /* XXX interrupt vector for cq */

	u_int8_t	_reserved4[16];
} __packed __aligned(8);

struct nvme_sqe_io {
	u_int8_t	opcode;
	u_int8_t	flags;
	u_int16_t	cid;

	u_int32_t	nsid;

	u_int8_t	_reserved[8];

	u_int64_t	mptr;

	union {
		u_int64_t	prp[2];
		struct nvme_sge	sge;
	} __packed	entry;

	u_int64_t	slba;	/* Starting LBA */

	u_int16_t	nlb;	/* Number of Logical Blocks */
	u_int16_t	ioflags;

	u_int8_t	dsm;	/* Dataset Management */
	u_int8_t	_reserved2[3];

	u_int32_t	eilbrt;	/* Expected Initial Logical Block
				   Reference Tag */

	u_int16_t	elbat;	/* Expected Logical Block
				   Application Tag */
	u_int16_t	elbatm;	/* Expected Logical Block
				   Application Tag Mask */
} __packed __aligned(8);

struct nvme_cqe {
	u_int32_t	cdw0;

	u_int32_t	_reserved;

	u_int16_t	sqhd; /* SQ Head Pointer */
	u_int16_t	sqid; /* SQ Identifier */

	u_int16_t	cid; /* Command Identifier */
	u_int16_t	flags;
#define NVME_CQE_DNR		(1 << 15)
#define NVME_CQE_M		(1 << 14)
#define NVME_CQE_SCT(_f)	((_f) & (0x07 << 9))
#define  NVME_CQE_SCT_GENERIC		(0x00 << 9)
#define  NVME_CQE_SCT_COMMAND		(0x01 << 9)
#define  NVME_CQE_SCT_MEDIAERR		(0x02 << 9)
#define  NVME_CQE_SCT_VENDOR		(0x07 << 9)
#define NVME_CQE_SC(_f)		((_f) & (0xff << 1))
#define  NVME_CQE_SC_SUCCESS		(0x00 << 1)
#define  NVME_CQE_SC_INVALID_OPCODE	(0x01 << 1)
#define  NVME_CQE_SC_INVALID_FIELD	(0x02 << 1)
#define  NVME_CQE_SC_CID_CONFLICT	(0x03 << 1)
#define  NVME_CQE_SC_DATA_XFER_ERR	(0x04 << 1)
#define  NVME_CQE_SC_ABRT_BY_NO_PWR	(0x05 << 1)
#define  NVME_CQE_SC_INTERNAL_DEV_ERR	(0x06 << 1)
#define  NVME_CQE_SC_CMD_ABRT_REQD	(0x07 << 1)
#define  NVME_CQE_SC_CMD_ABDR_SQ_DEL	(0x08 << 1)
#define  NVME_CQE_SC_CMD_ABDR_FUSE_ERR	(0x09 << 1)
#define  NVME_CQE_SC_CMD_ABDR_FUSE_MISS	(0x0a << 1)
#define  NVME_CQE_SC_INVALID_NS		(0x0b << 1)
#define  NVME_CQE_SC_CMD_SEQ_ERR	(0x0c << 1)
#define  NVME_CQE_SC_INVALID_LAST_SGL	(0x0d << 1)
#define  NVME_CQE_SC_INVALID_NUM_SGL	(0x0e << 1)
#define  NVME_CQE_SC_DATA_SGL_LEN	(0x0f << 1)
#define  NVME_CQE_SC_MDATA_SGL_LEN	(0x10 << 1)
#define  NVME_CQE_SC_SGL_TYPE_INVALID	(0x11 << 1)
#define  NVME_CQE_SC_LBA_RANGE		(0x80 << 1)
#define  NVME_CQE_SC_CAP_EXCEEDED	(0x81 << 1)
#define  NVME_CQE_NS_NOT_RDY		(0x82 << 1)
#define  NVME_CQE_RSV_CONFLICT		(0x83 << 1)
#define NVME_CQE_PHASE		(1 << 0)
} __packed __aligned(8);

#define NVM_ADMIN_DEL_IOSQ	0x00 /* Delete I/O Submission Queue */
#define NVM_ADMIN_ADD_IOSQ	0x01 /* Create I/O Submission Queue */
#define NVM_ADMIN_GET_LOG_PG	0x02 /* Get Log Page */
#define NVM_ADMIN_DEL_IOCQ	0x04 /* Delete I/O Completion Queue */ 
#define NVM_ADMIN_ADD_IOCQ	0x05 /* Create I/O Completion Queue */ 
#define NVM_ADMIN_IDENTIFY	0x06 /* Identify */
#define NVM_ADMIN_ABORT		0x08 /* Abort */
#define NVM_ADMIN_SET_FEATURES	0x09 /* Set Features */
#define NVM_ADMIN_GET_FEATURES	0x0a /* Get Features */
#define NVM_ADMIN_ASYNC_EV_REQ	0x0c /* Asynchronous Event Request */
#define NVM_ADMIN_FW_ACTIVATE	0x10 /* Firmware Activate */
#define NVM_ADMIN_FW_DOWNLOAD	0x11 /* Firmware Image Download */
#define NVM_ADMIN_SELFTEST	0x14 /* Start self test */

#define NVM_CMD_FLUSH		0x00 /* Flush */
#define NVM_CMD_WRITE		0x01 /* Write */
#define NVM_CMD_READ		0x02 /* Read */
#define NVM_CMD_WR_UNCOR	0x04 /* Write Uncorrectable */
#define NVM_CMD_COMPARE		0x05 /* Compare */
#define NVM_CMD_DSM		0x09 /* Dataset Management */

/* Power State Descriptor Data */
struct nvm_identify_psd {
	u_int16_t	mp;		/* Max Power */
	u_int16_t	flags;

	u_int32_t	enlat;		/* Entry Latency */

	u_int32_t	exlat;		/* Exit Latency */

	u_int8_t	rrt;		/* Relative Read Throughput */
	u_int8_t	rrl;		/* Relative Read Latency */
	u_int8_t	rwt;		/* Relative Write Throughput */
	u_int8_t	rwl;		/* Relative Write Latency */

	u_int8_t	_reserved[16];
} __packed __aligned(8);

struct nvm_identify_controller {
	/* Controller Capabilities and Features */

	u_int16_t	vid;		/* PCI Vendor ID */
	u_int16_t	ssvid;		/* PCI Subsystem Vendor ID */

	u_int8_t	sn[20];		/* Serial Number */
	u_int8_t	mn[40];		/* Model Number */
	u_int8_t	fr[8];		/* Firmware Revision */

	u_int8_t	rab;		/* Recommended Arbitration Burst */
	u_int8_t	ieee[3];	/* IEEE OUI Identifier */

	u_int8_t	cmic;		/* Controller Multi-Path I/O and
					   Namespace Sharing Capabilities */
	u_int8_t	mdts;		/* Maximum Data Transfer Size */
	u_int16_t	cntlid;		/* Controller ID */

	u_int8_t	_reserved1[16];
	u_int32_t	ctratt;
#define NVM_ID_CTRL_CTRATT_FMT			"\020" \
	"\016DELEG" "\017DEVNVM" "\020ELBAS" "\005ENDURGRPS" \
	"\014FIXCAPMGMT" "\001HOSTID" "\013MDS" "\002NOPSPM" \
	"\010NSGRAN" "\003NVMSETS" "\006PREDLATENCY" "\004READRCVRY" \
	"\011SQASSOC" "\007TBKAS" "\012UUIDLIST" "\015VARCAPMGMT"

	u_int8_t	_reserved9[156];

	/* Admin Command Set Attributes & Optional Controller Capabilities */

	u_int16_t	oacs;		/* Optional Admin Command Support */
#define NVM_ID_CTRL_OACS_FMT			"\020" \
	"\013CAFL" "\011DBBC" "\006DIREC" "\005DST" "\012GLBAS" \
	"\002FORMAT" "\003FWCD" "\007MISR" "\004NSMGMT" "\001SECSR" \
	"\010VM"

	u_int8_t	acl;		/* Abort Command Limit */
	u_int8_t	aerl;		/* Asynchronous Event Request Limit */

	u_int8_t	frmw;		/* Firmware Updates */
	u_int8_t	lpa;		/* Log Page Attributes */
#define NVM_ID_CTRL_LPA_PE		(1 << 4)
	u_int8_t	elpe;		/* Error Log Page Entries */
	u_int8_t	npss;		/* Number of Power States Support */

	u_int8_t	avscc;		/* Admin Vendor Specific Command
					   Configuration */
	u_int8_t	apsta;		/* Autonomous Power State Transition
					   Attributes */

	u_int8_t	_reserved2[62];
	u_int32_t	sanicap;
#define NVM_ID_CTRL_SANICAP_FMT			"\020" \
	"\002BlockErase" "\001CryptoErase" "\003Overwrite"
	u_int8_t	_reserved10[180];

	/* NVM Command Set Attributes */

	u_int8_t	sqes;		/* Submission Queue Entry Size */
	u_int8_t	cqes;		/* Completion Queue Entry Size */
	u_int8_t	_reserved3[2];

	u_int32_t	nn;		/* Number of Namespaces */

	u_int16_t	oncs;		/* Optional NVM Command Support */
#define NVM_ID_CTRL_ONCS_FMT			"\020" \
	"\006RSV" "\001SCMP" "\011SCPY" "\003SDMGMT" "\005SF" \
	"\010SV" "\002SWU" "\004SWZ" "\007TS"

	u_int16_t	fuses;		/* Fused Operation Support */

	u_int8_t	fna;		/* Format NVM Attributes */
#define NVM_ID_CTRL_FNA_CRYPTOFORMAT		(1 << 2)
	u_int8_t	vwc;		/* Volatile Write Cache */
#define NVM_ID_CTRL_VWC_PRESENT			(1 << 0)
	u_int16_t	awun;		/* Atomic Write Unit Normal */

	u_int16_t	awupf;		/* Atomic Write Unit Power Fail */
	u_int8_t	nvscc;		/* NVM Vendor Specific Command */
	u_int8_t	_reserved4[1];

	u_int16_t	acwu;		/* Atomic Compare & Write Unit */
	u_int8_t	_reserved5[2];

	u_int32_t	sgls;		/* SGL Support */

	u_int8_t	_reserved6[164];

	/* I/O Command Set Attributes */

	u_int8_t	_reserved7[1344];

	/* Power State Descriptors */

	struct nvm_identify_psd psd[32]; /* Power State Descriptors */

	/* Vendor Specific */

	u_int8_t	_reserved8[1024];
} __packed __aligned(8);

struct nvm_namespace_format {
	u_int16_t	ms;		/* Metadata Size */
	u_int8_t	lbads;		/* LBA Data Size */
	u_int8_t	rp;		/* Relative Performance */
} __packed __aligned(4);

struct nvm_identify_namespace {
	u_int64_t	nsze;		/* Namespace Size */

	u_int64_t	ncap;		/* Namespace Capacity */

	u_int64_t	nuse;		/* Namespace Utilization */

	u_int8_t	nsfeat;		/* Namespace Features */
#define	NVME_ID_NS_NSFEAT_THIN_PROV	(1 << 0)
#define NVME_ID_NS_NSFEAT_FMT		"\020" \
	"\002NSABP" "\005OPTPERF" "\001THIN_PROV" "\004UIDREUSE" "\003DAE"

	u_int8_t	nlbaf;		/* Number of LBA Formats */
	u_int8_t	flbas;		/* Formatted LBA Size */
#define NVME_ID_NS_FLBAS(_f)		((_f) & 0x0f)
#define NVME_ID_NS_FLBAS_MD		0x10
	u_int8_t	mc;		/* Metadata Capabilities */

	u_int8_t	dpc;		/* End-to-end Data Protection
					   Capabilities */
	u_int8_t	dps;		/* End-to-end Data Protection Type Settings */
#define NVME_ID_NS_DPS_PIP		(1 << 3)
#define NVME_ID_NS_DPS_TYPE(_f)		((_f) & 0x7)

	u_int8_t	_reserved1[74];
	uint8_t		nguid[16];
	uint8_t		eui64[8];	/* BIG-endian */

	struct nvm_namespace_format
			lbaf[16];	/* LBA Format Support */

	u_int8_t	_reserved2[192];

	u_int8_t	vs[3712];
} __packed __aligned(8);

#define NVM_LOG_PAGE_SMART_HEALTH	0x02
struct nvm_smart_health {
	u_int8_t	critical_warning;
#define NVM_HEALTH_CW_SPARE		(1 << 0)
#define NVM_HEALTH_CW_TEMP		(1 << 1)
#define NVM_HEALTH_CW_MEDIA		(1 << 2)
#define NVM_HEALTH_CW_READONLY		(1 << 3)
#define NVM_HEALTH_CW_VOLATILE		(1 << 4)
#define NVM_HEALTH_CW_PMR		(1 << 5)
	u_int16_t	temperature;
	u_int8_t	avail_spare;
	u_int8_t	avail_spare_threshold;
	u_int8_t	percent_used;
	u_int8_t	end_grp_summary;	/* 1.4+ */

	u_int8_t	_reserved1[25];

	u_int64_t	data_units_read[2];
	u_int64_t	data_units_written[2];
	u_int64_t	host_read_commands[2];
	u_int64_t	host_write_commands[2];
	u_int64_t	busy_time[2];
	u_int64_t	power_cycles[2];
	u_int64_t	power_on_hours[2];
	u_int64_t	unsafe_shutdowns[2];
	u_int64_t	integrity_errors[2];
	u_int64_t	error_log_entries[2];
	u_int32_t	warn_temp_time;		/* 1.2+ */
	u_int32_t	crit_temp_time;		/* 1.2+ */
	u_int16_t	temp_sensors[8];	/* 1.2+ */
	u_int32_t	therm_mgmt_count_1;	/* 1.3+ */
	u_int32_t	therm_mgmt_count_2;	/* 1.3+ */
	u_int32_t	therm_mgmt_time_1;	/* 1.3+ */
	u_int32_t	therm_mgmt_time_2;	/* 1.3+ */
	
	u_int8_t	_reserved2[280];
} __packed __aligned(8);
