/*	$OpenBSD: mbim.h,v 1.6 2021/03/30 15:59:04 patrick Exp $ */

/*
 * Copyright (c) 2016 genua mbH
 * All rights reserved.
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

/*
 * Mobile Broadband Interface Model
 * https://www.usb.org/sites/default/files/MBIM-Compliance-1.0.pdf
 */
#ifndef _MBIM_H_
#define _MBIM_H_

#define UDESCSUB_MBIM			27
#define MBIM_INTERFACE_ALTSETTING	1

#define MBIM_RESET_FUNCTION		0x05

/*
 * Registration state (MBIM_REGISTER_STATE)
 */
#define MBIM_REGSTATE_UNKNOWN			0
#define MBIM_REGSTATE_DEREGISTERED		1
#define MBIM_REGSTATE_SEARCHING			2
#define MBIM_REGSTATE_HOME			3
#define MBIM_REGSTATE_ROAMING			4
#define MBIM_REGSTATE_PARTNER			5
#define MBIM_REGSTATE_DENIED			6

/*
 * Data classes mask (MBIM_DATA_CLASS)
 */
#define MBIM_DATACLASS_NONE			0x00000000
#define MBIM_DATACLASS_GPRS			0x00000001
#define MBIM_DATACLASS_EDGE			0x00000002
#define MBIM_DATACLASS_UMTS			0x00000004
#define MBIM_DATACLASS_HSDPA			0x00000008
#define MBIM_DATACLASS_HSUPA			0x00000010
#define MBIM_DATACLASS_LTE			0x00000020
#define MBIM_DATACLASS_1XRTT			0x00010000
#define MBIM_DATACLASS_1XEVDO			0x00020000
#define MBIM_DATACLASS_1XEVDO_REV_A		0x00040000
#define MBIM_DATACLASS_1XEVDV			0x00080000
#define MBIM_DATACLASS_3XRTT			0x00100000
#define MBIM_DATACLASS_1XEVDO_REV_B		0x00200000
#define MBIM_DATACLASS_UMB			0x00400000
#define MBIM_DATACLASS_CUSTOM			0x80000000

/*
 * Cell classes mask (MBIM_CELLULAR_CLASS)
 */
#define MBIM_CELLCLASS_GSM			0x00000001
#define MBIM_CELLCLASS_CDMA			0x00000002

/*
 * UUIDs
 */
#define MBIM_UUID_LEN		16

#define MBIM_UUID_BASIC_CONNECT {				\
		0xa2, 0x89, 0xcc, 0x33, 0xbc, 0xbb, 0x8b, 0x4f,	\
		0xb6, 0xb0, 0x13, 0x3e, 0xc2, 0xaa, 0xe6, 0xdf	\
	}

#define MBIM_UUID_CONTEXT_INTERNET {				\
		0x7e, 0x5e, 0x2a, 0x7e, 0x4e, 0x6f, 0x72, 0x72,	\
		0x73, 0x6b, 0x65, 0x6e, 0x7e, 0x5e, 0x2a, 0x7e	\
	}

#define MBIM_UUID_CONTEXT_VPN {				\
		0x9b, 0x9f, 0x7b, 0xbe, 0x89, 0x52, 0x44, 0xb7,	\
		0x83, 0xac, 0xca, 0x41, 0x31, 0x8d, 0xf7, 0xa0	\
	}

#define MBIM_UUID_QMI_MBIM {				\
		0xd1, 0xa3, 0x0b, 0xc2, 0xf9, 0x7a, 0x6e, 0x43,	\
		0xbf, 0x65, 0xc7, 0xe2, 0x4f, 0xb0, 0xf0, 0xd3	\
	}

#define MBIM_CTRLMSG_MINLEN		64
#define MBIM_CTRLMSG_MAXLEN		(4 * 1204)

#define MBIM_MAXSEGSZ_MINVAL		(2 * 1024)

/*
 * Control messages (host to function)
 */
