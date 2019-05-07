/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#ifndef _DRM_HDCP_H_INCLUDED_
#define _DRM_HDCP_H_INCLUDED_

#include <linux/types.h>

/* Period of hdcp checks (to ensure we're still authenticated) */
#define DRM_HDCP_CHECK_PERIOD_MS		(128 * 16)
#define DRM_HDCP2_CHECK_PERIOD_MS		500

/* Shared lengths/masks between HDMI/DVI/DisplayPort */
#define DRM_HDCP_AN_LEN				8
#define DRM_HDCP_BSTATUS_LEN			2
#define DRM_HDCP_KSV_LEN			5
#define DRM_HDCP_RI_LEN				2
#define DRM_HDCP_V_PRIME_PART_LEN		4
#define DRM_HDCP_V_PRIME_NUM_PARTS		5
#define DRM_HDCP_NUM_DOWNSTREAM(x)		(x & 0x7f)
#define DRM_HDCP_MAX_CASCADE_EXCEEDED(x)	(x & BIT(3))
#define DRM_HDCP_MAX_DEVICE_EXCEEDED(x)		(x & BIT(7))

/* Slave address for the HDCP registers in the receiver */
#define DRM_HDCP_DDC_ADDR			0x3A

/* HDCP register offsets for HDMI/DVI devices */
#define DRM_HDCP_DDC_BKSV			0x00
#define DRM_HDCP_DDC_RI_PRIME			0x08
#define DRM_HDCP_DDC_AKSV			0x10
#define DRM_HDCP_DDC_AN				0x18
#define DRM_HDCP_DDC_V_PRIME(h)			(0x20 + h * 4)
#define DRM_HDCP_DDC_BCAPS			0x40
#define  DRM_HDCP_DDC_BCAPS_REPEATER_PRESENT	BIT(6)
#define  DRM_HDCP_DDC_BCAPS_KSV_FIFO_READY	BIT(5)
#define DRM_HDCP_DDC_BSTATUS			0x41
#define DRM_HDCP_DDC_KSV_FIFO			0x43

#define DRM_HDCP_1_4_SRM_ID			0x8
#define DRM_HDCP_1_4_VRL_LENGTH_SIZE		3
#define DRM_HDCP_1_4_DCP_SIG_SIZE		40

/* Protocol message definition for HDCP2.2 specification */
/*
 * Protected content streams are classified into 2 types:
 * - Type0: Can be transmitted with HDCP 1.4+
 * - Type1: Can be transmitted with HDCP 2.2+
 */
#define HDCP_STREAM_TYPE0			0x00
#define HDCP_STREAM_TYPE1			0x01

/* HDCP2.2 Msg IDs */
#define HDCP_2_2_NULL_MSG			1
#define HDCP_2_2_AKE_INIT			2
#define HDCP_2_2_AKE_SEND_CERT			3
#define HDCP_2_2_AKE_NO_STORED_KM		4
#define HDCP_2_2_AKE_STORED_KM			5
#define HDCP_2_2_AKE_SEND_HPRIME		7
#define HDCP_2_2_AKE_SEND_PAIRING_INFO		8
#define HDCP_2_2_LC_INIT			9
#define HDCP_2_2_LC_SEND_LPRIME			10
#define HDCP_2_2_SKE_SEND_EKS			11
#define HDCP_2_2_REP_SEND_RECVID_LIST		12
#define HDCP_2_2_REP_SEND_ACK			15
#define HDCP_2_2_REP_STREAM_MANAGE		16
#define HDCP_2_2_REP_STREAM_READY		17

#define HDCP_2_2_RTX_LEN			8
#define HDCP_2_2_RRX_LEN			8

#define HDCP_2_2_K_PUB_RX_MOD_N_LEN		128
#define HDCP_2_2_K_PUB_RX_EXP_E_LEN		3
#define HDCP_2_2_K_PUB_RX_LEN			(HDCP_2_2_K_PUB_RX_MOD_N_LEN + \
						 HDCP_2_2_K_PUB_RX_EXP_E_LEN)

#define HDCP_2_2_DCP_LLC_SIG_LEN		384

#define HDCP_2_2_E_KPUB_KM_LEN			128
#define HDCP_2_2_E_KH_KM_M_LEN			(16 + 16)
#define HDCP_2_2_H_PRIME_LEN			32
#define HDCP_2_2_E_KH_KM_LEN			16
#define HDCP_2_2_RN_LEN				8
#define HDCP_2_2_L_PRIME_LEN			32
#define HDCP_2_2_E_DKEY_KS_LEN			16
#define HDCP_2_2_RIV_LEN			8
#define HDCP_2_2_SEQ_NUM_LEN			3
#define HDCP_2_2_V_PRIME_HALF_LEN		(HDCP_2_2_L_PRIME_LEN / 2)
#define HDCP_2_2_RECEIVER_ID_LEN		DRM_HDCP_KSV_LEN
#define HDCP_2_2_MAX_DEVICE_COUNT		31
#define HDCP_2_2_RECEIVER_IDS_MAX_LEN		(HDCP_2_2_RECEIVER_ID_LEN * \
						 HDCP_2_2_MAX_DEVICE_COUNT)
