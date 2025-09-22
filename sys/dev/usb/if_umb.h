/*	$OpenBSD: if_umb.h,v 1.12 2025/06/16 12:36:43 gerhard Exp $ */

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

struct umb_valdescr {
	int	 val;
	char	*descr;
};

static const char *
umb_val2descr(const struct umb_valdescr *vdp, int val)
{
	static char sval[32];

	while (vdp->descr != NULL) {
		if (vdp->val == val)
			return vdp->descr;
		vdp++;
	}
	snprintf(sval, sizeof (sval), "#%d", val);
	return sval;
}

#define MBIM_REGSTATE_DESCRIPTIONS {				\
	{ MBIM_REGSTATE_UNKNOWN,	"unknown" },		\
	{ MBIM_REGSTATE_DEREGISTERED,	"not registered" },	\
	{ MBIM_REGSTATE_SEARCHING,	"searching" },		\
	{ MBIM_REGSTATE_HOME,		"home network" },	\
	{ MBIM_REGSTATE_ROAMING,	"roaming network" },	\
	{ MBIM_REGSTATE_PARTNER,	"partner network" },	\
	{ MBIM_REGSTATE_DENIED,		"access denied" },	\
	{ 0, NULL } }

#define MBIM_DATACLASS_DESCRIPTIONS {					\
	{ MBIM_DATACLASS_NONE,				"none" },	\
	{ MBIM_DATACLASS_GPRS,				"GPRS" },	\
	{ MBIM_DATACLASS_EDGE,				"EDGE" },	\
	{ MBIM_DATACLASS_UMTS,				"UMTS" },	\
	{ MBIM_DATACLASS_HSDPA,				"HSDPA" },	\
	{ MBIM_DATACLASS_HSUPA,				"HSUPA" },	\
	{ MBIM_DATACLASS_HSDPA|MBIM_DATACLASS_HSUPA,	"HSPA" },	\
	{ MBIM_DATACLASS_LTE,				"LTE" },	\
	{ MBIM_DATACLASS_1XRTT,				"CDMA2000" },	\
	{ MBIM_DATACLASS_1XEVDO,			"CDMA2000" },	\
	{ MBIM_DATACLASS_1XEVDO_REV_A,			"CDMA2000" },	\
	{ MBIM_DATACLASS_1XEVDV,			"CDMA2000" },	\
	{ MBIM_DATACLASS_3XRTT,				"CDMA2000" },	\
	{ MBIM_DATACLASS_1XEVDO_REV_B,			"CDMA2000" },	\
	{ MBIM_DATACLASS_UMB,				"CDMA2000" },	\
	{ MBIM_DATACLASS_CUSTOM,			"custom" },	\
	{ 0, NULL } }

#define MBIM_1TO1_DESCRIPTION(m)	{ (m), #m }
#define MBIM_MESSAGES_DESCRIPTIONS {				\
	MBIM_1TO1_DESCRIPTION(MBIM_OPEN_MSG),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CLOSE_MSG),			\
	MBIM_1TO1_DESCRIPTION(MBIM_COMMAND_MSG),		\
	MBIM_1TO1_DESCRIPTION(MBIM_HOST_ERROR_MSG),		\
	MBIM_1TO1_DESCRIPTION(MBIM_OPEN_DONE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CLOSE_DONE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_COMMAND_DONE),		\
	MBIM_1TO1_DESCRIPTION(MBIM_FUNCTION_ERROR_MSG),		\
	MBIM_1TO1_DESCRIPTION(MBIM_INDICATE_STATUS_MSG),	\
	{ 0, NULL } }