#define MBIM_OPEN_MSG			1U
#define MBIM_CLOSE_MSG			2U
#define MBIM_COMMAND_MSG		3U
#define MBIM_HOST_ERROR_MSG		4U

/*
 * Control messages (function to host)
 */
#define MBIM_OPEN_DONE			0x80000001U
#define MBIM_CLOSE_DONE			0x80000002U
#define MBIM_COMMAND_DONE		0x80000003U
#define MBIM_FUNCTION_ERROR_MSG		0x80000004U
#define MBIM_INDICATE_STATUS_MSG	0x80000007U

/*
 * Generic status codes
 */
#define MBIM_STATUS_SUCCESS			0
#define MBIM_STATUS_BUSY			1
#define MBIM_STATUS_FAILURE			2
#define MBIM_STATUS_SIM_NOT_INSERTED		3
#define MBIM_STATUS_BAD_SIM			4
#define MBIM_STATUS_PIN_REQUIRED		5
#define MBIM_STATUS_PIN_DISABLED		6
#define MBIM_STATUS_NOT_REGISTERED		7
#define MBIM_STATUS_PROVIDERS_NOT_FOUND		8
#define MBIM_STATUS_NO_DEVICE_SUPPORT		9
#define MBIM_STATUS_PROVIDER_NOT_VISIBLE	10
#define MBIM_STATUS_DATA_CLASS_NOT_AVAILABLE	11
#define MBIM_STATUS_PACKET_SERVICE_DETACHED	12
#define MBIM_STATUS_MAX_ACTIVATED_CONTEXTS	13
#define MBIM_STATUS_NOT_INITIALIZED		14
#define MBIM_STATUS_VOICE_CALL_IN_PROGRESS	15
#define MBIM_STATUS_CONTEXT_NOT_ACTIVATED	16
#define MBIM_STATUS_SERVICE_NOT_ACTIVATED	17
#define MBIM_STATUS_INVALID_ACCESS_STRING	18
#define MBIM_STATUS_INVALID_USER_NAME_PWD	19
#define MBIM_STATUS_RADIO_POWER_OFF		20
#define MBIM_STATUS_INVALID_PARAMETERS		21
#define MBIM_STATUS_READ_FAILURE		22
#define MBIM_STATUS_WRITE_FAILURE		23
#define MBIM_STATUS_NO_PHONEBOOK		25
#define MBIM_STATUS_PARAMETER_TOO_LONG		26
#define MBIM_STATUS_STK_BUSY			27
#define MBIM_STATUS_OPERATION_NOT_ALLOWED	28
#define MBIM_STATUS_MEMORY_FAILURE		29
#define MBIM_STATUS_INVALID_MEMORY_INDEX	30
#define MBIM_STATUS_MEMORY_FULL			31
#define MBIM_STATUS_FILTER_NOT_SUPPORTED	32
#define MBIM_STATUS_DSS_INSTANCE_LIMIT		33
#define MBIM_STATUS_INVALID_DEVICE_SERVICE_OPERATION	34
#define MBIM_STATUS_AUTH_INCORRECT_AUTN		35
#define MBIM_STATUS_AUTH_SYNC_FAILURE		36
#define MBIM_STATUS_AUTH_AMF_NOT_SET		37
#define MBIM_STATUS_CONTEXT_NOT_SUPPORTED	38
#define MBIM_STATUS_SMS_UNKNOWN_SMSC_ADDRESS	100
#define MBIM_STATUS_SMS_NETWORK_TIMEOUT		101
#define MBIM_STATUS_SMS_LANG_NOT_SUPPORTED	102
#define MBIM_STATUS_SMS_ENCODING_NOT_SUPPORTED	103
#define MBIM_STATUS_SMS_FORMAT_NOT_SUPPORTED	104

/*
 * Message formats
 */
struct mbim_msghdr {
	/* Msg header */
	uint32_t	type;		/* message type */
	uint32_t	len;		/* message length */
	uint32_t	tid;		/* transaction id */
} __packed;

