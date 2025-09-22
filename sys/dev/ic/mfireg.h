/* $OpenBSD: mfireg.h,v 1.52 2022/09/16 12:08:27 stsp Exp $ */
/*
 * Copyright (c) 2006 Marco Peereboom <marco@peereboom.us>
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

/* management interface constants */
#define MFI_MGMT_VD				0x01
#define MFI_MGMT_SD				0x02

/* generic constants */
#define MFI_FRAME_SIZE				64
#define MFI_SENSE_SIZE				128
#define MFI_OSTS_INTR_VALID			0x00000002 /* valid interrupt */
#define MFI_OSTS_PPC_INTR_VALID			0x80000000
#define MFI_OSTS_GEN2_INTR_VALID		(0x00000001 | 0x00000004)
#define MFI_INVALID_CTX				0xffffffff
#define MFI_ENABLE_INTR				0x01

/* register offsets */
#define MFI_IMSG0				0x10 /* inbound msg 0 */
#define MFI_IMSG1				0x14 /* inbound msg 1 */
#define MFI_OMSG0				0x18 /* outbound msg 0 */
#define MFI_OMSG1				0x1c /* outbound msg 1 */
#define MFI_IDB					0x20 /* inbound doorbell */
#define MFI_ISTS				0x24 /* inbound intr stat */
#define MFI_IMSK				0x28 /* inbound intr mask */
#define MFI_ODB					0x2c /* outbound doorbell */
#define MFI_OSTS				0x30 /* outbound intr stat */
#define MFI_OMSK				0x34 /* outbound inter mask */
#define MFI_IQP					0x40 /* inbound queue port */
#define MFI_OQP					0x44 /* outbound queue port */
#define MFI_ODC					0xa0 /* outbound doorbell clr */
#define MFI_OSP					0xb0 /* outbound scratch pad */

/*
 * skinny specific changes
*/
#define MFI_SKINNY_IDB				0x00 /* Inbound doorbell is at 0x00 for skinny */
#define MFI_IQPL				0x000000c0
#define MFI_IQPH				0x000000c4
#define MFI_OSTS_SKINNY_INTR_VALID		0x00000001

/* * firmware states */
#define MFI_STATE_MASK				0xf0000000
#define MFI_STATE_UNDEFINED			0x00000000
#define MFI_STATE_BB_INIT			0x10000000
#define MFI_STATE_FW_INIT			0x40000000
#define MFI_STATE_WAIT_HANDSHAKE		0x60000000
#define MFI_STATE_FW_INIT_2			0x70000000
#define MFI_STATE_DEVICE_SCAN			0x80000000
#define MFI_STATE_BOOT_MESSAGE_PENDING		0x90000000
#define MFI_STATE_FLUSH_CACHE			0xa0000000
#define MFI_STATE_READY				0xb0000000
#define MFI_STATE_OPERATIONAL			0xc0000000
#define MFI_STATE_FAULT				0xf0000000
#define MFI_STATE_MAXSGL_MASK			0x00ff0000
#define MFI_STATE_MAXCMD_MASK			0x0000ffff

/* command reset register */
#define MFI_INIT_ABORT				0x00000000
#define MFI_INIT_READY				0x00000002
#define MFI_INIT_MFIMODE			0x00000004
#define MFI_INIT_CLEAR_HANDSHAKE		0x00000008
#define MFI_INIT_HOTPLUG			0x00000010
#define MFI_RESET_FLAGS				MFI_INIT_READY|MFI_INIT_MFIMODE

/* mfi Frame flags */
#define MFI_FRAME_POST_IN_REPLY_QUEUE		0x0000
#define MFI_FRAME_DONT_POST_IN_REPLY_QUEUE	0x0001
#define MFI_FRAME_SGL32				0x0000
#define MFI_FRAME_SGL64				0x0002
#define MFI_FRAME_SENSE32			0x0000
#define MFI_FRAME_SENSE64			0x0004
#define MFI_FRAME_DIR_NONE			0x0000
#define MFI_FRAME_DIR_WRITE			0x0008
#define MFI_FRAME_DIR_READ			0x0010
#define MFI_FRAME_DIR_BOTH			0x0018
#define MFI_FRAME_IEEE				0x0020

/* mfi command opcodes */
#define MFI_CMD_INIT				0x00
#define MFI_CMD_LD_READ				0x01
#define MFI_CMD_LD_WRITE			0x02
#define MFI_CMD_LD_SCSI_IO			0x03
#define MFI_CMD_PD_SCSI_IO			0x04
#define MFI_CMD_DCMD				0x05
#define MFI_CMD_ABORT				0x06
#define MFI_CMD_SMP				0x07
#define MFI_CMD_STP				0x08

#define MFI_PR_STATE_STOPPED			0
#define MFI_PR_STATE_READY			1
#define MFI_PR_STATE_ACTIVE			2
#define MFI_PR_STATE_ABORTED			0xff

#define MFI_PR_OPMODE_AUTO			0x00
#define MFI_PR_OPMODE_MANUAL			0x01
#define MFI_PR_OPMODE_DISABLED			0x02

/* direct commands */
#define MR_DCMD_CTRL_GET_INFO			0x01010000
#define MR_DCMD_CTRL_CACHE_FLUSH		0x01101000
#define   MR_FLUSH_CTRL_CACHE			0x01
#define   MR_FLUSH_DISK_CACHE			0x02
#define MR_DCMD_CTRL_SHUTDOWN			0x01050000
#define   MR_ENABLE_DRIVE_SPINDOWN		0x01
#define MR_DCMD_CTRL_EVENT_GET_INFO		0x01040100
#define MR_DCMD_CTRL_EVENT_GET			0x01040300
#define MR_DCMD_CTRL_EVENT_WAIT			0x01040500
#define MR_DCMD_PR_GET_STATUS			0x01070100
#define MR_DCMD_PR_GET_PROPERTIES		0x01070200
#define MR_DCMD_PR_SET_PROPERTIES		0x01070300
#define MR_DCMD_PR_START			0x01070400
#define MR_DCMD_PR_STOP				0x01070500
#define MR_DCMD_TIME_SECS_GET			0x01080201
#define MR_DCMD_PD_GET_LIST			0x02010000
#define MR_DCMD_PD_GET_INFO			0x02020000
#define MR_DCMD_PD_SET_STATE			0x02030100
#define MR_DCMD_PD_REBUILD			0x02040100
#define MR_DCMD_PD_BLINK			0x02070100
#define MR_DCMD_PD_UNBLINK			0x02070200
#define MR_DCMD_PD_GET_ALLOWED_OPS_LIST		0x020a0100
#define MR_DCMD_LD_GET_LIST			0x03010000
#define MR_DCMD_LD_GET_INFO			0x03020000
#define MR_DCMD_LD_GET_PROPERTIES		0x03030000
#define MR_DCMD_LD_SET_PROPERTIES		0x03040000
#define MR_DCMD_LD_DELETE			0x03090000
#define MR_DCMD_CONF_GET			0x04010000
#define MR_DCMD_CFG_ADD				0x04020000
#define MR_DCMD_CFG_CLEAR			0x04030000
#define MR_DCMD_CFG_MAKE_SPARE			0x04040000
#define MR_DCMD_CFG_FOREIGN_SCAN		0x04060100
#define MR_DCMD_CFG_FOREIGN_CLEAR		0x04060500
#define MR_DCMD_BBU_GET_STATUS			0x05010000
#define MR_DCMD_BBU_GET_CAPACITY_INFO		0x05020000
#define MR_DCMD_BBU_GET_DESIGN_INFO		0x05030000
#define MR_DCMD_BBU_START_LEARN			0x05040000
#define MR_DCMD_BBU_GET_PROP			0x05050100
#define MR_DCMD_BBU_SET_PROP			0x05050200
#define MR_DCMD_CLUSTER				0x08000000
#define MR_DCMD_CLUSTER_RESET_ALL		0x08010100
#define MR_DCMD_CLUSTER_RESET_LD		0x08010200