#define MBIM_STATUS_DESCRIPTION(m)	{ MBIM_STATUS_ ## m, #m }
#define MBIM_STATUS_DESCRIPTIONS {					\
	MBIM_STATUS_DESCRIPTION(SUCCESS),				\
	MBIM_STATUS_DESCRIPTION(BUSY),					\
	MBIM_STATUS_DESCRIPTION(FAILURE),				\
	MBIM_STATUS_DESCRIPTION(SIM_NOT_INSERTED),			\
	MBIM_STATUS_DESCRIPTION(BAD_SIM),				\
	MBIM_STATUS_DESCRIPTION(PIN_REQUIRED),				\
	MBIM_STATUS_DESCRIPTION(PIN_DISABLED),				\
	MBIM_STATUS_DESCRIPTION(NOT_REGISTERED),			\
	MBIM_STATUS_DESCRIPTION(PROVIDERS_NOT_FOUND),			\
	MBIM_STATUS_DESCRIPTION(NO_DEVICE_SUPPORT),			\
	MBIM_STATUS_DESCRIPTION(PROVIDER_NOT_VISIBLE),			\
	MBIM_STATUS_DESCRIPTION(DATA_CLASS_NOT_AVAILABLE),		\
	MBIM_STATUS_DESCRIPTION(PACKET_SERVICE_DETACHED),		\
	MBIM_STATUS_DESCRIPTION(MAX_ACTIVATED_CONTEXTS),		\
	MBIM_STATUS_DESCRIPTION(NOT_INITIALIZED),			\
	MBIM_STATUS_DESCRIPTION(VOICE_CALL_IN_PROGRESS),		\
	MBIM_STATUS_DESCRIPTION(CONTEXT_NOT_ACTIVATED),			\
	MBIM_STATUS_DESCRIPTION(SERVICE_NOT_ACTIVATED),			\
	MBIM_STATUS_DESCRIPTION(INVALID_ACCESS_STRING),			\
	MBIM_STATUS_DESCRIPTION(INVALID_USER_NAME_PWD),			\
	MBIM_STATUS_DESCRIPTION(RADIO_POWER_OFF),			\
	MBIM_STATUS_DESCRIPTION(INVALID_PARAMETERS),			\
	MBIM_STATUS_DESCRIPTION(READ_FAILURE),				\
	MBIM_STATUS_DESCRIPTION(WRITE_FAILURE),				\
	MBIM_STATUS_DESCRIPTION(NO_PHONEBOOK),				\
	MBIM_STATUS_DESCRIPTION(PARAMETER_TOO_LONG),			\
	MBIM_STATUS_DESCRIPTION(STK_BUSY),				\
	MBIM_STATUS_DESCRIPTION(OPERATION_NOT_ALLOWED),			\
	MBIM_STATUS_DESCRIPTION(MEMORY_FAILURE),			\
	MBIM_STATUS_DESCRIPTION(INVALID_MEMORY_INDEX),			\
	MBIM_STATUS_DESCRIPTION(MEMORY_FULL),				\
	MBIM_STATUS_DESCRIPTION(FILTER_NOT_SUPPORTED),			\
	MBIM_STATUS_DESCRIPTION(DSS_INSTANCE_LIMIT),			\
	MBIM_STATUS_DESCRIPTION(INVALID_DEVICE_SERVICE_OPERATION),	\
	MBIM_STATUS_DESCRIPTION(AUTH_INCORRECT_AUTN),			\
	MBIM_STATUS_DESCRIPTION(AUTH_SYNC_FAILURE),			\
	MBIM_STATUS_DESCRIPTION(AUTH_AMF_NOT_SET),			\
	MBIM_STATUS_DESCRIPTION(CONTEXT_NOT_SUPPORTED),			\
	MBIM_STATUS_DESCRIPTION(SMS_UNKNOWN_SMSC_ADDRESS),		\
	MBIM_STATUS_DESCRIPTION(SMS_NETWORK_TIMEOUT),			\
	MBIM_STATUS_DESCRIPTION(SMS_LANG_NOT_SUPPORTED),		\
	MBIM_STATUS_DESCRIPTION(SMS_ENCODING_NOT_SUPPORTED),		\
	MBIM_STATUS_DESCRIPTION(SMS_FORMAT_NOT_SUPPORTED),		\
	{ 0, NULL } }