struct mbim_fraghdr {
	uint32_t	nfrag;		/* total # of fragments */
	uint32_t	currfrag;	/* current fragment */
} __packed;

struct mbim_fragmented_msg_hdr {
	struct mbim_msghdr	hdr;
	struct mbim_fraghdr	frag;
} __packed;

struct mbim_h2f_openmsg {
	struct mbim_msghdr	hdr;
	uint32_t		maxlen;
} __packed;

struct mbim_h2f_closemsg {
	struct mbim_msghdr	hdr;
} __packed;

struct mbim_h2f_cmd {
	struct mbim_msghdr	hdr;
	struct mbim_fraghdr	frag;
	uint8_t			devid[MBIM_UUID_LEN];
	uint32_t		cid;		/* command id */
#define MBIM_CMDOP_QRY		0
#define MBIM_CMDOP_SET		1
	uint32_t		op;
	uint32_t		infolen;
	uint8_t			info[];
} __packed;

struct mbim_f2h_indicate_status {
	struct mbim_msghdr	hdr;
	struct mbim_fraghdr	frag;
	uint8_t			devid[MBIM_UUID_LEN];
	uint32_t		cid;		/* command id */
	uint32_t		infolen;
	uint8_t			info[];
} __packed;

struct mbim_f2h_hosterr {
	struct mbim_msghdr	hdr;

#define MBIM_ERROR_TIMEOUT_FRAGMENT		1
#define MBIM_ERROR_FRAGMENT_OUT_OF_SEQUENCE	2
#define MBIM_ERROR_LENGTH_MISMATCH		3
#define MBIM_ERROR_DUPLICATED_TID		4
#define MBIM_ERROR_NOT_OPENED			5
#define MBIM_ERROR_UNKNOWN			6
#define MBIM_ERROR_CANCEL			7
#define MBIM_ERROR_MAX_TRANSFER			8
	uint32_t		err;
} __packed;

struct mbim_f2h_openclosedone {
	struct mbim_msghdr	hdr;
	int32_t			status;
} __packed;

struct mbim_f2h_cmddone {
	struct mbim_msghdr	hdr;
	struct mbim_fraghdr	frag;
	uint8_t			devid[MBIM_UUID_LEN];
	uint32_t		cid;		/* command id */
	int32_t			status;
	uint32_t		infolen;
	uint8_t			info[];
} __packed;

/*
 * Messages and commands for MBIM_UUID_BASIC_CONNECT
 */
#define MBIM_CID_DEVICE_CAPS				1
#define MBIM_CID_SUBSCRIBER_READY_STATUS		2
#define MBIM_CID_RADIO_STATE				3
#define MBIM_CID_PIN					4
#define MBIM_CID_PIN_LIST				5
#define MBIM_CID_HOME_PROVIDER				6
#define MBIM_CID_PREFERRED_PROVIDERS			7
#define MBIM_CID_VISIBLE_PROVIDERS			8
#define MBIM_CID_REGISTER_STATE				9
#define MBIM_CID_PACKET_SERVICE				10
#define MBIM_CID_SIGNAL_STATE				11
#define MBIM_CID_CONNECT				12
#define MBIM_CID_PROVISIONED_CONTEXTS			13
#define MBIM_CID_SERVICE_ACTIVATION			14
#define MBIM_CID_IP_CONFIGURATION			15
#define MBIM_CID_DEVICE_SERVICES			16
#define MBIM_CID_DEVICE_SERVICE_SUBSCRIBE_LIST		19
#define MBIM_CID_PACKET_STATISTICS			20
#define MBIM_CID_NETWORK_IDLE_HINT			21
#define MBIM_CID_EMERGENCY_MODE				22
#define MBIM_CID_IP_PACKET_FILTERS			23
#define MBIM_CID_MULTICARRIER_PROVIDERS			24