#define MR_DCMD_SPEAKER_GET			0x01030100
#define MR_DCMD_SPEAKER_ENABLE			0x01030200
#define MR_DCMD_SPEAKER_DISABLE			0x01030300
#define MR_DCMD_SPEAKER_SILENCE			0x01030400
#define MR_DCMD_SPEAKER_TEST			0x01030500

#define MR_LD_CACHE_WRITE_BACK			0x01
#define MR_LD_CACHE_WRITE_ADAPTIVE		0x02
#define MR_LD_CACHE_READ_AHEAD	       		0x04
#define MR_LD_CACHE_READ_ADAPTIVE		0x08
#define MR_LD_CACHE_WRITE_CACHE_BAD_BBU		0x10
#define MR_LD_CACHE_ALLOW_WRITE_CACHE		0x20
#define MR_LD_CACHE_ALLOW_READ_CACHE		0x40

#define MR_LD_DISK_CACHE_ENABLE			0x01
#define MR_LD_DISK_CACHE_DISABLE		0x02

/* mailbox bytes in direct command */
#define MFI_MBOX_SIZE				12

union mfi_mbox {
	uint8_t			b[MFI_MBOX_SIZE];
	uint16_t		s[6];
	uint32_t		w[3];
} __packed __aligned(4);

/* mfi completion codes */
typedef enum {
	MFI_STAT_OK =				0x00,
	MFI_STAT_INVALID_CMD =			0x01,
	MFI_STAT_INVALID_DCMD =			0x02,
	MFI_STAT_INVALID_PARAMETER =		0x03,
	MFI_STAT_INVALID_SEQUENCE_NUMBER =	0x04,
	MFI_STAT_ABORT_NOT_POSSIBLE =		0x05,
	MFI_STAT_APP_HOST_CODE_NOT_FOUND =	0x06,
	MFI_STAT_APP_IN_USE =			0x07,
	MFI_STAT_APP_NOT_INITIALIZED =		0x08,
	MFI_STAT_ARRAY_INDEX_INVALID =		0x09,
	MFI_STAT_ARRAY_ROW_NOT_EMPTY =		0x0a,
	MFI_STAT_CONFIG_RESOURCE_CONFLICT =	0x0b,
	MFI_STAT_DEVICE_NOT_FOUND =		0x0c,
	MFI_STAT_DRIVE_TOO_SMALL =		0x0d,
	MFI_STAT_FLASH_ALLOC_FAIL =		0x0e,
	MFI_STAT_FLASH_BUSY =			0x0f,
	MFI_STAT_FLASH_ERROR =			0x10,
	MFI_STAT_FLASH_IMAGE_BAD =		0x11,
	MFI_STAT_FLASH_IMAGE_INCOMPLETE =	0x12,
	MFI_STAT_FLASH_NOT_OPEN =		0x13,
	MFI_STAT_FLASH_NOT_STARTED =		0x14,
	MFI_STAT_FLUSH_FAILED =			0x15,
	MFI_STAT_HOST_CODE_NOT_FOUNT =		0x16,
	MFI_STAT_LD_CC_IN_PROGRESS =		0x17,
	MFI_STAT_LD_INIT_IN_PROGRESS =		0x18,
	MFI_STAT_LD_LBA_OUT_OF_RANGE =		0x19,
	MFI_STAT_LD_MAX_CONFIGURED =		0x1a,
	MFI_STAT_LD_NOT_OPTIMAL =		0x1b,
	MFI_STAT_LD_RBLD_IN_PROGRESS =		0x1c,
	MFI_STAT_LD_RECON_IN_PROGRESS =		0x1d,
	MFI_STAT_LD_WRONG_RAID_LEVEL =		0x1e,
	MFI_STAT_MAX_SPARES_EXCEEDED =		0x1f,
	MFI_STAT_MEMORY_NOT_AVAILABLE =		0x20,
	MFI_STAT_MFC_HW_ERROR =			0x21,
	MFI_STAT_NO_HW_PRESENT =		0x22,
	MFI_STAT_NOT_FOUND =			0x23,
	MFI_STAT_NOT_IN_ENCL =			0x24,
	MFI_STAT_PD_CLEAR_IN_PROGRESS =		0x25,
	MFI_STAT_PD_TYPE_WRONG =		0x26,
	MFI_STAT_PR_DISABLED =			0x27,
	MFI_STAT_ROW_INDEX_INVALID =		0x28,
	MFI_STAT_SAS_CONFIG_INVALID_ACTION =	0x29,
	MFI_STAT_SAS_CONFIG_INVALID_DATA =	0x2a,
	MFI_STAT_SAS_CONFIG_INVALID_PAGE =	0x2b,
	MFI_STAT_SAS_CONFIG_INVALID_TYPE =	0x2c,
	MFI_STAT_SCSI_DONE_WITH_ERROR =		0x2d,
	MFI_STAT_SCSI_IO_FAILED =		0x2e,
	MFI_STAT_SCSI_RESERVATION_CONFLICT =	0x2f,
	MFI_STAT_SHUTDOWN_FAILED =		0x30,
	MFI_STAT_TIME_NOT_SET =			0x31,
	MFI_STAT_WRONG_STATE =			0x32,
	MFI_STAT_LD_OFFLINE =			0x33,
	MFI_STAT_PEER_NOTIFICATION_REJECTED =	0x34,
	MFI_STAT_PEER_NOTIFICATION_FAILED =	0x35,
	MFI_STAT_RESERVATION_IN_PROGRESS =	0x36,
	MFI_STAT_I2C_ERRORS_DETECTED =		0x37,
	MFI_STAT_PCI_ERRORS_DETECTED =		0x38,
	MFI_STAT_INVALID_STATUS =		0xff
} mfi_status_t;

