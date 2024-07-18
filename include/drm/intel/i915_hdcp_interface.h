/* SPDX-License-Identifier: (GPL-2.0+) */
/*
 * Copyright © 2017-2019 Intel Corporation
 *
 * Authors:
 * Ramalingam C <ramalingam.c@intel.com>
 */

#ifndef _I915_HDCP_INTERFACE_H_
#define _I915_HDCP_INTERFACE_H_

#include <linux/mutex.h>
#include <linux/device.h>
#include <drm/display/drm_hdcp.h>

/**
 * enum hdcp_port_type - HDCP port implementation type defined by ME/GSC FW
 * @HDCP_PORT_TYPE_INVALID: Invalid hdcp port type
 * @HDCP_PORT_TYPE_INTEGRATED: In-Host HDCP2.x port
 * @HDCP_PORT_TYPE_LSPCON: HDCP2.2 discrete wired Tx port with LSPCON
 *			   (HDMI 2.0) solution
 * @HDCP_PORT_TYPE_CPDP: HDCP2.2 discrete wired Tx port using the CPDP (DP 1.3)
 *			 solution
 */
enum hdcp_port_type {
	HDCP_PORT_TYPE_INVALID,
	HDCP_PORT_TYPE_INTEGRATED,
	HDCP_PORT_TYPE_LSPCON,
	HDCP_PORT_TYPE_CPDP
};

/**
 * enum hdcp_wired_protocol - HDCP adaptation used on the port
 * @HDCP_PROTOCOL_INVALID: Invalid HDCP adaptation protocol
 * @HDCP_PROTOCOL_HDMI: HDMI adaptation of HDCP used on the port
 * @HDCP_PROTOCOL_DP: DP adaptation of HDCP used on the port
 */
enum hdcp_wired_protocol {
	HDCP_PROTOCOL_INVALID,
	HDCP_PROTOCOL_HDMI,
	HDCP_PROTOCOL_DP
};

enum hdcp_ddi {
	HDCP_DDI_INVALID_PORT = 0x0,

	HDCP_DDI_B = 1,
	HDCP_DDI_C,
	HDCP_DDI_D,
	HDCP_DDI_E,
	HDCP_DDI_F,
	HDCP_DDI_A = 7,
	HDCP_DDI_RANGE_END = HDCP_DDI_A,
};

/**
 * enum hdcp_transcoder - ME/GSC Firmware defined index for transcoders
 * @HDCP_INVALID_TRANSCODER: Index for Invalid transcoder
 * @HDCP_TRANSCODER_EDP: Index for EDP Transcoder
 * @HDCP_TRANSCODER_DSI0: Index for DSI0 Transcoder
 * @HDCP_TRANSCODER_DSI1: Index for DSI1 Transcoder
 * @HDCP_TRANSCODER_A: Index for Transcoder A
 * @HDCP_TRANSCODER_B: Index for Transcoder B
 * @HDCP_TRANSCODER_C: Index for Transcoder C
 * @HDCP_TRANSCODER_D: Index for Transcoder D
 */
enum hdcp_transcoder {
	HDCP_INVALID_TRANSCODER = 0x00,
	HDCP_TRANSCODER_EDP,
	HDCP_TRANSCODER_DSI0,
	HDCP_TRANSCODER_DSI1,
	HDCP_TRANSCODER_A = 0x10,
	HDCP_TRANSCODER_B,
	HDCP_TRANSCODER_C,
	HDCP_TRANSCODER_D
};

/**
 * struct hdcp_port_data - intel specific HDCP port data
 * @hdcp_ddi: ddi index as per ME/GSC FW
 * @hdcp_transcoder: transcoder index as per ME/GSC FW
 * @port_type: HDCP port type as per ME/GSC FW classification
 * @protocol: HDCP adaptation as per ME/GSC FW
 * @k: No of streams transmitted on a port. Only on DP MST this is != 1
 * @seq_num_m: Count of RepeaterAuth_Stream_Manage msg propagated.
 *	       Initialized to 0 on AKE_INIT. Incremented after every successful
 *	       transmission of RepeaterAuth_Stream_Manage message. When it rolls
 *	       over re-Auth has to be triggered.
 * @streams: struct hdcp2_streamid_type[k]. Defines the type and id for the
 *	     streams
 */