#define HDCP_2_2_MPRIME_LEN			32

/* Following Macros take a byte at a time for bit(s) masking */
/*
 * TODO: This has to be changed for DP MST, as multiple stream on
 * same port is possible.
 * For HDCP2.2 on HDMI and DP SST this value is always 1.
 */
#define HDCP_2_2_MAX_CONTENT_STREAMS_CNT	1
#define HDCP_2_2_TXCAP_MASK_LEN			2
#define HDCP_2_2_RXCAPS_LEN			3
#define HDCP_2_2_RX_REPEATER(x)			((x) & BIT(0))
#define HDCP_2_2_DP_HDCP_CAPABLE(x)		((x) & BIT(1))
#define HDCP_2_2_RXINFO_LEN			2

/* HDCP1.x compliant device in downstream */
#define HDCP_2_2_HDCP1_DEVICE_CONNECTED(x)	((x) & BIT(0))

/* HDCP2.0 Compliant repeater in downstream */
#define HDCP_2_2_HDCP_2_0_REP_CONNECTED(x)	((x) & BIT(1))
#define HDCP_2_2_MAX_CASCADE_EXCEEDED(x)	((x) & BIT(2))
#define HDCP_2_2_MAX_DEVS_EXCEEDED(x)		((x) & BIT(3))
#define HDCP_2_2_DEV_COUNT_LO(x)		(((x) & (0xF << 4)) >> 4)
#define HDCP_2_2_DEV_COUNT_HI(x)		((x) & BIT(0))
#define HDCP_2_2_DEPTH(x)			(((x) & (0x7 << 1)) >> 1)

struct hdcp2_cert_rx {
	u8	receiver_id[HDCP_2_2_RECEIVER_ID_LEN];
	u8	kpub_rx[HDCP_2_2_K_PUB_RX_LEN];
	u8	reserved[2];
	u8	dcp_signature[HDCP_2_2_DCP_LLC_SIG_LEN];
} __packed;

struct hdcp2_streamid_type {
	u8	stream_id;
	u8	stream_type;
} __packed;

/*
 * The TxCaps field specified in the HDCP HDMI, DP specs
 * This field is big endian as specified in the errata.
 */
struct hdcp2_tx_caps {
	/* Transmitter must set this to 0x2 */
	u8	version;

	/* Reserved for HDCP and DP Spec. Read as Zero */
	u8	tx_cap_mask[HDCP_2_2_TXCAP_MASK_LEN];
} __packed;

/* Main structures for HDCP2.2 protocol communication */
struct hdcp2_ake_init {
	u8			msg_id;
	u8			r_tx[HDCP_2_2_RTX_LEN];
	struct hdcp2_tx_caps	tx_caps;
} __packed;

struct hdcp2_ake_send_cert {
	u8			msg_id;
	struct hdcp2_cert_rx	cert_rx;
	u8			r_rx[HDCP_2_2_RRX_LEN];
	u8			rx_caps[HDCP_2_2_RXCAPS_LEN];
} __packed;

struct hdcp2_ake_no_stored_km {
	u8	msg_id;
	u8	e_kpub_km[HDCP_2_2_E_KPUB_KM_LEN];
} __packed;

struct hdcp2_ake_stored_km {
	u8	msg_id;
	u8	e_kh_km_m[HDCP_2_2_E_KH_KM_M_LEN];
} __packed;

struct hdcp2_ake_send_hprime {
	u8	msg_id;
	u8	h_prime[HDCP_2_2_H_PRIME_LEN];
} __packed;

struct hdcp2_ake_send_pairing_info {
	u8	msg_id;
	u8	e_kh_km[HDCP_2_2_E_KH_KM_LEN];
} __packed;

struct hdcp2_lc_init {
	u8	msg_id;
	u8	r_n[HDCP_2_2_RN_LEN];
} __packed;

struct hdcp2_lc_send_lprime {
	u8	msg_id;
	u8	l_prime[HDCP_2_2_L_PRIME_LEN];
} __packed;

struct hdcp2_ske_send_eks {
	u8	msg_id;
	u8	e_dkey_ks[HDCP_2_2_E_DKEY_KS_LEN];
	u8	riv[HDCP_2_2_RIV_LEN];
} __packed;