typedef enum {
	MFI_EVT_CLASS_DEBUG =			-2,
	MFI_EVT_CLASS_PROGRESS =		-1,
	MFI_EVT_CLASS_INFO =			0,
	MFI_EVT_CLASS_WARNING =			1,
	MFI_EVT_CLASS_CRITICAL =		2,
	MFI_EVT_CLASS_FATAL =			3,
	MFI_EVT_CLASS_DEAD =			4
} mfi_evt_class_t;

typedef enum {
	MFI_EVT_LOCALE_LD =			0x0001,
	MFI_EVT_LOCALE_PD =			0x0002,
	MFI_EVT_LOCALE_ENCL =			0x0004,
	MFI_EVT_LOCALE_BBU =			0x0008,
	MFI_EVT_LOCALE_SAS =			0x0010,
	MFI_EVT_LOCALE_CTRL =			0x0020,
	MFI_EVT_LOCALE_CONFIG =			0x0040,
	MFI_EVT_LOCALE_CLUSTER =		0x0080,
	MFI_EVT_LOCALE_ALL =			0xffff
} mfi_evt_locale_t;

#define MFI_EVT_ARGS_NONE			0x00
#define MFI_EVT_ARGS_CDB_SENSE			0x01
#define MFI_EVT_ARGS_LD				0x02
#define MFI_EVT_ARGS_LD_COUNT			0x03
#define MFI_EVT_ARGS_LD_LBA			0x04
#define MFI_EVT_ARGS_LD_OWNER			0x05
#define MFI_EVT_ARGS_LD_LBA_PD_LBA		0x06
#define MFI_EVT_ARGS_LD_PROG			0x07
#define MFI_EVT_ARGS_LD_STATE			0x08
#define MFI_EVT_ARGS_LD_STRIP			0x09
#define MFI_EVT_ARGS_PD				0x0a
#define MFI_EVT_ARGS_PD_ERR			0x0b
#define MFI_EVT_ARGS_PD_LBA			0x0c
#define MFI_EVT_ARGS_PD_LBA_LD			0x0d
#define MFI_EVT_ARGS_PD_PROG			0x0e
#define MFI_EVT_ARGS_PD_STATE			0x0f
#define MFI_EVT_ARGS_PCI			0x10
#define MFI_EVT_ARGS_RATE			0x11
#define MFI_EVT_ARGS_STR			0x12
#define MFI_EVT_ARGS_TIME			0x13
#define MFI_EVT_ARGS_ECC			0x14
#define MFI_EVT_ARGS_LD_PROP			0x15
#define MFI_EVT_ARGS_PD_SPARE			0x16
#define MFI_EVT_ARGS_PD_INDEX			0x17
#define MFI_EVT_ARGS_DIAG_PASS			0x18
#define MFI_EVT_ARGS_DIAG_FAIL			0x19
#define MFI_EVT_ARGS_PD_LBA_LBA			0x1a
#define MFI_EVT_ARGS_PORT_PHY			0x1b
#define MFI_EVT_ARGS_PD_MISSING			0x1c
#define MFI_EVT_ARGS_PD_ADDRESS			0x1d
#define MFI_EVT_ARGS_BITMAP			0x1e
#define MFI_EVT_ARGS_CONNECTOR			0x1f
#define MFI_EVT_ARGS_PD_PD			0x20
#define MFI_EVT_ARGS_PD_FRU			0x21
#define MFI_EVT_ARGS_PD_PATHINFO		0x22
#define MFI_EVT_ARGS_PD_POWER_STATE		0x23
#define MFI_EVT_ARGS_GENERIC			0x24

#define MFI_EVT_CFG_CLEARED			0x0004
#define MFI_EVT_LD_STATE_CHANGE			0x0051
#define MFI_EVT_PD_INSERTED			0x005b
#define MFI_EVT_PD_REMOVED			0x0070
#define MFI_EVT_PD_STATE_CHANGE			0x0072
#define MFI_EVT_LD_CREATED			0x008a
#define MFI_EVT_LD_DELETED			0x008b
#define MFI_EVT_FOREIGN_CFG_IMPORTED		0x00db
#define MFI_EVT_PD_REMOVED_EXT			0x00f8
#define MFI_EVT_PD_INSERTED_EXT			0x00f7
#define MFI_EVT_LD_OFFLINE			0x00fc
#define MFI_EVT_CTRL_HOST_BUS_SCAN_REQUESTED	0x0152
#define MFI_EVT_CTRL_PROP_CHANGED		0x012f

/* driver definitions */
#define MFI_MAX_PD_CHANNELS			2
#define MFI_MAX_PD_ARRAY			32
#define MFI_MAX_LD_CHANNELS			2
#define MFI_MAX_CHANNELS	(MFI_MAX_PD_CHANNELS + MFI_MAX_LD_CHANNELS)
#define MFI_MAX_CHANNEL_DEVS			128
#define MFI_DEFAULT_ID				-1
#define MFI_MAX_LUN				8
#define MFI_MAX_LD				64
#define MFI_MAX_SPAN				8
#define MFI_MAX_ARRAY_DEDICATED			16
#define MFI_MAX_PD				256

/* sense buffer */
struct mfi_sense {
	uint8_t			mse_data[MFI_SENSE_SIZE];
} __packed;

/* scatter gather elements */
struct mfi_sg32 {
	uint32_t		addr;
	uint32_t		len;
} __packed;

struct mfi_sg64 {
	uint64_t		addr;
	uint32_t		len;
} __packed;

struct mfi_sg_skinny {
	uint64_t		addr;
	uint32_t		len;
	uint32_t		flag;
} __packed;

union mfi_sgl {
	struct mfi_sg32		sg32[1];
	struct mfi_sg64		sg64[1];
	struct mfi_sg_skinny	sg_skinny[1];
} __packed;

/* message frame */
struct mfi_frame_header {
	uint8_t			mfh_cmd;
	uint8_t			mfh_sense_len;
	uint8_t			mfh_cmd_status;
	uint8_t			mfh_scsi_status;
	uint8_t			mfh_target_id;
	uint8_t			mfh_lun_id;
	uint8_t			mfh_cdb_len;
	uint8_t			mfh_sg_count;
	uint32_t		mfh_context;
	uint32_t		mfh_pad0;
	uint16_t		mfh_flags;
	uint16_t		mfh_timeout;
	uint32_t		mfh_data_len;
} __packed;

union mfi_sgl_frame {
	struct mfi_sg32		sge32[8];
	struct mfi_sg64		sge64[5];

} __packed;