struct hdcp_port_data {
	enum hdcp_ddi hdcp_ddi;
	enum hdcp_transcoder hdcp_transcoder;
	u8 port_type;
	u8 protocol;
	u16 k;
	u32 seq_num_m;
	struct hdcp2_streamid_type *streams;
};

/**
 * struct i915_hdcp_ops- ops for HDCP2.2 services.
 * @owner: Module providing the ops
 * @initiate_hdcp2_session: Initiate a Wired HDCP2.2 Tx Session.
 *			    And Prepare AKE_Init.
 * @verify_receiver_cert_prepare_km: Verify the Receiver Certificate
 *				     AKE_Send_Cert and prepare
 *				     AKE_Stored_Km/AKE_No_Stored_Km
 * @verify_hprime: Verify AKE_Send_H_prime
 * @store_pairing_info: Store pairing info received
 * @initiate_locality_check: Prepare LC_Init
 * @verify_lprime: Verify lprime
 * @get_session_key: Prepare SKE_Send_Eks
 * @repeater_check_flow_prepare_ack: Validate the Downstream topology
 *				     and prepare rep_ack
 * @verify_mprime: Verify mprime
 * @enable_hdcp_authentication:  Mark a port as authenticated.
 * @close_hdcp_session: Close the Wired HDCP Tx session per port.
 *			This also disables the authenticated state of the port.
 */
struct i915_hdcp_ops {
	/**
	 * @owner: hdcp module
	 */
	struct module *owner;

	int (*initiate_hdcp2_session)(struct device *dev,
				      struct hdcp_port_data *data,
				      struct hdcp2_ake_init *ake_data);
	int (*verify_receiver_cert_prepare_km)(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_ake_send_cert
								*rx_cert,
					       bool *km_stored,
					       struct hdcp2_ake_no_stored_km
								*ek_pub_km,
					       size_t *msg_sz);
	int (*verify_hprime)(struct device *dev,
			     struct hdcp_port_data *data,
			     struct hdcp2_ake_send_hprime *rx_hprime);
	int (*store_pairing_info)(struct device *dev,
				  struct hdcp_port_data *data,
				  struct hdcp2_ake_send_pairing_info
								*pairing_info);
	int (*initiate_locality_check)(struct device *dev,
				       struct hdcp_port_data *data,
				       struct hdcp2_lc_init *lc_init_data);
	int (*verify_lprime)(struct device *dev,
			     struct hdcp_port_data *data,
			     struct hdcp2_lc_send_lprime *rx_lprime);
	int (*get_session_key)(struct device *dev,
			       struct hdcp_port_data *data,
			       struct hdcp2_ske_send_eks *ske_data);
	int (*repeater_check_flow_prepare_ack)(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_rep_send_receiverid_list
								*rep_topology,
					       struct hdcp2_rep_send_ack
								*rep_send_ack);
	int (*verify_mprime)(struct device *dev,
			     struct hdcp_port_data *data,
			     struct hdcp2_rep_stream_ready *stream_ready);
	int (*enable_hdcp_authentication)(struct device *dev,
					  struct hdcp_port_data *data);
	int (*close_hdcp_session)(struct device *dev,
				  struct hdcp_port_data *data);
};

/**
 * struct i915_hdcp_arbiter - Used for communication between i915
 * and hdcp drivers for the HDCP2.2 services
 */
struct i915_hdcp_arbiter {
	/**
	 * @hdcp_dev: device that provides the HDCP2.2 service from MEI Bus.
	 */
	struct device *hdcp_dev;

	/**
	 * @ops: Ops implemented by hdcp driver or intel_hdcp_gsc, used by i915
	 * driver.
	 */
	const struct i915_hdcp_ops *ops;