struct mbim_cid_subscriber_ready_info {
#define MBIM_SIMSTATE_NOTINITIALIZED		0
#define MBIM_SIMSTATE_INITIALIZED		1
#define MBIM_SIMSTATE_NOTINSERTED		2
#define MBIM_SIMSTATE_BADSIM			3
#define MBIM_SIMSTATE_FAILURE			4
#define MBIM_SIMSTATE_NOTACTIVATED		5
#define MBIM_SIMSTATE_LOCKED			6
	uint32_t	ready;

	uint32_t	sid_offs;
	uint32_t	sid_size;

	uint32_t	icc_offs;
	uint32_t	icc_size;

#define MBIM_SIMUNIQEID_NONE			0
#define MBIM_SIMUNIQEID_PROTECT			1
	uint32_t	info;

	uint32_t	no_pn;
	struct {
		uint32_t	offs;
		uint32_t	size;
	}
			pn[];
} __packed;

struct mbim_cid_radio_state {
#define MBIM_RADIO_STATE_OFF			0
#define MBIM_RADIO_STATE_ON			1
	uint32_t	state;
} __packed;

struct mbim_cid_radio_state_info {
	uint32_t	hw_state;
	uint32_t	sw_state;
} __packed;

struct mbim_cid_pin {
#define MBIM_PIN_TYPE_NONE			0
#define MBIM_PIN_TYPE_CUSTOM			1
#define MBIM_PIN_TYPE_PIN1			2
#define MBIM_PIN_TYPE_PIN2			3
#define MBIM_PIN_TYPE_DEV_SIM_PIN		4
#define MBIM_PIN_TYPE_DEV_FIRST_SIM_PIN		5
#define MBIM_PIN_TYPE_NETWORK_PIN		6
#define MBIM_PIN_TYPE_NETWORK_SUBSET_PIN	7
#define MBIM_PIN_TYPE_SERVICE_PROVIDER_PIN	8
#define MBIM_PIN_TYPE_CORPORATE_PIN		9
#define MBIM_PIN_TYPE_SUBSIDY_LOCK		10
#define MBIM_PIN_TYPE_PUK1			11
#define MBIM_PIN_TYPE_PUK2			12
#define MBIM_PIN_TYPE_DEV_FIRST_SIM_PUK		13
#define MBIM_PIN_TYPE_NETWORK_PUK		14
#define MBIM_PIN_TYPE_NETWORK_SUBSET_PUK	15
#define MBIM_PIN_TYPE_SERVICE_PROVIDER_PUK	16
#define MBIM_PIN_TYPE_CORPORATE_PUK		17
	uint32_t	type;

#define MBIM_PIN_OP_ENTER			0
#define MBIM_PIN_OP_ENABLE			1
#define MBIM_PIN_OP_DISABLE			2
#define MBIM_PIN_OP_CHANGE			3
	uint32_t	op;
	uint32_t	pin_offs;
	uint32_t	pin_size;
	uint32_t	newpin_offs;
	uint32_t	newpin_size;
#define MBIM_PIN_MAXLEN	32
	uint8_t		data[2 * MBIM_PIN_MAXLEN];
} __packed;

struct mbim_cid_pin_info {
	uint32_t	type;

#define MBIM_PIN_STATE_UNLOCKED			0
#define MBIM_PIN_STATE_LOCKED			1
	uint32_t	state;
	uint32_t	remaining_attempts;
} __packed;

struct mbim_cid_pin_list_info {
	struct mbim_pin_desc {

#define MBIM_PINMODE_NOTSUPPORTED		0
#define MBIM_PINMODE_ENABLED			1
#define MBIM_PINMODE_DISABLED			2
		uint32_t	mode;

#define MBIM_PINFORMAT_UNKNOWN			0
#define MBIM_PINFORMAT_NUMERIC			1
#define MBIM_PINFORMAT_ALPHANUMERIC		2
		uint32_t	format;