struct mfi_init_frame {
	struct mfi_frame_header	mif_header;
	uint64_t		mif_qinfo_new_addr;
	uint64_t		mif_qinfo_old_addr;
	uint32_t		mif_reserved[6];
} __packed;

/* queue init structure */
struct mfi_init_qinfo {
	uint32_t		miq_flags;
	uint32_t		miq_rq_entries;
	uint64_t		miq_rq_addr;
	uint64_t		miq_pi_addr;
	uint64_t		miq_ci_addr;
} __packed;

#define MFI_IO_FRAME_SIZE	40
struct mfi_io_frame {
	struct mfi_frame_header	mif_header;
	uint64_t		mif_sense_addr;
	uint64_t		mif_lba;
	union mfi_sgl		mif_sgl;
} __packed;

#define MFI_PASS_FRAME_SIZE	48
struct mfi_pass_frame {
	struct mfi_frame_header mpf_header;
	uint64_t		mpf_sense_addr;
	uint8_t			mpf_cdb[16];
	union mfi_sgl		mpf_sgl;
} __packed;

#define MFI_DCMD_FRAME_SIZE	40
struct mfi_dcmd_frame {
	struct mfi_frame_header mdf_header;
	uint32_t		mdf_opcode;
	union mfi_mbox		mdf_mbox;
	union mfi_sgl		mdf_sgl;
} __packed;

struct mfi_abort_frame {
	struct mfi_frame_header maf_header;
	uint32_t		maf_abort_context;
	uint32_t		maf_pad;
	uint64_t		maf_abort_mfi_addr;
	uint32_t		maf_reserved[6];
} __packed;

struct mfi_smp_frame {
	struct mfi_frame_header msf_header;
	uint64_t		msf_sas_addr;
	union {
		struct mfi_sg32 sg32[2];
		struct mfi_sg64 sg64[2];
	}			msf_sgl;
} __packed;

struct mfi_stp_frame {
	struct mfi_frame_header msf_header;
	uint16_t		msf_fis[10];
	uint32_t		msf_stp_flags;
	union {
		struct mfi_sg32 sg32[2];
		struct mfi_sg64 sg64[2];
	} 			msf_sgl;
} __packed;

union mfi_frame {
	struct mfi_frame_header mfr_header;
	struct mfi_init_frame	mfr_init;
	struct mfi_io_frame	mfr_io;
	struct mfi_pass_frame	mfr_pass;
	struct mfi_dcmd_frame	mfr_dcmd;
	struct mfi_abort_frame	mfr_abort;
	struct mfi_smp_frame	mfr_smp;
	struct mfi_stp_frame	mfr_stp;
	uint8_t			mfr_bytes[MFI_FRAME_SIZE];
};

union mfi_evt_class_locale {
	struct {
		uint16_t	locale;
		uint8_t		reserved;
		int8_t		class;
	} __packed		mec_members;

	uint32_t		mec_word;
} __packed;

struct mfi_evt_log_info {
	uint32_t		mel_newest_seq_num;
	uint32_t		mel_oldest_seq_num;
	uint32_t		mel_clear_seq_num;
	uint32_t		mel_shutdown_seq_num;
	uint32_t		mel_boot_seq_num;
} __packed;

struct mfi_progress {
	uint16_t		mp_progress;
	uint16_t		mp_elapsed_seconds;
} __packed;

struct mfi_evtarg_ld {
	uint16_t		mel_target_id;
	uint8_t			mel_ld_index;
	uint8_t			mel_reserved;
} __packed;

struct mfi_evtarg_pd {
	uint16_t		mep_device_id;
	uint8_t			mep_encl_index;
	uint8_t			mep_slot_number;
} __packed;

struct mfi_evtarg_pd_state {
	struct mfi_evtarg_pd	pd;
	uint32_t		prev_state;
	uint32_t		new_state;
} __packed;

struct mfi_evtarg_pd_address {
	uint16_t		device_id;
	uint16_t		encl_id;

	union {
		struct {
			uint8_t			encl_index;
			uint8_t			slot_number;
		} __packed		pd_address;
		struct {
			uint8_t			encl_position;
			uint8_t			encl_connector_index;
		} __packed		encl_address;
	} __packed		address;

        uint8_t			scsi_dev_type;

	union {
		uint8_t			port_bitmap;
		uint8_t			port_numbers;
	} __packed		connected;

	uint64_t		sas_addr[2];
} __packed __aligned(8);

struct mfi_evt_detail {
	uint32_t				med_seq_num;
	uint32_t				med_time_stamp;
	uint32_t				med_code;
	union mfi_evt_class_locale		med_cl;
	uint8_t					med_arg_type;
	uint8_t					med_reserved1[15];

	union {
		struct {
			struct mfi_evtarg_pd	pd;
			uint8_t			cdb_length;
			uint8_t			sense_length;
			uint8_t			reserved[2];
			uint8_t			cdb[16];
			uint8_t			sense[64];
		} __packed			cdb_sense;

		struct mfi_evtarg_ld		ld;

		struct {
			struct mfi_evtarg_ld	ld;
			uint64_t		count;
		} __packed			ld_count;

		struct {
			uint64_t		lba;
			struct mfi_evtarg_ld	ld;
		} __packed			ld_lba;

		struct {
			struct mfi_evtarg_ld	ld;
			uint32_t		prev_owner;
			uint32_t		new_owner;
		} __packed			ld_owner;

		struct {
			uint64_t		ld_lba;
			uint64_t		pd_lba;
			struct mfi_evtarg_ld	ld;
			struct mfi_evtarg_pd	pd;
		} __packed			ld_lba_pd_lba;

		struct {
			struct mfi_evtarg_ld	ld;
			struct mfi_progress	prog;
		} __packed			ld_prog;

		struct {
			struct mfi_evtarg_ld	ld;
			uint32_t		prev_state;
			uint32_t		new_state;
		} __packed			ld_state;

		struct {
			uint64_t		strip;
			struct mfi_evtarg_ld	ld;
		} __packed			ld_strip;

		struct mfi_evtarg_pd		pd;

		struct {
			struct mfi_evtarg_pd	pd;
			uint32_t		err;
		} __packed			pd_err;

		struct {
			uint64_t		lba;
			struct mfi_evtarg_pd	pd;
		} __packed			pd_lba;

		struct {
			uint64_t		lba;
			struct mfi_evtarg_pd	pd;
			struct mfi_evtarg_ld	ld;
		} __packed			pd_lba_ld;

		struct {
			struct mfi_evtarg_pd	pd;
			struct mfi_progress	prog;
		} __packed			pd_prog;

		struct mfi_evtarg_pd_state	pd_state;