	/**
	 * @mutex: To protect the above members.
	 */
	struct mutex mutex;
};

/* fw_hdcp_status: Enumeration of all HDCP Status Codes */
enum fw_hdcp_status {
	FW_HDCP_STATUS_SUCCESS			= 0x0000,

	/* WiDi Generic Status Codes */
	FW_HDCP_STATUS_INTERNAL_ERROR		= 0x1000,
	FW_HDCP_STATUS_UNKNOWN_ERROR		= 0x1001,
	FW_HDCP_STATUS_INCORRECT_API_VERSION	= 0x1002,
	FW_HDCP_STATUS_INVALID_FUNCTION		= 0x1003,
	FW_HDCP_STATUS_INVALID_BUFFER_LENGTH	= 0x1004,
	FW_HDCP_STATUS_INVALID_PARAMS		= 0x1005,
	FW_HDCP_STATUS_AUTHENTICATION_FAILED	= 0x1006,

	/* WiDi Status Codes */
	FW_HDCP_INVALID_SESSION_STATE		= 0x6000,
	FW_HDCP_SRM_FRAGMENT_UNEXPECTED		= 0x6001,
	FW_HDCP_SRM_INVALID_LENGTH		= 0x6002,
	FW_HDCP_SRM_FRAGMENT_OFFSET_INVALID	= 0x6003,
	FW_HDCP_SRM_VERIFICATION_FAILED		= 0x6004,
	FW_HDCP_SRM_VERSION_TOO_OLD		= 0x6005,
	FW_HDCP_RX_CERT_VERIFICATION_FAILED	= 0x6006,
	FW_HDCP_RX_REVOKED			= 0x6007,
	FW_HDCP_H_VERIFICATION_FAILED		= 0x6008,
	FW_HDCP_REPEATER_CHECK_UNEXPECTED	= 0x6009,
	FW_HDCP_TOPOLOGY_MAX_EXCEEDED		= 0x600A,
	FW_HDCP_V_VERIFICATION_FAILED		= 0x600B,
	FW_HDCP_L_VERIFICATION_FAILED		= 0x600C,
	FW_HDCP_STREAM_KEY_ALLOC_FAILED		= 0x600D,
	FW_HDCP_BASE_KEY_RESET_FAILED		= 0x600E,
	FW_HDCP_NONCE_GENERATION_FAILED		= 0x600F,
	FW_HDCP_STATUS_INVALID_E_KEY_STATE	= 0x6010,
	FW_HDCP_STATUS_INVALID_CS_ICV		= 0x6011,
	FW_HDCP_STATUS_INVALID_KB_KEY_STATE	= 0x6012,
	FW_HDCP_STATUS_INVALID_PAVP_MODE_ICV	= 0x6013,
	FW_HDCP_STATUS_INVALID_PAVP_MODE	= 0x6014,
	FW_HDCP_STATUS_LC_MAX_ATTEMPTS		= 0x6015,

	/* New status for HDCP 2.1 */
	FW_HDCP_STATUS_MISMATCH_IN_M		= 0x6016,