		uint32_t	minlen;
		uint32_t	maxlen;
	}
		pin1,
		pin2,
		dev_sim_pin,
		first_dev_sim_pin,
		net_pin,
		net_sub_pin,
		svp_pin,
		corp_pin,
		subsidy_lock,
		custom;
} __packed;

struct mbim_cid_device_caps {
#define MBIM_DEVTYPE_UNKNOWN			0
#define MBIM_DEVTYPE_EMBEDDED			1
#define MBIM_DEVTYPE_REMOVABLE			2
#define MBIM_DEVTYPE_REMOTE			3
	uint32_t	devtype;

	uint32_t	cellclass;	/* values: MBIM_CELLULAR_CLASS */
	uint32_t	voiceclass;
	uint32_t	simclass;
	uint32_t	dataclass;	/* values: MBIM_DATA_CLASS */
	uint32_t	smscaps;
	uint32_t	cntrlcaps;
	uint32_t	max_sessions;

	uint32_t	custdataclass_offs;
	uint32_t	custdataclass_size;

	uint32_t	devid_offs;
	uint32_t	devid_size;

	uint32_t	fwinfo_offs;
	uint32_t	fwinfo_size;

	uint32_t	hwinfo_offs;
	uint32_t	hwinfo_size;

	uint32_t	data[];
} __packed;

struct mbim_cid_registration_state {
	uint32_t	provid_offs;
	uint32_t	provid_size;

#define MBIM_REGACTION_AUTOMATIC		0
#define MBIM_REGACTION_MANUAL			1
	uint32_t	regaction;
	uint32_t	data_class;

	uint32_t	data[];
} __packed;

struct mbim_cid_registration_state_info {
	uint32_t	nwerror;

	uint32_t	regstate;	/* values: MBIM_REGISTER_STATE */

#define MBIM_REGMODE_UNKNOWN			0
#define MBIM_REGMODE_AUTOMATIC			1
#define MBIM_REGMODE_MANUAL			2
	uint32_t	regmode;

	uint32_t	availclasses;	/* values: MBIM_DATA_CLASS */
	uint32_t	curcellclass;	/* values: MBIM_CELLULAR_CLASS */

	uint32_t	provid_offs;
	uint32_t	provid_size;

	uint32_t	provname_offs;
	uint32_t	provname_size;

	uint32_t	roamingtxt_offs;
	uint32_t	roamingtxt_size;

#define MBIM_REGFLAGS_NONE			0
#define MBIM_REGFLAGS_MANUAL_NOT_AVAILABLE	1
#define MBIM_REGFLAGS_PACKETSERVICE_AUTOATTACH	2
	uint32_t	regflag;

	uint32_t	data[];
} __packed;

struct mbim_cid_packet_service {
#define MBIM_PKTSERVICE_ACTION_ATTACH		0
#define MBIM_PKTSERVICE_ACTION_DETACH		1
	uint32_t	action;
} __packed;

struct mbim_cid_packet_service_info {
	uint32_t	nwerror;

#define MBIM_PKTSERVICE_STATE_UNKNOWN		0
#define MBIM_PKTSERVICE_STATE_ATTACHING		1
#define MBIM_PKTSERVICE_STATE_ATTACHED		2
#define MBIM_PKTSERVICE_STATE_DETACHING		3
#define MBIM_PKTSERVICE_STATE_DETACHED		4
	uint32_t	state;

	uint32_t	highest_dataclass;
	uint64_t	uplink_speed;
	uint64_t	downlink_speed;
} __packed;

struct mbim_cid_signal_state {
	uint32_t	rssi;
	uint32_t	err_rate;
	uint32_t	ss_intvl;
	uint32_t	rssi_thr;
	uint32_t	err_thr;
} __packed;