#define MBIM_ERROR_DESCRIPTION(m)	{ MBIM_ERROR_ ## m, #m }
#define MBIM_ERROR_DESCRIPTIONS {					\
	MBIM_ERROR_DESCRIPTION(TIMEOUT_FRAGMENT),			\
	MBIM_ERROR_DESCRIPTION(FRAGMENT_OUT_OF_SEQUENCE),		\
	MBIM_ERROR_DESCRIPTION(LENGTH_MISMATCH),			\
	MBIM_ERROR_DESCRIPTION(DUPLICATED_TID),				\
	MBIM_ERROR_DESCRIPTION(NOT_OPENED),				\
	MBIM_ERROR_DESCRIPTION(UNKNOWN),				\
	MBIM_ERROR_DESCRIPTION(CANCEL),					\
	MBIM_ERROR_DESCRIPTION(MAX_TRANSFER),				\
	{ 0, NULL } }

#define MBIM_CID_DESCRIPTIONS {						\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_DEVICE_CAPS),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_SUBSCRIBER_READY_STATUS),	\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_RADIO_STATE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_PIN),				\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_PIN_LIST),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_HOME_PROVIDER),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_PREFERRED_PROVIDERS),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_VISIBLE_PROVIDERS),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_REGISTER_STATE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_PACKET_SERVICE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_SIGNAL_STATE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_CONNECT),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_PROVISIONED_CONTEXTS),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_SERVICE_ACTIVATION),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_IP_CONFIGURATION),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_DEVICE_SERVICES),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_DEVICE_SERVICE_SUBSCRIBE_LIST),	\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_PACKET_STATISTICS),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_NETWORK_IDLE_HINT),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_EMERGENCY_MODE),			\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_IP_PACKET_FILTERS),		\
	MBIM_1TO1_DESCRIPTION(MBIM_CID_MULTICARRIER_PROVIDERS),		\
	{ 0, NULL } }

#define MBIM_SIMSTATE_DESCRIPTIONS {					\
	{ MBIM_SIMSTATE_NOTINITIALIZED, "not initialized" },		\
	{ MBIM_SIMSTATE_INITIALIZED, "initialized" },			\
	{ MBIM_SIMSTATE_NOTINSERTED, "not inserted" },			\
	{ MBIM_SIMSTATE_BADSIM, "bad type" },				\
	{ MBIM_SIMSTATE_FAILURE, "failed" },				\
	{ MBIM_SIMSTATE_NOTACTIVATED, "not activated" },		\
	{ MBIM_SIMSTATE_LOCKED, "locked" },				\
	{ 0, NULL } }

#define MBIM_PINTYPE_DESCRIPTIONS {					\
	{ MBIM_PIN_TYPE_NONE, "none" },					\
	{ MBIM_PIN_TYPE_CUSTOM, "custom" },				\
	{ MBIM_PIN_TYPE_PIN1, "PIN1" },					\
	{ MBIM_PIN_TYPE_PIN2, "PIN2" },					\
	{ MBIM_PIN_TYPE_DEV_SIM_PIN, "device PIN" },			\
	{ MBIM_PIN_TYPE_DEV_FIRST_SIM_PIN, "device 1st PIN" },		\
	{ MBIM_PIN_TYPE_NETWORK_PIN, "network PIN" },			\
	{ MBIM_PIN_TYPE_NETWORK_SUBSET_PIN, "network subset PIN" },	\
	{ MBIM_PIN_TYPE_SERVICE_PROVIDER_PIN, "provider PIN" },		\
	{ MBIM_PIN_TYPE_CORPORATE_PIN, "corporate PIN" },		\
	{ MBIM_PIN_TYPE_SUBSIDY_LOCK, "subsidy lock" },			\
	{ MBIM_PIN_TYPE_PUK1, "PUK" },					\
	{ MBIM_PIN_TYPE_PUK2, "PUK2" },					\
	{ MBIM_PIN_TYPE_DEV_FIRST_SIM_PUK, "device 1st PUK" },		\
	{ MBIM_PIN_TYPE_NETWORK_PUK, "network PUK" },			\
	{ MBIM_PIN_TYPE_NETWORK_SUBSET_PUK, "network subset PUK" },	\
	{ MBIM_PIN_TYPE_SERVICE_PROVIDER_PUK, "provider PUK" },		\
	{ MBIM_PIN_TYPE_CORPORATE_PUK, "corporate PUK" },		\
	{ 0, NULL } }