	/* New status code for HDCP 2.2 Rx */
	FW_HDCP_STATUS_RX_PROV_NOT_ALLOWED	= 0x6017,
	FW_HDCP_STATUS_RX_PROV_WRONG_SUBJECT	= 0x6018,
	FW_HDCP_RX_NEEDS_PROVISIONING		= 0x6019,
	FW_HDCP_BKSV_ICV_AUTH_FAILED		= 0x6020,
	FW_HDCP_STATUS_INVALID_STREAM_ID	= 0x6021,
	FW_HDCP_STATUS_CHAIN_NOT_INITIALIZED	= 0x6022,
	FW_HDCP_FAIL_NOT_EXPECTED		= 0x6023,
	FW_HDCP_FAIL_HDCP_OFF			= 0x6024,
	FW_HDCP_FAIL_INVALID_PAVP_MEMORY_MODE	= 0x6025,
	FW_HDCP_FAIL_AES_ECB_FAILURE		= 0x6026,
	FW_HDCP_FEATURE_NOT_SUPPORTED		= 0x6027,
	FW_HDCP_DMA_READ_ERROR			= 0x6028,
	FW_HDCP_DMA_WRITE_ERROR			= 0x6029,
	FW_HDCP_FAIL_INVALID_PACKET_SIZE	= 0x6030,
	FW_HDCP_H264_PARSING_ERROR		= 0x6031,
	FW_HDCP_HDCP2_ERRATA_VIDEO_VIOLATION	= 0x6032,
	FW_HDCP_HDCP2_ERRATA_AUDIO_VIOLATION	= 0x6033,
	FW_HDCP_TX_ACTIVE_ERROR			= 0x6034,
	FW_HDCP_MODE_CHANGE_ERROR		= 0x6035,
	FW_HDCP_STREAM_TYPE_ERROR		= 0x6036,
	FW_HDCP_STREAM_MANAGE_NOT_POSSIBLE	= 0x6037,

	FW_HDCP_STATUS_PORT_INVALID_COMMAND	= 0x6038,
	FW_HDCP_STATUS_UNSUPPORTED_PROTOCOL	= 0x6039,
	FW_HDCP_STATUS_INVALID_PORT_INDEX	= 0x603a,
	FW_HDCP_STATUS_TX_AUTH_NEEDED		= 0x603b,
	FW_HDCP_STATUS_NOT_INTEGRATED_PORT	= 0x603c,
	FW_HDCP_STATUS_SESSION_MAX_REACHED	= 0x603d,

	/* hdcp capable bit is not set in rx_caps(error is unique to DP) */
	FW_HDCP_STATUS_NOT_HDCP_CAPABLE		= 0x6041,

	FW_HDCP_STATUS_INVALID_STREAM_COUNT	= 0x6042,
};

#define HDCP_API_VERSION				0x00010000

#define HDCP_M_LEN					16
#define HDCP_KH_LEN					16

/* Payload Buffer size(Excluding Header) for CMDs and corresponding response */
/* Wired_Tx_AKE  */
#define	WIRED_CMD_BUF_LEN_INITIATE_HDCP2_SESSION_IN	(4 + 1)
#define	WIRED_CMD_BUF_LEN_INITIATE_HDCP2_SESSION_OUT	(4 + 8 + 3)

#define	WIRED_CMD_BUF_LEN_VERIFY_RECEIVER_CERT_IN	(4 + 522 + 8 + 3)
#define	WIRED_CMD_BUF_LEN_VERIFY_RECEIVER_CERT_MIN_OUT	(4 + 1 + 3 + 16 + 16)
#define	WIRED_CMD_BUF_LEN_VERIFY_RECEIVER_CERT_MAX_OUT	(4 + 1 + 3 + 128)

#define	WIRED_CMD_BUF_LEN_AKE_SEND_HPRIME_IN		(4 + 32)
#define	WIRED_CMD_BUF_LEN_AKE_SEND_HPRIME_OUT		(4)

#define	WIRED_CMD_BUF_LEN_SEND_PAIRING_INFO_IN		(4 + 16)
#define	WIRED_CMD_BUF_LEN_SEND_PAIRING_INFO_OUT		(4)

#define	WIRED_CMD_BUF_LEN_CLOSE_SESSION_IN		(4)
#define	WIRED_CMD_BUF_LEN_CLOSE_SESSION_OUT		(4)

/* Wired_Tx_LC */
#define	WIRED_CMD_BUF_LEN_INIT_LOCALITY_CHECK_IN	(4)
#define	WIRED_CMD_BUF_LEN_INIT_LOCALITY_CHECK_OUT	(4 + 8)

#define	WIRED_CMD_BUF_LEN_VALIDATE_LOCALITY_IN		(4 + 32)
#define	WIRED_CMD_BUF_LEN_VALIDATE_LOCALITY_OUT		(4)