struct hdcp2_rep_send_receiverid_list {
	u8	msg_id;
	u8	rx_info[HDCP_2_2_RXINFO_LEN];
	u8	seq_num_v[HDCP_2_2_SEQ_NUM_LEN];
	u8	v_prime[HDCP_2_2_V_PRIME_HALF_LEN];
	u8	receiver_ids[HDCP_2_2_RECEIVER_IDS_MAX_LEN];
} __packed;

struct hdcp2_rep_send_ack {
	u8	msg_id;
	u8	v[HDCP_2_2_V_PRIME_HALF_LEN];
} __packed;

struct hdcp2_rep_stream_manage {
	u8			msg_id;
	u8			seq_num_m[HDCP_2_2_SEQ_NUM_LEN];
	__be16			k;
	struct hdcp2_streamid_type streams[HDCP_2_2_MAX_CONTENT_STREAMS_CNT];
} __packed;

struct hdcp2_rep_stream_ready {
	u8	msg_id;
	u8	m_prime[HDCP_2_2_MPRIME_LEN];
} __packed;

/* HDCP2.2 TIMEOUTs in mSec */
#define HDCP_2_2_CERT_TIMEOUT_MS		100
#define HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS	1000
#define HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS	200
#define HDCP_2_2_PAIRING_TIMEOUT_MS		200
#define	HDCP_2_2_HDMI_LPRIME_TIMEOUT_MS		20
#define HDCP_2_2_DP_LPRIME_TIMEOUT_MS		7
#define HDCP_2_2_RECVID_LIST_TIMEOUT_MS		3000
#define HDCP_2_2_STREAM_READY_TIMEOUT_MS	100

/* HDMI HDCP2.2 Register Offsets */
#define HDCP_2_2_HDMI_REG_VER_OFFSET		0x50
#define HDCP_2_2_HDMI_REG_WR_MSG_OFFSET		0x60
#define HDCP_2_2_HDMI_REG_RXSTATUS_OFFSET	0x70
#define HDCP_2_2_HDMI_REG_RD_MSG_OFFSET		0x80
#define HDCP_2_2_HDMI_REG_DBG_OFFSET		0xC0

#define HDCP_2_2_HDMI_SUPPORT_MASK		BIT(2)
#define HDCP_2_2_RX_CAPS_VERSION_VAL		0x02
#define HDCP_2_2_SEQ_NUM_MAX			0xFFFFFF
#define	HDCP_2_2_DELAY_BEFORE_ENCRYPTION_EN	200

/* Below macros take a byte at a time and mask the bit(s) */
#define HDCP_2_2_HDMI_RXSTATUS_LEN		2
#define HDCP_2_2_HDMI_RXSTATUS_MSG_SZ_HI(x)	((x) & 0x3)
#define HDCP_2_2_HDMI_RXSTATUS_READY(x)		((x) & BIT(2))
#define HDCP_2_2_HDMI_RXSTATUS_REAUTH_REQ(x)	((x) & BIT(3))

/*
 * Helper functions to convert 24bit big endian hdcp sequence number to
 * host format and back
 */
static inline
u32 drm_hdcp_be24_to_cpu(const u8 seq_num[HDCP_2_2_SEQ_NUM_LEN])
{
	return (u32)(seq_num[2] | seq_num[1] << 8 | seq_num[0] << 16);
}

static inline
void drm_hdcp_cpu_to_be24(u8 seq_num[HDCP_2_2_SEQ_NUM_LEN], u32 val)
{
	seq_num[0] = val >> 16;
	seq_num[1] = val >> 8;
	seq_num[2] = val;
}

#define DRM_HDCP_SRM_GEN1_MAX_BYTES		(5 * 1024)
#define DRM_HDCP_1_4_SRM_ID			0x8
#define DRM_HDCP_SRM_ID_MASK			(0xF << 4)
#define DRM_HDCP_1_4_VRL_LENGTH_SIZE		3
#define DRM_HDCP_1_4_DCP_SIG_SIZE		40
#define DRM_HDCP_2_SRM_ID			0x9
#define DRM_HDCP_2_INDICATOR			0x1
#define DRM_HDCP_2_INDICATOR_MASK		0xF
#define DRM_HDCP_2_VRL_LENGTH_SIZE		3
#define DRM_HDCP_2_DCP_SIG_SIZE			384
#define DRM_HDCP_2_NO_OF_DEV_PLUS_RESERVED_SZ	4
#define DRM_HDCP_2_KSV_COUNT_2_LSBITS(byte)	(((byte) & 0xC) >> 6)

struct hdcp_srm_header {
	u8 srm_id;
	u8 reserved;
	__be16 srm_version;
	u8 srm_gen_no;
} __packed;

struct drm_device;
struct drm_connector;

bool drm_hdcp_check_ksvs_revoked(struct drm_device *dev,
				 u8 *ksvs, u32 ksv_count);
int drm_connector_attach_content_protection_property(
		struct drm_connector *connector);
#endif