		struct {
			uint16_t		vendor_id;
			uint16_t		device_id;
			uint16_t		subvendor_id;
			uint16_t		subdevice_id;
		} __packed			pci;

		uint32_t			rate;
		char				str[96];

		struct {
			uint32_t		rtc;
			uint32_t		elapsed_seconds;
		} __packed			time;

		struct {
			uint32_t		ecar;
			uint32_t		elog;
			char			str[64];
		} __packed			ecc;

		struct mfi_evtarg_pd_address	pd_address;

		uint8_t				b[96];
		uint16_t			s[48];
		uint32_t			w[24];
		uint64_t			d[12];
	}					args;

	char					med_description[128];
} __packed;

/* controller properties from mfi_ctrl_info */
struct mfi_ctrl_props {
	uint16_t		mcp_seq_num;
	uint16_t		mcp_pred_fail_poll_interval;
	uint16_t		mcp_intr_throttle_cnt;
	uint16_t		mcp_intr_throttle_timeout;
	uint8_t			mcp_rebuild_rate;
	uint8_t			mcp_patrol_read_rate;
	uint8_t			mcp_bgi_rate;
	uint8_t			mcp_cc_rate;
	uint8_t			mcp_recon_rate;
	uint8_t			mcp_cache_flush_interval;
	uint8_t			mcp_spinup_drv_cnt;
	uint8_t			mcp_spinup_delay;
	uint8_t			mcp_cluster_enable;
	uint8_t			mcp_coercion_mode;
	uint8_t			mcp_alarm_enable;
	uint8_t			mcp_disable_auto_rebuild;
	uint8_t			mcp_disable_battery_warn;
	uint8_t			mcp_ecc_bucket_size;
	uint16_t		mcp_ecc_bucket_leak_rate;
	uint8_t			mcp_restore_hotspare_on_insertion;
	uint8_t			mcp_expose_encl_devices;
	uint8_t			mcp_reserved[38];
} __packed;

/* pci info */
struct mfi_info_pci {
	uint16_t		mip_vendor;
	uint16_t		mip_device;
	uint16_t		mip_subvendor;
	uint16_t		mip_subdevice;
	uint8_t			mip_reserved[24];
} __packed;

/* host interface info */
struct mfi_info_host {
	uint8_t			mih_type;
#define MFI_INFO_HOST_PCIX	0x01
#define MFI_INFO_HOST_PCIE	0x02
#define MFI_INFO_HOST_ISCSI	0x04
#define MFI_INFO_HOST_SAS3G	0x08
	uint8_t			mih_reserved[6];
	uint8_t			mih_port_count;
	uint64_t		mih_port_addr[8];
} __packed;

/* device interface info */
struct mfi_info_device {
	uint8_t			mid_type;
#define MFI_INFO_DEV_SPI	0x01
#define MFI_INFO_DEV_SAS3G	0x02
#define MFI_INFO_DEV_SATA1	0x04
#define MFI_INFO_DEV_SATA3G	0x08
	uint8_t			mid_reserved[6];
	uint8_t			mid_port_count;
	uint64_t		mid_port_addr[8];
} __packed;

/* firmware component info */
struct mfi_info_component {
	char			mic_name[8];
	char			mic_version[32];
	char			mic_build_date[16];
	char			mic_build_time[16];
} __packed;

/* controller info from MFI_DCMD_CTRL_GETINFO. */
struct mfi_ctrl_info {
	struct mfi_info_pci	mci_pci;
	struct mfi_info_host	mci_host;
	struct mfi_info_device	mci_device;

	/* Firmware components that are present and active. */
	uint32_t		mci_image_check_word;
	uint32_t		mci_image_component_count;
	struct mfi_info_component mci_image_component[8];

	/* Firmware components that have been flashed but are inactive */
	uint32_t		mci_pending_image_component_count;
	struct mfi_info_component mci_pending_image_component[8];

	uint8_t			mci_max_arms;
	uint8_t			mci_max_spans;
	uint8_t			mci_max_arrays;
	uint8_t			mci_max_lds;
	char			mci_product_name[80];
	char			mci_serial_number[32];
	uint32_t		mci_hw_present;
#define MFI_INFO_HW_BBU			0x01
#define MFI_INFO_HW_ALARM		0x02
#define MFI_INFO_HW_NVRAM		0x04
#define MFI_INFO_HW_UART		0x08
#define MFI_INFO_HW_FMT		"\020" "\001BBU" "\002ALARM" "\003NVRAM" \
				    "\004UART"

	uint32_t		mci_current_fw_time;
	uint16_t		mci_max_cmds;
	uint16_t		mci_max_sg_elements;
	uint32_t		mci_max_request_size;
	uint16_t		mci_lds_present;
	uint16_t		mci_lds_degraded;
	uint16_t		mci_lds_offline;
	uint16_t		mci_pd_present;
	uint16_t		mci_pd_disks_present;
	uint16_t		mci_pd_disks_pred_failure;
	uint16_t		mci_pd_disks_failed;
	uint16_t		mci_nvram_size;
	uint16_t		mci_memory_size;
	uint16_t		mci_flash_size;
	uint16_t		mci_ram_correctable_errors;
	uint16_t		mci_ram_uncorrectable_errors;
	uint8_t			mci_cluster_allowed;
	uint8_t			mci_cluster_active;
	uint16_t		mci_max_strips_per_io;

	uint32_t		mci_raid_levels;
#define MFI_INFO_RAID_0		0x01
#define MFI_INFO_RAID_1		0x02
#define MFI_INFO_RAID_5		0x04
#define MFI_INFO_RAID_1E	0x08
#define MFI_INFO_RAID_6		0x10

	uint32_t		mci_adapter_ops;
#define MFI_INFO_AOPS_RBLD_RATE		0x0001
#define MFI_INFO_AOPS_CC_RATE		0x0002
#define MFI_INFO_AOPS_BGI_RATE		0x0004
#define MFI_INFO_AOPS_RECON_RATE	0x0008
#define MFI_INFO_AOPS_PATROL_RATE	0x0010
#define MFI_INFO_AOPS_ALARM_CONTROL	0x0020
#define MFI_INFO_AOPS_CLUSTER_SUPPORTED	0x0040
#define MFI_INFO_AOPS_BBU		0x0080
#define MFI_INFO_AOPS_SPANNING_ALLOWED	0x0100
#define MFI_INFO_AOPS_DEDICATED_SPARES	0x0200
#define MFI_INFO_AOPS_REVERTIBLE_SPARES	0x0400
#define MFI_INFO_AOPS_FOREIGN_IMPORT	0x0800
#define MFI_INFO_AOPS_SELF_DIAGNOSTIC	0x1000
#define MFI_INFO_AOPS_MIXED_ARRAY	0x2000
#define MFI_INFO_AOPS_GLOBAL_SPARES	0x4000
#define MFI_INFO_AOPS_FMT	"\020" "\001RBLD_RATE" "\002CC_RATE" \
				    "\003BGI_RATE" "\004RECON_RATE" \
				    "\005PATROL_RATE" "\006ALARM_CONTROL" \
				    "\007CLUSTER_SUPPORT" "\010BBU" \
				    "\011SPANNING_ALLOWED" \
				    "\012DEDICATED_SPARES" \
				    "\013REVERTIBLE_SPARES" \
				    "\014FOREIGN_IMPORT" "\015SELF_DIAGNOSTIC" \
				    "\016MIXED_ARRAY" "\017GLOBAL_SPARES"