#define MBIM_PKTSRV_STATE_DESCRIPTIONS {				\
	{ MBIM_PKTSERVICE_STATE_UNKNOWN, "unknown" },			\
	{ MBIM_PKTSERVICE_STATE_ATTACHING, "attaching" },		\
	{ MBIM_PKTSERVICE_STATE_ATTACHED, "attached" },			\
	{ MBIM_PKTSERVICE_STATE_DETACHING, "detaching" },		\
	{ MBIM_PKTSERVICE_STATE_DETACHED, "detached" },			\
	{ 0, NULL } }

#define MBIM_ACTIVATION_STATE_DESCRIPTIONS {				\
	{ MBIM_ACTIVATION_STATE_UNKNOWN, "unknown" },			\
	{ MBIM_ACTIVATION_STATE_ACTIVATED, "activated" },		\
	{ MBIM_ACTIVATION_STATE_ACTIVATING, "activating" },		\
	{ MBIM_ACTIVATION_STATE_DEACTIVATED, "deactivated" },		\
	{ MBIM_ACTIVATION_STATE_DEACTIVATING, "deactivating" },		\
	{ 0, NULL } }

/*
 * Driver internal state
 */
enum umb_state {
	UMB_S_DOWN = 0,		/* interface down */
	UMB_S_OPEN,		/* MBIM device has been opened */
	UMB_S_CID,		/* QMI client id allocated */
	UMB_S_RADIO,		/* radio is on */
	UMB_S_SIMREADY,		/* SIM is ready */
	UMB_S_ATTACHED,		/* packet service is attached */
	UMB_S_CONNECTED,	/* connected to provider */
	UMB_S_UP,		/* have IP configuration */
};

#define UMB_INTERNAL_STATE_DESCRIPTIONS {	\
	{ UMB_S_DOWN, "down" },			\
	{ UMB_S_OPEN, "open" },			\
	{ UMB_S_CID, "CID allocated" },		\
	{ UMB_S_RADIO, "radio on" },		\
	{ UMB_S_SIMREADY, "SIM is ready" },	\
	{ UMB_S_ATTACHED, "attached" },		\
	{ UMB_S_CONNECTED, "connected" },	\
	{ UMB_S_UP, "up" },			\
	{ 0, NULL } }

/*
 * UMB parameters (SIOC[GS]UMBPARAM ioctls)
 */
struct umb_parameter {
	int			op;
	int			is_puk;
	char			pin[MBIM_PIN_MAXLEN];
	int			pinlen;

	char			newpin[MBIM_PIN_MAXLEN];
	int			newpinlen;

#define UMB_APN_MAXLEN		100
	uint16_t		apn[UMB_APN_MAXLEN];
	int			apnlen;

	int			roaming;
	uint32_t		preferredclasses;
};

/*
 * UMB device status info (SIOCGUMBINFO ioctl)
 */
struct umb_info {
	enum umb_state		state;
	int			enable_roaming;
#define UMB_PIN_REQUIRED	0
#define UMB_PIN_UNLOCKED	1
#define UMB_PUK_REQUIRED	2
	int			pin_state;
	int			pin_attempts_left;
	int			activation;
	int			sim_state;
	int			regstate;
	int			regmode;
	int			nwerror;
	int			packetstate;
	uint32_t		supportedclasses; /* what the hw supports */
	uint32_t		preferredclasses; /* what the user prefers */
	uint32_t		highestclass;	/* what the network offers */
	uint32_t		cellclass;
#define UMB_PROVIDERNAME_MAXLEN		20
	uint16_t		provider[UMB_PROVIDERNAME_MAXLEN];
#define UMB_PROVIDERID_MAXLEN		20
	uint16_t		providerid[UMB_PROVIDERID_MAXLEN];
#define UMB_PHONENR_MAXLEN		22
	uint16_t		pn[UMB_PHONENR_MAXLEN];
#define UMB_SUBSCRIBERID_MAXLEN		15
	uint16_t		sid[UMB_SUBSCRIBERID_MAXLEN];
#define UMB_ICCID_MAXLEN		20
	uint16_t		iccid[UMB_ICCID_MAXLEN];
#define UMB_ROAMINGTEXT_MAXLEN		63
	uint16_t		roamingtxt[UMB_ROAMINGTEXT_MAXLEN];

#define UMB_DEVID_MAXLEN		18
	uint16_t		devid[UMB_DEVID_MAXLEN];
#define UMB_FWINFO_MAXLEN		30
	uint16_t		fwinfo[UMB_FWINFO_MAXLEN];
#define UMB_HWINFO_MAXLEN		30
	uint16_t		hwinfo[UMB_HWINFO_MAXLEN];