/* Wired_Tx_SKE */
#define	WIRED_CMD_BUF_LEN_GET_SESSION_KEY_IN		(4)
#define	WIRED_CMD_BUF_LEN_GET_SESSION_KEY_OUT		(4 + 16 + 8)

/* Wired_Tx_SKE */
#define	WIRED_CMD_BUF_LEN_ENABLE_AUTH_IN		(4 + 1)
#define	WIRED_CMD_BUF_LEN_ENABLE_AUTH_OUT		(4)

/* Wired_Tx_Repeater */
#define	WIRED_CMD_BUF_LEN_VERIFY_REPEATER_IN		(4 + 2 + 3 + 16 + 155)
#define	WIRED_CMD_BUF_LEN_VERIFY_REPEATER_OUT		(4 + 1 + 16)

#define	WIRED_CMD_BUF_LEN_REPEATER_AUTH_STREAM_REQ_MIN_IN	(4 + 3 + \
								32 + 2 + 2)

#define	WIRED_CMD_BUF_LEN_REPEATER_AUTH_STREAM_REQ_OUT		(4)

/* hdcp_command_id: Enumeration of all WIRED HDCP Command IDs */
enum hdcp_command_id {
	_WIDI_COMMAND_BASE		= 0x00030000,
	WIDI_INITIATE_HDCP2_SESSION	= _WIDI_COMMAND_BASE,
	HDCP_GET_SRM_STATUS,
	HDCP_SEND_SRM_FRAGMENT,

	/* The wired HDCP Tx commands */
	_WIRED_COMMAND_BASE		= 0x00031000,
	WIRED_INITIATE_HDCP2_SESSION	= _WIRED_COMMAND_BASE,
	WIRED_VERIFY_RECEIVER_CERT,
	WIRED_AKE_SEND_HPRIME,
	WIRED_AKE_SEND_PAIRING_INFO,
	WIRED_INIT_LOCALITY_CHECK,
	WIRED_VALIDATE_LOCALITY,
	WIRED_GET_SESSION_KEY,
	WIRED_ENABLE_AUTH,
	WIRED_VERIFY_REPEATER,
	WIRED_REPEATER_AUTH_STREAM_REQ,
	WIRED_CLOSE_SESSION,

	_WIRED_COMMANDS_COUNT,
};

union encrypted_buff {
	u8		e_kpub_km[HDCP_2_2_E_KPUB_KM_LEN];
	u8		e_kh_km_m[HDCP_2_2_E_KH_KM_M_LEN];
	struct {
		u8	e_kh_km[HDCP_KH_LEN];
		u8	m[HDCP_M_LEN];
	} __packed;
};

/* HDCP HECI message header. All header values are little endian. */
struct hdcp_cmd_header {
	u32			api_version;
	u32			command_id;
	enum fw_hdcp_status	status;
	/* Length of the HECI message (excluding the header) */
	u32			buffer_len;
} __packed;

/* Empty command request or response. No data follows the header. */
struct hdcp_cmd_no_data {
	struct hdcp_cmd_header header;
} __packed;

/* Uniquely identifies the hdcp port being addressed for a given command. */
struct hdcp_port_id {
	u8	integrated_port_type;
	/* physical_port is used until Gen11.5. Must be zero for Gen11.5+ */
	u8	physical_port;
	/* attached_transcoder is for Gen11.5+. Set to zero for <Gen11.5 */
	u8	attached_transcoder;
	u8	reserved;
} __packed;

/*
 * Data structures for integrated wired HDCP2 Tx in
 * support of the AKE protocol
 */
/* HECI struct for integrated wired HDCP Tx session initiation. */
struct wired_cmd_initiate_hdcp2_session_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			protocol; /* for HDMI vs DP */
} __packed;

struct wired_cmd_initiate_hdcp2_session_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			r_tx[HDCP_2_2_RTX_LEN];
	struct hdcp2_tx_caps	tx_caps;
} __packed;