	uint32_t		mci_ld_ops;
#define MFI_INFO_LDOPS_READ_POLICY	0x01
#define MFI_INFO_LDOPS_WRITE_POLICY	0x02
#define MFI_INFO_LDOPS_IO_POLICY	0x04
#define MFI_INFO_LDOPS_ACCESS_POLICY	0x08
#define MFI_INFO_LDOPS_DISK_CACHE_POLICY 0x10

	struct {
		uint8_t		min;
		uint8_t		max;
		uint8_t		reserved[2];
	} __packed		mci_stripe_sz_ops;

	uint32_t		mci_pd_ops;
#define MFI_INFO_PDOPS_FORCE_ONLINE	0x01
#define MFI_INFO_PDOPS_FORCE_OFFLINE	0x02
#define MFI_INFO_PDOPS_FORCE_REBUILD	0x04

	uint32_t		mci_pd_mix_support;
#define MFI_INFO_PDMIX_SAS		0x01
#define MFI_INFO_PDMIX_SATA		0x02
#define MFI_INFO_PDMIX_ENCL		0x04
#define MFI_INFO_PDMIX_LD		0x08
#define MFI_INFO_PDMIX_SATA_CLUSTER	0x10

	uint8_t			mci_ecc_bucket_count;
	uint8_t			mci_reserved2[11];
	struct mfi_ctrl_props	mci_properties;
	char			mci_package_version[0x60];
	uint8_t			mci_pad[0x800 - 0x6a0];
} __packed;

/* logical disk info from MR_DCMD_LD_GET_LIST */
struct mfi_ld {
	uint8_t			mld_target;
	uint8_t			mld_res;
	uint16_t		mld_seq;
} __packed;

struct mfi_ld_list {
	uint32_t		mll_no_ld;
	uint32_t		mll_res;
	struct {
		struct mfi_ld	mll_ld;
		uint8_t		mll_state;
#define MFI_LD_OFFLINE			0x00
#define MFI_LD_PART_DEGRADED		0x01
#define MFI_LD_DEGRADED			0x02
#define MFI_LD_ONLINE			0x03
		uint8_t		mll_res2;
		uint8_t		mll_res3;
		uint8_t		mll_res4;
		uint64_t	mll_size;
	} mll_list[MFI_MAX_LD];
} __packed;

/* logicl disk details from MR_DCMD_LD_GET_INFO */
struct mfi_ld_prop {
	struct mfi_ld		mlp_ld;
	char			mlp_name[16];
	uint8_t			mlp_cache_policy;
	uint8_t			mlp_acces_policy;
	uint8_t			mlp_diskcache_policy;
	uint8_t			mlp_cur_cache_policy;
	uint8_t			mlp_disable_bgi;
	uint8_t			mlp_res[7];
} __packed;

struct mfi_ld_parm {
	uint8_t			mpa_pri_raid;	/* SNIA DDF PRL */
#define MFI_DDF_PRL_RAID0	0x00
#define MFI_DDF_PRL_RAID1	0x01
#define MFI_DDF_PRL_RAID3	0x03
#define MFI_DDF_PRL_RAID4	0x04
#define MFI_DDF_PRL_RAID5	0x05
#define MFI_DDF_PRL_RAID1E	0x11
#define MFI_DDF_PRL_JBOD	0x0f
#define MFI_DDF_PRL_CONCAT	0x1f
#define MFI_DDF_PRL_RAID5E	0x15
#define MFI_DDF_PRL_RAID5EE	0x25
#define MFI_DDF_PRL_RAID6	0x16
	uint8_t			mpa_raid_qual;	/* SNIA DDF RLQ */
	uint8_t			mpa_sec_raid;	/* SNIA DDF SRL */
#define MFI_DDF_SRL_STRIPED	0x00
#define MFI_DDF_SRL_MIRRORED	0x01
#define MFI_DDF_SRL_CONCAT	0x02
#define MFI_DDF_SRL_SPANNED	0x03
	uint8_t			mpa_stripe_size;
	uint8_t			mpa_no_drv_per_span;
	uint8_t			mpa_span_depth;
	uint8_t			mpa_state;
	uint8_t			mpa_init_state;
	uint8_t			mpa_res[24];
} __packed;

struct mfi_ld_span {
	uint64_t		mls_start_block;
	uint64_t		mls_no_blocks;
	uint16_t		mls_index;
	uint8_t			mls_res[6];
} __packed;

struct mfi_ld_cfg {
	struct mfi_ld_prop	mlc_prop;
	struct mfi_ld_parm	mlc_parm;
	struct mfi_ld_span	mlc_span[MFI_MAX_SPAN];
} __packed;

struct mfi_ld_progress {
	uint32_t		mlp_in_prog;
#define MFI_LD_PROG_CC		0x01
#define MFI_LD_PROG_BGI		0x02
#define MFI_LD_PROG_FGI		0x04
#define MFI_LD_PROG_RECONSTRUCT	0x08
	struct mfi_progress	mlp_cc;
	struct mfi_progress	mlp_bgi;
	struct mfi_progress	mlp_fgi;
	struct mfi_progress	mlp_reconstruct;
	struct mfi_progress	mlp_res[4];
} __packed;

struct mfi_ld_details {
	struct mfi_ld_cfg	mld_cfg;
	uint64_t		mld_size;
	struct mfi_ld_progress	mld_progress;
	uint16_t		mld_clust_own_id;
	uint8_t			mld_res1;
	uint8_t			mld_res2;
	uint8_t			mld_inq_page83[64];
	uint8_t			mld_res[16];
} __packed;

/* physical disk info from MR_DCMD_PD_GET_LIST */
struct mfi_pd_address {
	uint16_t		mpa_pd_id;
	uint16_t		mpa_enc_id;
	uint8_t			mpa_enc_index;
	uint8_t			mpa_enc_slot;
	uint8_t			mpa_scsi_type;
	uint8_t			mpa_port;
	uint64_t		mpa_sas_address[2];
} __packed;