	uint16_t		apn[UMB_APN_MAXLEN];
	int			apnlen;

#define UMB_VALUE_UNKNOWN	-999
	int			rssi;
#define UMB_BER_EXCELLENT	0
#define UMB_BER_VERYGOOD	1
#define UMB_BER_GOOD		2
#define UMB_BER_OK		3
#define UMB_BER_MEDIUM		4
#define UMB_BER_BAD		5
#define UMB_BER_VERYBAD		6
#define UMB_BER_EXTREMELYBAD	7
	int			ber;

	int			hw_radio_on;
	int			sw_radio_on;

	uint64_t		uplink_speed;
	uint64_t		downlink_speed;

#define UMB_MAX_DNSSRV			2
	struct in_addr		ipv4dns[UMB_MAX_DNSSRV];
	struct in6_addr		ipv6dns[UMB_MAX_DNSSRV];
};

#ifdef _KERNEL
/*
 * UMB device
 */
struct umb_softc {
	struct device		 sc_dev;
	struct ifnet		 sc_if;
#define GET_IFP(sc)	(&(sc)->sc_if)
	struct usbd_device	*sc_udev;

	int			 sc_ver_maj;
	int			 sc_ver_min;
	int			 sc_ctrl_len;
	uint32_t		 sc_maxpktlen;
	int			 sc_maxsessions;
	unsigned int		 sc_ncm_supported_formats;
	int			 sc_ncm_format;

	int			 sc_maxdgram;
	int			 sc_align;
	int			 sc_ndp_div;
	int			 sc_ndp_remainder;

#define UMBFLG_FCC_AUTH_REQUIRED	0x0001
#define UMBFLG_NO_INET6			0x0002
#define UMBFLG_NDP_AT_END		0x0004
	uint32_t		 sc_flags;
	int			 sc_cid;

	struct usb_task		 sc_umb_task;
	struct usb_task		 sc_get_response_task;
	int			 sc_nresp;
	struct timeout		 sc_statechg_timer;

	uint8_t			 sc_ctrl_ifaceno;
	struct usbd_pipe	*sc_ctrl_pipe;
	struct usb_cdc_notification sc_intr_msg;
	struct usbd_interface	*sc_data_iface;

	void			*sc_resp_buf;
	void			*sc_ctrl_msg;

	int			 sc_rx_ep;
	struct usbd_xfer	*sc_rx_xfer;
	void			*sc_rx_buf;
	int			 sc_rx_bufsz;
	struct usbd_pipe	*sc_rx_pipe;
	unsigned		 sc_rx_nerr;

	int			 sc_tx_ep;
	struct usbd_xfer	*sc_tx_xfer;
	void			*sc_tx_buf;
	int			 sc_tx_bufsz;
	struct usbd_pipe	*sc_tx_pipe;
	struct mbuf_list	 sc_tx_ml;
	uint32_t		 sc_tx_seq;

	uint32_t		 sc_tid;

#define sc_state		sc_info.state
#define sc_roaming		sc_info.enable_roaming
	struct umb_info		 sc_info;

	struct rwlock		 sc_kstat_lock;
	struct kstat		*sc_kstat_signal;
};
#endif /* _KERNEL */