/* HECI struct for ending an integrated wired HDCP Tx session. */
struct wired_cmd_close_session_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

struct wired_cmd_close_session_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

/* HECI struct for integrated wired HDCP Tx Rx Cert verification. */
struct wired_cmd_verify_receiver_cert_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	struct hdcp2_cert_rx	cert_rx;
	u8			r_rx[HDCP_2_2_RRX_LEN];
	u8			rx_caps[HDCP_2_2_RXCAPS_LEN];
} __packed;

struct wired_cmd_verify_receiver_cert_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			km_stored;
	u8			reserved[3];
	union encrypted_buff	ekm_buff;
} __packed;

/* HECI struct for verification of Rx's Hprime in a HDCP Tx session */
struct wired_cmd_ake_send_hprime_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			h_prime[HDCP_2_2_H_PRIME_LEN];
} __packed;

struct wired_cmd_ake_send_hprime_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

/*
 * HECI struct for sending in AKE pairing data generated by the Rx in an
 * integrated wired HDCP Tx session.
 */
struct wired_cmd_ake_send_pairing_info_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			e_kh_km[HDCP_2_2_E_KH_KM_LEN];
} __packed;

struct wired_cmd_ake_send_pairing_info_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

/* Data structures for integrated wired HDCP2 Tx in support of the LC protocol*/
/*
 * HECI struct for initiating locality check with an
 * integrated wired HDCP Tx session.
 */
struct wired_cmd_init_locality_check_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

struct wired_cmd_init_locality_check_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			r_n[HDCP_2_2_RN_LEN];
} __packed;

/*
 * HECI struct for validating an Rx's LPrime value in an
 * integrated wired HDCP Tx session.
 */
struct wired_cmd_validate_locality_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			l_prime[HDCP_2_2_L_PRIME_LEN];
} __packed;

struct wired_cmd_validate_locality_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

/*
 * Data structures for integrated wired HDCP2 Tx in support of the
 * SKE protocol
 */
/* HECI struct for creating session key */
struct wired_cmd_get_session_key_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

struct wired_cmd_get_session_key_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			e_dkey_ks[HDCP_2_2_E_DKEY_KS_LEN];
	u8			r_iv[HDCP_2_2_RIV_LEN];
} __packed;

/* HECI struct for the Tx enable authentication command */
struct wired_cmd_enable_auth_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			stream_type;
} __packed;

struct wired_cmd_enable_auth_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

/*
 * Data structures for integrated wired HDCP2 Tx in support of
 * the repeater protocols
 */
/*
 * HECI struct for verifying the downstream repeater's HDCP topology in an
 * integrated wired HDCP Tx session.
 */
struct wired_cmd_verify_repeater_in {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			rx_info[HDCP_2_2_RXINFO_LEN];
	u8			seq_num_v[HDCP_2_2_SEQ_NUM_LEN];
	u8			v_prime[HDCP_2_2_V_PRIME_HALF_LEN];
	u8			receiver_ids[HDCP_2_2_RECEIVER_IDS_MAX_LEN];
} __packed;

struct wired_cmd_verify_repeater_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
	u8			content_type_supported;
	u8			v[HDCP_2_2_V_PRIME_HALF_LEN];
} __packed;

/*
 * HECI struct in support of stream management in an
 * integrated wired HDCP Tx session.
 */
struct wired_cmd_repeater_auth_stream_req_in {
	struct hdcp_cmd_header		header;
	struct hdcp_port_id		port;
	u8				seq_num_m[HDCP_2_2_SEQ_NUM_LEN];
	u8				m_prime[HDCP_2_2_MPRIME_LEN];
	__be16				k;
	struct hdcp2_streamid_type	streams[];
} __packed;

struct wired_cmd_repeater_auth_stream_req_out {
	struct hdcp_cmd_header	header;
	struct hdcp_port_id	port;
} __packed;

#endif /* _I915_HDCP_INTERFACE_H_ */