struct mfi_pd_list {
	uint32_t		mpl_size;
	uint32_t		mpl_no_pd;
	struct mfi_pd_address	mpl_address[MFI_MAX_PD];
} __packed;

struct mfi_pd {
	uint16_t		mfp_id;
	uint16_t		mfp_seq;
} __packed;

struct mfi_pd_progress {
	uint32_t		mfp_in_prog;
#define MFI_PD_PROG_RBLD	0x01
#define MFI_PD_PROG_PR		0x02
#define MFI_PD_PROG_CLEAR	0x04
	struct mfi_progress	mfp_rebuild;
	struct mfi_progress	mfp_patrol_read;
	struct mfi_progress	mfp_clear;
	struct mfi_progress	mfp_res[4];
} __packed;

struct mfi_pd_details {
	struct mfi_pd		mpd_pd;
	uint8_t			mpd_inq_data[96];
	uint8_t			mpd_inq_page83[64];
	uint8_t			mpd_no_support;
	uint8_t			mpd_scsi_type;
	uint8_t			mpd_port;
	uint8_t			mpd_speed;
	uint32_t		mpd_mediaerr_cnt;
	uint32_t		mpd_othererr_cnt;
	uint32_t		mpd_predfail_cnt;
	uint32_t		mpd_last_pred_event;
	uint16_t		mpd_fw_state;
	uint8_t			mpd_rdy_for_remove;
	uint8_t			mpd_link_speed;
	uint32_t		mpd_ddf_state;
#define MFI_DDF_GUID_FORCED	0x01
#define MFI_DDF_PART_OF_VD	0x02
#define MFI_DDF_GLOB_HOTSPARE	0x04
#define MFI_DDF_HOTSPARE	0x08
#define MFI_DDF_FOREIGN		0x10
#define MFI_DDF_TYPE_MASK	0xf000
#define MFI_DDF_TYPE_UNKNOWN	0x0000
#define MFI_DDF_TYPE_PAR_SCSI	0x1000
#define MFI_DDF_TYPE_SAS	0x2000
#define MFI_DDF_TYPE_SATA	0x3000
#define MFI_DDF_TYPE_FC		0x4000
	struct {
		uint8_t		mpp_cnt;
		uint8_t		mpp_severed;
		uint8_t		mpp_connector_idx[2];
		uint8_t		mpp_res[4];
		uint64_t	mpp_sas_addr[2];
		uint8_t		mpp_res2[16];
	} __packed mpd_path;
	uint64_t		mpd_size;
	uint64_t		mpd_no_coerce_size;
	uint64_t		mpd_coerce_size;
	uint16_t		mpd_enc_id;
	uint8_t			mpd_enc_idx;
	uint8_t			mpd_enc_slot;
	struct mfi_pd_progress	mpd_progress;
	uint8_t			mpd_bblock_full;
	uint8_t			mpd_unusable;
	uint8_t			mpd_inq_page83_ext[64];
	uint8_t			mpd_power_state; /* XXX */
	uint8_t			mpd_enc_pos;
	uint32_t		mpd_allowed_ops;
#define MFI_PD_A_ONLINE			(1<<0)
#define MFI_PD_A_OFFLINE		(1<<1)
#define MFI_PD_A_FAILED			(1<<2)
#define MFI_PD_A_BAD			(1<<3)
#define MFI_PD_A_UNCONFIG		(1<<4)
#define MFI_PD_A_HOTSPARE		(1<<5)
#define MFI_PD_A_REMOVEHOTSPARE		(1<<6)
#define MFI_PD_A_REPLACEMISSING		(1<<7)
#define MFI_PD_A_MARKMISSING		(1<<8)
#define MFI_PD_A_STARTREBUILD		(1<<9)
#define MFI_PD_A_STOPREBUILD		(1<<10)
#define MFI_PD_A_BLINK			(1<<11)
#define MFI_PD_A_CLEAR			(1<<12)
#define MFI_PD_A_FOREIGNIMPORNOTALLOWED	(1<<13)
#define MFI_PD_A_STARTCOPYBACK		(1<<14)
#define MFI_PD_A_STOPCOPYBACK		(1<<15)
#define MFI_PD_A_FWDOWNLOADDNOTALLOWED	(1<<16)
#define MFI_PD_A_REPROVISION		(1<<17)
	uint16_t		mpd_copyback_partner_id;
	uint16_t		mpd_enc_partner_devid;
	uint16_t		mpd_security;
#define MFI_PD_FDE_CAPABLE		(1<<0)
#define MFI_PD_FDE_ENABLED		(1<<1)
#define MFI_PD_FDE_SECURED		(1<<2)
#define MFI_PD_FDE_LOCKED		(1<<3)
#define MFI_PD_FDE_FOREIGNLOCK		(1<<4)
	uint8_t			mpd_media;
	uint8_t			mpd_res[141]; /* size is 512 */
} __packed;

struct mfi_pd_allowedops_list {
	uint32_t		mpo_no_entries;
	uint32_t		mpo_res;
	uint32_t		mpo_allowedops_list[MFI_MAX_PD];
} __packed;

/* array configuration from MR_DCMD_CONF_GET */
struct mfi_array {
	uint64_t		mar_smallest_pd;
	uint8_t			mar_no_disk;
	uint8_t			mar_res1;
	uint16_t		mar_array_ref;
	uint8_t			mar_res2[20];
	struct {
		struct mfi_pd	mar_pd;
		uint16_t	mar_pd_state;
#define MFI_PD_UNCONFIG_GOOD	0x00
#define MFI_PD_UNCONFIG_BAD	0x01
#define MFI_PD_HOTSPARE		0x02
#define MFI_PD_OFFLINE		0x10
#define MFI_PD_FAILED		0x11
#define MFI_PD_REBUILD		0x14
#define MFI_PD_ONLINE		0x18
#define MFI_PD_COPYBACK		0x20
#define MFI_PD_SYSTEM		0x40
		uint8_t		mar_enc_pd;
		uint8_t		mar_enc_slot;
	} pd[MFI_MAX_PD_ARRAY];
} __packed;

struct mfi_hotspare {
	struct mfi_pd	mhs_pd;
	uint8_t		mhs_type;
#define MFI_PD_HS_DEDICATED	0x01
#define MFI_PD_HS_REVERTIBLE	0x02
#define MFI_PD_HS_ENC_AFFINITY	0x04
	uint8_t		mhs_res[2];
	uint8_t		mhs_array_max;
	uint16_t	mhs_array_ref[MFI_MAX_ARRAY_DEDICATED];
} __packed;