struct mbim_cid_connect {
	uint32_t	sessionid;

#define MBIM_CONNECT_DEACTIVATE		0
#define MBIM_CONNECT_ACTIVATE		1
	uint32_t	command;

#define MBIM_ACCESS_MAXLEN		200
	uint32_t	access_offs;
	uint32_t	access_size;

#define MBIM_USER_MAXLEN		510
	uint32_t	user_offs;
	uint32_t	user_size;

#define MBIM_PASSWD_MAXLEN		510
	uint32_t	passwd_offs;
	uint32_t	passwd_size;

#define MBIM_COMPRESSION_NONE		0
#define MBIM_COMPRESSION_ENABLE		1
	uint32_t	compression;

#define MBIM_AUTHPROT_NONE		0
#define MBIM_AUTHPROT_PAP		1
#define MBIM_AUTHPROT_CHAP		2
#define MBIM_AUTHPROT_MSCHAP		3
	uint32_t	authprot;

#define MBIM_CONTEXT_IPTYPE_DEFAULT	0
#define MBIM_CONTEXT_IPTYPE_IPV4	1
#define MBIM_CONTEXT_IPTYPE_IPV6	2
#define MBIM_CONTEXT_IPTYPE_IPV4V6	3
#define MBIM_CONTEXT_IPTYPE_IPV4ANDV6	4
	uint32_t	iptype;

	uint8_t		context[MBIM_UUID_LEN];

	uint8_t		data[MBIM_ACCESS_MAXLEN + MBIM_USER_MAXLEN +
			     MBIM_PASSWD_MAXLEN];

} __packed;

struct mbim_cid_connect_info {
	uint32_t	sessionid;

#define MBIM_ACTIVATION_STATE_UNKNOWN		0
#define MBIM_ACTIVATION_STATE_ACTIVATED		1
#define MBIM_ACTIVATION_STATE_ACTIVATING	2
#define MBIM_ACTIVATION_STATE_DEACTIVATED	3
#define MBIM_ACTIVATION_STATE_DEACTIVATING	4
	uint32_t	activation;

	uint32_t	voice;
	uint32_t	iptype;
	uint8_t		context[MBIM_UUID_LEN];
	uint32_t	nwerror;
} __packed;

struct mbim_cid_ipv4_element {
	uint32_t	prefixlen;
	uint32_t	addr;
} __packed;

struct mbim_cid_ipv6_element {
	uint32_t	prefixlen;
	uint8_t		addr[16];
} __packed;

struct mbim_cid_ip_configuration_info {
	uint32_t	sessionid;

#define MBIM_IPCONF_HAS_ADDRINFO	0x0001
#define MBIM_IPCONF_HAS_GWINFO		0x0002
#define MBIM_IPCONF_HAS_DNSINFO		0x0004
#define MBIM_IPCONF_HAS_MTUINFO		0x0008
	uint32_t	ipv4_available;
	uint32_t	ipv6_available;

	uint32_t	ipv4_naddr;
	uint32_t	ipv4_addroffs;
	uint32_t	ipv6_naddr;
	uint32_t	ipv6_addroffs;

	uint32_t	ipv4_gwoffs;
	uint32_t	ipv6_gwoffs;

	uint32_t	ipv4_ndnssrv;
	uint32_t	ipv4_dnssrvoffs;
	uint32_t	ipv6_ndnssrv;
	uint32_t	ipv6_dnssrvoffs;

	uint32_t	ipv4_mtu;
	uint32_t	ipv6_mtu;

	uint32_t	data[];
} __packed;

struct mbim_cid_packet_statistics_info {
	uint32_t	in_discards;
	uint32_t	in_errors;
	uint64_t	in_octets;
	uint64_t	in_packets;
	uint64_t	out_octets;
	uint64_t	out_packets;
	uint32_t	out_errors;
	uint32_t	out_discards;
} __packed;


#ifdef _KERNEL

struct mbim_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
#define MBIM_VER_MAJOR(v)	(((v) >> 8) & 0x0f)
#define MBIM_VER_MINOR(v)	((v) & 0x0f)
	uWord	bcdMBIMVersion;
	uWord	wMaxControlMessage;
	uByte	bNumberFilters;
	uByte	bMaxFilterSize;
	uWord	wMaxSegmentSize;
	uByte	bmNetworkCapabilities;
} __packed;

/*
 * NCM Parameters
 */
#define NCM_GET_NTB_PARAMETERS	0x80
#define NCM_GET_NTB_FORMAT	0x83	/* Current format returned as uWord */
#define NCM_SET_NTB_FORMAT	0x84	/* Desired format is in wValue */

#define NCM_FORMAT_NTB16	0x00
#define NCM_FORMAT_NTB32	0x01

struct ncm_ntb_parameters {
	uWord	wLength;
	uWord	bmNtbFormatsSupported;
#define NCM_FORMAT_NTB16_MASK	(1U << NCM_FORMAT_NTB16)
#define NCM_FORMAT_NTB32_MASK	(1U << NCM_FORMAT_NTB32)
	uDWord	dwNtbInMaxSize;
	uWord	wNdpInDivisor;
	uWord	wNdpInPayloadRemainder;
	uWord	wNdpInAlignment;
	uWord	wReserved1;
	uDWord	dwNtbOutMaxSize;
	uWord	wNdpOutDivisor;
	uWord	wNdpOutPayloadRemainder;
	uWord	wNdpOutAlignment;
	uWord	wNtbOutMaxDatagrams;
} __packed;

/*
 * NCM Encoding
 */
struct ncm_header16 {
#define NCM_HDR16_SIG		0x484d434e
	uDWord	dwSignature;
	uWord	wHeaderLength;
	uWord	wSequence;
	uWord	wBlockLength;
	uWord	wNdpIndex;
} __packed;

struct ncm_header32 {
#define NCM_HDR32_SIG		0x686d636e
	uDWord	dwSignature;
	uWord	wHeaderLength;
	uWord	wSequence;
	uDWord	dwBlockLength;
	uDWord	dwNdpIndex;
} __packed;


#define MBIM_NCM_NTH_SIDSHIFT	24
#define MBIM_NCM_NTH_GETSID(s)	(((s) > MBIM_NCM_NTH_SIDSHIFT) & 0xff)

struct ncm_pointer16_dgram {
	uWord	wDatagramIndex;
	uWord	wDatagramLen;
} __packed;

struct ncm_pointer16 {
#define MBIM_NCM_NTH16_IPS	 0x00535049
#define MBIM_NCM_NTH16_ISISG(s) (((s) & 0x00ffffff) == MBIM_NCM_NTH16_IPS)
#define MBIM_NCM_NTH16_SIG(s)	\
		((((s) & 0xff) << MBIM_NCM_NTH_SIDSHIFT) | MBIM_NCM_NTH16_IPS)
	uDWord	dwSignature;
	uWord	wLength;
	uWord	wNextNdpIndex;

	/* Minimum is two datagrams, but can be more */
	struct ncm_pointer16_dgram dgram[1];
} __packed;

struct ncm_pointer32_dgram {
	uDWord	dwDatagramIndex;
	uDWord	dwDatagramLen;
} __packed;

struct ncm_pointer32 {
#define MBIM_NCM_NTH32_IPS	0x00737069
#define MBIM_NCM_NTH32_ISISG(s)	\
		(((s) & 0x00ffffff) == MBIM_NCM_NTH32_IPS)
#define MBIM_NCM_NTH32_SIG(s)		\
		((((s) & 0xff) << MBIM_NCM_NTH_SIDSHIFT) | MBIM_NCM_NTH32_IPS)
	uDWord	dwSignature;
	uWord	wLength;
	uWord	wReserved6;
	uDWord	dwNextNdpIndex;
	uDWord	dwReserved12;

	/* Minimum is two datagrams, but can be more */
	struct ncm_pointer32_dgram dgram[1];
} __packed;

#endif /* _KERNEL */

#endif /* _MBIM_H_ */