struct mfi_conf {
	uint32_t		mfc_size;
	uint16_t		mfc_no_array;
	uint16_t		mfc_array_size;
	uint16_t		mfc_no_ld;
	uint16_t		mfc_ld_size;
	uint16_t		mfc_no_hs;
	uint16_t		mfc_hs_size;
	uint8_t			mfc_res[16];
	/*
	 * XXX this is a ridiculous hack and does not reflect reality
	 * Structures are actually indexed and therefore need pointer
	 * math to reach.  We need the size of this structure first so
	 * call it with the size of this structure and then use the returned
	 * values to allocate memory and do the transfer of the whole structure
	 * then calculate pointers to each of these structures.
	 */
	struct mfi_array	mfc_array[1];
	struct mfi_ld_cfg	mfc_ld[1];
	struct mfi_hotspare	mfc_hs[1];
} __packed;

struct mfi_bbu_capacity_info {
	uint16_t		relative_charge;
	uint16_t		absolute_charge;
	uint16_t		remaining_capacity;
	uint16_t		full_charge_capacity;
	uint16_t		run_time_to_empty;
	uint16_t		average_time_to_empty;
	uint16_t		average_time_to_full;
	uint16_t		cycle_count;
	uint16_t		max_error;
	uint16_t		remaining_capacity_alarm;
	uint16_t		remaining_time_alarm;
	uint8_t			reserved[26];
} __packed;

struct mfi_bbu_design_info {
	uint32_t		mfg_date;
	uint16_t		design_capacity;
	uint16_t		design_voltage;
	uint16_t		spec_info;
	uint16_t		serial_number;
	uint16_t		pack_stat_config;
	uint8_t			mfg_name[12];
	uint8_t			device_name[8];
	uint8_t			device_chemistry[8];
	uint8_t			mfg_data[8];
	uint8_t			reserved[17];
} __packed;

struct mfi_ibbu_state {
	uint16_t		gas_guage_status;
	uint16_t		relative_charge;
	uint16_t		charger_system_state;
	uint16_t		charger_system_ctrl;
	uint16_t		charging_current;
	uint16_t		absolute_charge;
	uint16_t		max_error;
	uint8_t			reserved[18];
} __packed;

struct mfi_bbu_state {
	uint16_t		gas_guage_status;
	uint16_t		relative_charge;
	uint16_t		charger_status;
	uint16_t		remaining_capacity;
	uint16_t		full_charge_capacity;
	uint8_t			is_SOH_good;
	uint8_t			reserved[21];
} __packed;

struct mfi_bbu_properties {
	uint32_t		auto_learn_period;
	uint32_t		next_learn_time;
	uint8_t			learn_delay_interval;
	uint8_t			auto_learn_mode;
	uint8_t			bbu_mode;
	uint8_t			reserved[21];
} __packed;

union mfi_bbu_status_detail {
	struct mfi_ibbu_state	ibbu;
	struct mfi_bbu_state	bbu;
};

struct mfi_bbu_status {
	uint8_t			battery_type;
#define MFI_BBU_TYPE_NONE		0
#define MFI_BBU_TYPE_IBBU		1
#define MFI_BBU_TYPE_BBU		2
	uint8_t			reserved;
	uint16_t		voltage; /* mV */
	int16_t			current; /* mA */
	uint16_t		temperature; /* degC */
	uint32_t		fw_status;
#define MFI_BBU_STATE_PACK_MISSING	(1 << 0)
#define MFI_BBU_STATE_VOLTAGE_LOW	(1 << 1)
#define MFI_BBU_STATE_TEMPERATURE_HIGH	(1 << 2)
#define MFI_BBU_STATE_CHARGE_ACTIVE	(1 << 3)
#define MFI_BBU_STATE_DISCHARGE_ACTIVE	(1 << 4)
#define MFI_BBU_STATE_LEARN_CYC_REQ	(1 << 5)
#define MFI_BBU_STATE_LEARN_CYC_ACTIVE	(1 << 6)
#define MFI_BBU_STATE_LEARN_CYC_FAIL	(1 << 7)
#define MFI_BBU_STATE_LEARN_CYC_TIMEOUT	(1 << 8)
#define MFI_BBU_STATE_I2C_ERR_DETECT	(1 << 9)
#define MFI_BBU_STATE_REPLACE_PACK	(1 << 10)
#define MFI_BBU_STATE_CAPACITY_LOW	(1 << 11)
#define MFI_BBU_STATE_LEARN_REQUIRED	(1 << 12)
#define MFI_BBU_STATE_FMT	"\020" \
				    "\001PACK_MISSING" \
				    "\002VOLTAGE_LOW" \
				    "\003TEMP_HIGH" \
				    "\004CHARGE_ACTIVE" \
				    "\005DISCHARGE_ACTIVE" \
				    "\006LEARN_CYC_REQ" \
				    "\007LEARN_CYC_ACTIVE" \
				    "\010LEARN_CYC_FAIL" \
				    "\011LEARN_CYC_TIMEOUT" \
				    "\012I2C_ERR_DETECT" \
				    "\013REPLACE_PACK" \
				    "\014CAPACITY_LOW" \
				    "\015LEARN_REQUIRED"
#define MFI_BBU_STATE_BAD_IBBU	( \
				    MFI_BBU_STATE_PACK_MISSING | \
				    MFI_BBU_STATE_VOLTAGE_LOW | \
				    MFI_BBU_STATE_DISCHARGE_ACTIVE | \
				    MFI_BBU_STATE_LEARN_CYC_REQ | \
				    MFI_BBU_STATE_LEARN_CYC_ACTIVE | \
				    MFI_BBU_STATE_REPLACE_PACK | \
				    MFI_BBU_STATE_CAPACITY_LOW)
#define MFI_BBU_STATE_BAD_BBU	( \
				    MFI_BBU_STATE_PACK_MISSING | \
				    MFI_BBU_STATE_REPLACE_PACK | \
				    MFI_BBU_STATE_CAPACITY_LOW)

	uint8_t			pad[20];
	union mfi_bbu_status_detail detail;
} __packed;

struct mfi_pr_status {
	uint32_t		num_iteration;
	uint8_t			state;
	uint8_t			num_pd_done;
	uint8_t			reserved[10];
} __packed;

struct mfi_pr_properties {
	uint8_t			op_mode;
	uint8_t			max_pd;
	uint8_t			reserved;
	uint8_t			exclude_ld_count;
	uint16_t		excluded_ld[MFI_MAX_LD];
	uint8_t			cur_pd_map[MFI_MAX_PD / 8];
	uint8_t			last_pd_map[MFI_MAX_PD / 8];
	uint32_t		next_exec;
	uint32_t		exec_freq;
	uint32_t		clear_freq;
} __packed;

/* We currently don't know the full details of the following struct */
struct mfii_foreign_scan_cfg {
	char data[24];
};

struct mfii_foreign_scan_info {
	uint32_t count; /* Number of foreign configs found */
	struct mfii_foreign_scan_cfg cfgs[8];
};
