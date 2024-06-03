/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#ifndef __HCI_H
#define __HCI_H

#define HCI_MAX_ACL_SIZE	1024
#define HCI_MAX_SCO_SIZE	255
#define HCI_MAX_ISO_SIZE	251
#define HCI_MAX_EVENT_SIZE	260
#define HCI_MAX_FRAME_SIZE	(HCI_MAX_ACL_SIZE + 4)

#define HCI_LINK_KEY_SIZE	16
#define HCI_AMP_LINK_KEY_SIZE	(2 * HCI_LINK_KEY_SIZE)

#define HCI_MAX_AMP_ASSOC_SIZE	672

#define HCI_MAX_CPB_DATA_SIZE	252

/* HCI dev events */
#define HCI_DEV_REG			1
#define HCI_DEV_UNREG			2
#define HCI_DEV_UP			3
#define HCI_DEV_DOWN			4
#define HCI_DEV_SUSPEND			5
#define HCI_DEV_RESUME			6
#define HCI_DEV_OPEN			7
#define HCI_DEV_CLOSE			8
#define HCI_DEV_SETUP			9

/* HCI notify events */
#define HCI_NOTIFY_CONN_ADD		1
#define HCI_NOTIFY_CONN_DEL		2
#define HCI_NOTIFY_VOICE_SETTING	3
#define HCI_NOTIFY_ENABLE_SCO_CVSD	4
#define HCI_NOTIFY_ENABLE_SCO_TRANSP	5
#define HCI_NOTIFY_DISABLE_SCO		6

/* HCI bus types */
#define HCI_VIRTUAL	0
#define HCI_USB		1
#define HCI_PCCARD	2
#define HCI_UART	3
#define HCI_RS232	4
#define HCI_PCI		5
#define HCI_SDIO	6
#define HCI_SPI		7
#define HCI_I2C		8
#define HCI_SMD		9
#define HCI_VIRTIO	10

/* HCI controller types */
#define HCI_PRIMARY	0x00
#define HCI_AMP		0x01

/* First BR/EDR Controller shall have ID = 0 */
#define AMP_ID_BREDR	0x00

/* AMP controller types */
#define AMP_TYPE_BREDR	0x00
#define AMP_TYPE_80211	0x01

/* AMP controller status */
#define AMP_STATUS_POWERED_DOWN			0x00
#define AMP_STATUS_BLUETOOTH_ONLY		0x01
#define AMP_STATUS_NO_CAPACITY			0x02
#define AMP_STATUS_LOW_CAPACITY			0x03
#define AMP_STATUS_MEDIUM_CAPACITY		0x04
#define AMP_STATUS_HIGH_CAPACITY		0x05
#define AMP_STATUS_FULL_CAPACITY		0x06

/* HCI device quirks */
enum {
	/* When this quirk is set, the HCI Reset command is send when
	 * closing the transport instead of when opening it.
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_RESET_ON_CLOSE,

	/* When this quirk is set, the device is turned into a raw-only
	 * device and it will stay in unconfigured state.
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_RAW_DEVICE,

	/* When this quirk is set, the buffer sizes reported by
	 * HCI Read Buffer Size command are corrected if invalid.
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_FIXUP_BUFFER_SIZE,

	/* When this quirk is set, then a controller that does not
	 * indicate support for Inquiry Result with RSSI is assumed to
	 * support it anyway. Some early Bluetooth 1.2 controllers had
	 * wrongly configured local features that will require forcing
	 * them to enable this mode. Getting RSSI information with the
	 * inquiry responses is preferred since it allows for a better
	 * user experience.
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_FIXUP_INQUIRY_MODE,

	/* When this quirk is set, then the HCI Read Local Supported
	 * Commands command is not supported. In general Bluetooth 1.2
	 * and later controllers should support this command. However
	 * some controllers indicate Bluetooth 1.2 support, but do
	 * not support this command.
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_BROKEN_LOCAL_COMMANDS,

	/* When this quirk is set, then no stored link key handling
	 * is performed. This is mainly due to the fact that the
	 * HCI Delete Stored Link Key command is advertised, but
	 * not supported.
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_BROKEN_STORED_LINK_KEY,

	/* When this quirk is set, an external configuration step
	 * is required and will be indicated with the controller
	 * configuration.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_EXTERNAL_CONFIG,

	/* When this quirk is set, the public Bluetooth address
	 * initially reported by HCI Read BD Address command
	 * is considered invalid. Controller configuration is
	 * required before this device can be used.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_INVALID_BDADDR,

	/* When this quirk is set, the public Bluetooth address
	 * initially reported by HCI Read BD Address command
	 * is considered invalid. The public BD Address can be
	 * specified in the fwnode property 'local-bd-address'.
	 * If this property does not exist or is invalid controller
	 * configuration is required before this device can be used.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_USE_BDADDR_PROPERTY,

	/* When this quirk is set, the Bluetooth Device Address provided by
	 * the 'local-bd-address' fwnode property is incorrectly specified in
	 * big-endian order.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_BDADDR_PROPERTY_BROKEN,

	/* When this quirk is set, the duplicate filtering during
	 * scanning is based on Bluetooth devices addresses. To allow
	 * RSSI based updates, restart scanning if needed.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_STRICT_DUPLICATE_FILTER,

	/* When this quirk is set, LE scan and BR/EDR inquiry is done
	 * simultaneously, otherwise it's interleaved.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_SIMULTANEOUS_DISCOVERY,

	/* When this quirk is set, the enabling of diagnostic mode is
	 * not persistent over HCI Reset. Every time the controller
	 * is brought up it needs to be reprogrammed.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_NON_PERSISTENT_DIAG,

	/* When this quirk is set, setup() would be run after every
	 * open() and not just after the first open().
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 *
	 */
	HCI_QUIRK_NON_PERSISTENT_SETUP,

	/* When this quirk is set, wide band speech is supported by
	 * the driver since no reliable mechanism exist to report
	 * this from the hardware, a driver flag is use to convey
	 * this support
	 *
	 * This quirk must be set before hci_register_dev is called.
	 */
	HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED,

	/* When this quirk is set, the controller has validated that
	 * LE states reported through the HCI_LE_READ_SUPPORTED_STATES are
	 * valid.  This mechanism is necessary as many controllers have
	 * been seen has having trouble initiating a connectable
	 * advertisement despite the state combination being reported as
	 * supported.
	 */
	HCI_QUIRK_VALID_LE_STATES,

	/* When this quirk is set, then erroneous data reporting
	 * is ignored. This is mainly due to the fact that the HCI
	 * Read Default Erroneous Data Reporting command is advertised,
	 * but not supported; these controllers often reply with unknown
	 * command and tend to lock up randomly. Needing a hard reset.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_BROKEN_ERR_DATA_REPORTING,

	/*
	 * When this quirk is set, then the hci_suspend_notifier is not
	 * registered. This is intended for devices which drop completely
	 * from the bus on system-suspend and which will show up as a new
	 * HCI after resume.
	 */
	HCI_QUIRK_NO_SUSPEND_NOTIFIER,

	/*
	 * When this quirk is set, LE tx power is not queried on startup
	 * and the min/max tx power values default to HCI_TX_POWER_INVALID.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_BROKEN_READ_TRANSMIT_POWER,

	/* When this quirk is set, HCI_OP_SET_EVENT_FLT requests with
	 * HCI_FLT_CLEAR_ALL are ignored and event filtering is
	 * completely avoided. A subset of the CSR controller
	 * clones struggle with this and instantly lock up.
	 *
	 * Note that devices using this must (separately) disable
	 * runtime suspend, because event filtering takes place there.
	 */
	HCI_QUIRK_BROKEN_FILTER_CLEAR_ALL,

	/*
	 * When this quirk is set, disables the use of
	 * HCI_OP_ENHANCED_SETUP_SYNC_CONN command to setup SCO connections.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_BROKEN_ENHANCED_SETUP_SYNC_CONN,

	/*
	 * When this quirk is set, the HCI_OP_LE_SET_EXT_SCAN_ENABLE command is
	 * disabled. This is required for some Broadcom controllers which
	 * erroneously claim to support extended scanning.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_BROKEN_EXT_SCAN,

	/*
	 * When this quirk is set, the HCI_OP_GET_MWS_TRANSPORT_CONFIG command is
	 * disabled. This is required for some Broadcom controllers which
	 * erroneously claim to support MWS Transport Layer Configuration.
	 *
	 * This quirk can be set before hci_register_dev is called or
	 * during the hdev->setup vendor callback.
	 */
	HCI_QUIRK_BROKEN_MWS_TRANSPORT_CONFIG,

	/* When this quirk is set, max_page for local extended features
	 * is set to 1, even if controller reports higher number. Some
	 * controllers (e.g. RTL8723CS) report more pages, but they
	 * don't actually support features declared there.
	 */
	HCI_QUIRK_BROKEN_LOCAL_EXT_FEATURES_PAGE_2,

	/*
	 * When this quirk is set, the HCI_OP_LE_SET_RPA_TIMEOUT command is
	 * skipped during initialization. This is required for the Actions
	 * Semiconductor ATS2851 based controllers, which erroneously claims
	 * to support it.
	 */
	HCI_QUIRK_BROKEN_SET_RPA_TIMEOUT,
};

/* HCI device flags */
enum {
	HCI_UP,
	HCI_INIT,
	HCI_RUNNING,

	HCI_PSCAN,
	HCI_ISCAN,
	HCI_AUTH,
	HCI_ENCRYPT,
	HCI_INQUIRY,

	HCI_RAW,

	HCI_RESET,
};

/* HCI socket flags */
enum {
	HCI_SOCK_TRUSTED,
	HCI_MGMT_INDEX_EVENTS,
	HCI_MGMT_UNCONF_INDEX_EVENTS,
	HCI_MGMT_EXT_INDEX_EVENTS,
	HCI_MGMT_EXT_INFO_EVENTS,
	HCI_MGMT_OPTION_EVENTS,
	HCI_MGMT_SETTING_EVENTS,
	HCI_MGMT_DEV_CLASS_EVENTS,
	HCI_MGMT_LOCAL_NAME_EVENTS,
	HCI_MGMT_OOB_DATA_EVENTS,
	HCI_MGMT_EXP_FEATURE_EVENTS,
};

/*
 * BR/EDR and/or LE controller flags: the flags defined here should represent
 * states from the controller.
 */
enum {
	HCI_SETUP,
	HCI_CONFIG,
	HCI_DEBUGFS_CREATED,
	HCI_AUTO_OFF,
	HCI_RFKILLED,
	HCI_MGMT,
	HCI_BONDABLE,
	HCI_SERVICE_CACHE,
	HCI_KEEP_DEBUG_KEYS,
	HCI_USE_DEBUG_KEYS,
	HCI_UNREGISTER,
	HCI_UNCONFIGURED,
	HCI_USER_CHANNEL,
	HCI_EXT_CONFIGURED,
	HCI_LE_ADV,
	HCI_LE_PER_ADV,
	HCI_LE_SCAN,
	HCI_SSP_ENABLED,
	HCI_SC_ENABLED,
	HCI_SC_ONLY,
	HCI_PRIVACY,
	HCI_LIMITED_PRIVACY,
	HCI_RPA_EXPIRED,
	HCI_RPA_RESOLVING,
	HCI_HS_ENABLED,
	HCI_LE_ENABLED,
	HCI_ADVERTISING,
	HCI_ADVERTISING_CONNECTABLE,
	HCI_CONNECTABLE,
	HCI_DISCOVERABLE,
	HCI_LIMITED_DISCOVERABLE,
	HCI_LINK_SECURITY,
	HCI_PERIODIC_INQ,
	HCI_FAST_CONNECTABLE,
	HCI_BREDR_ENABLED,
	HCI_LE_SCAN_INTERRUPTED,
	HCI_WIDEBAND_SPEECH_ENABLED,
	HCI_EVENT_FILTER_CONFIGURED,
	HCI_PA_SYNC,

	HCI_DUT_MODE,
	HCI_VENDOR_DIAG,
	HCI_FORCE_BREDR_SMP,
	HCI_FORCE_STATIC_ADDR,
	HCI_LL_RPA_RESOLUTION,
	HCI_ENABLE_LL_PRIVACY,
	HCI_CMD_PENDING,
	HCI_FORCE_NO_MITM,
	HCI_QUALITY_REPORT,
	HCI_OFFLOAD_CODECS_ENABLED,
	HCI_LE_SIMULTANEOUS_ROLES,
	HCI_CMD_DRAIN_WORKQUEUE,

	HCI_MESH_EXPERIMENTAL,
	HCI_MESH,
	HCI_MESH_SENDING,

	__HCI_NUM_FLAGS,
};

/* HCI timeouts */
#define HCI_DISCONN_TIMEOUT	msecs_to_jiffies(2000)	/* 2 seconds */
#define HCI_PAIRING_TIMEOUT	msecs_to_jiffies(60000)	/* 60 seconds */
#define HCI_INIT_TIMEOUT	msecs_to_jiffies(10000)	/* 10 seconds */
#define HCI_CMD_TIMEOUT		msecs_to_jiffies(2000)	/* 2 seconds */
#define HCI_NCMD_TIMEOUT	msecs_to_jiffies(4000)	/* 4 seconds */
#define HCI_ACL_TX_TIMEOUT	msecs_to_jiffies(45000)	/* 45 seconds */
#define HCI_AUTO_OFF_TIMEOUT	msecs_to_jiffies(2000)	/* 2 seconds */
#define HCI_LE_CONN_TIMEOUT	msecs_to_jiffies(20000)	/* 20 seconds */
#define HCI_LE_AUTOCONN_TIMEOUT	msecs_to_jiffies(4000)	/* 4 seconds */

/* HCI data types */
#define HCI_COMMAND_PKT		0x01
#define HCI_ACLDATA_PKT		0x02
#define HCI_SCODATA_PKT		0x03
#define HCI_EVENT_PKT		0x04
#define HCI_ISODATA_PKT		0x05
#define HCI_DIAG_PKT		0xf0
#define HCI_VENDOR_PKT		0xff

/* HCI packet types */
#define HCI_DM1		0x0008
#define HCI_DM3		0x0400
#define HCI_DM5		0x4000
#define HCI_DH1		0x0010
#define HCI_DH3		0x0800
#define HCI_DH5		0x8000

/* HCI packet types inverted masks */
#define HCI_2DH1	0x0002
#define HCI_3DH1	0x0004
#define HCI_2DH3	0x0100
#define HCI_3DH3	0x0200
#define HCI_2DH5	0x1000
#define HCI_3DH5	0x2000

#define HCI_HV1		0x0020
#define HCI_HV2		0x0040
#define HCI_HV3		0x0080

#define SCO_PTYPE_MASK	(HCI_HV1 | HCI_HV2 | HCI_HV3)
#define ACL_PTYPE_MASK	(~SCO_PTYPE_MASK)

/* eSCO packet types */
#define ESCO_HV1	0x0001
#define ESCO_HV2	0x0002
#define ESCO_HV3	0x0004
#define ESCO_EV3	0x0008
#define ESCO_EV4	0x0010
#define ESCO_EV5	0x0020
#define ESCO_2EV3	0x0040
#define ESCO_3EV3	0x0080
#define ESCO_2EV5	0x0100
#define ESCO_3EV5	0x0200

#define SCO_ESCO_MASK  (ESCO_HV1 | ESCO_HV2 | ESCO_HV3)
#define EDR_ESCO_MASK  (ESCO_2EV3 | ESCO_3EV3 | ESCO_2EV5 | ESCO_3EV5)

/* ACL flags */
#define ACL_START_NO_FLUSH	0x00
#define ACL_CONT		0x01
#define ACL_START		0x02
#define ACL_COMPLETE		0x03
#define ACL_ACTIVE_BCAST	0x04
#define ACL_PICO_BCAST		0x08

/* ISO PB flags */
#define ISO_START		0x00
#define ISO_CONT		0x01
#define ISO_SINGLE		0x02
#define ISO_END			0x03

/* ISO TS flags */
#define ISO_TS			0x01

/* Baseband links */
#define SCO_LINK	0x00
#define ACL_LINK	0x01
#define ESCO_LINK	0x02
/* Low Energy links do not have defined link type. Use invented one */
#define LE_LINK		0x80
#define AMP_LINK	0x81
#define ISO_LINK	0x82
#define INVALID_LINK	0xff

/* LMP features */
#define LMP_3SLOT	0x01
#define LMP_5SLOT	0x02
#define LMP_ENCRYPT	0x04
#define LMP_SOFFSET	0x08
#define LMP_TACCURACY	0x10
#define LMP_RSWITCH	0x20
#define LMP_HOLD	0x40
#define LMP_SNIFF	0x80

#define LMP_PARK	0x01
#define LMP_RSSI	0x02
#define LMP_QUALITY	0x04
#define LMP_SCO		0x08
#define LMP_HV2		0x10
#define LMP_HV3		0x20
#define LMP_ULAW	0x40
#define LMP_ALAW	0x80

#define LMP_CVSD	0x01
#define LMP_PSCHEME	0x02
#define LMP_PCONTROL	0x04
#define LMP_TRANSPARENT	0x08

#define LMP_EDR_2M		0x02
#define LMP_EDR_3M		0x04
#define LMP_RSSI_INQ	0x40
#define LMP_ESCO	0x80

#define LMP_EV4		0x01
#define LMP_EV5		0x02
#define LMP_NO_BREDR	0x20
#define LMP_LE		0x40
#define LMP_EDR_3SLOT	0x80

#define LMP_EDR_5SLOT	0x01
#define LMP_SNIFF_SUBR	0x02
#define LMP_PAUSE_ENC	0x04
#define LMP_EDR_ESCO_2M	0x20
#define LMP_EDR_ESCO_3M	0x40
#define LMP_EDR_3S_ESCO	0x80

#define LMP_EXT_INQ	0x01
#define LMP_SIMUL_LE_BR	0x02
#define LMP_SIMPLE_PAIR	0x08
#define LMP_ERR_DATA_REPORTING 0x20
#define LMP_NO_FLUSH	0x40

#define LMP_LSTO	0x01
#define LMP_INQ_TX_PWR	0x02
#define LMP_EXTFEATURES	0x80

/* Extended LMP features */
#define LMP_CPB_CENTRAL		0x01
#define LMP_CPB_PERIPHERAL	0x02
#define LMP_SYNC_TRAIN		0x04
#define LMP_SYNC_SCAN		0x08

#define LMP_SC		0x01
#define LMP_PING	0x02

/* Host features */
#define LMP_HOST_SSP		0x01
#define LMP_HOST_LE		0x02
#define LMP_HOST_LE_BREDR	0x04
#define LMP_HOST_SC		0x08

/* LE features */
#define HCI_LE_ENCRYPTION		0x01
#define HCI_LE_CONN_PARAM_REQ_PROC	0x02
#define HCI_LE_PERIPHERAL_FEATURES	0x08
#define HCI_LE_PING			0x10
#define HCI_LE_DATA_LEN_EXT		0x20
#define HCI_LE_LL_PRIVACY		0x40
#define HCI_LE_EXT_SCAN_POLICY		0x80
#define HCI_LE_PHY_2M			0x01
#define HCI_LE_PHY_CODED		0x08
#define HCI_LE_EXT_ADV			0x10
#define HCI_LE_PERIODIC_ADV		0x20
#define HCI_LE_CHAN_SEL_ALG2		0x40
#define HCI_LE_CIS_CENTRAL		0x10
#define HCI_LE_CIS_PERIPHERAL		0x20
#define HCI_LE_ISO_BROADCASTER		0x40

/* Connection modes */
#define HCI_CM_ACTIVE	0x0000
#define HCI_CM_HOLD	0x0001
#define HCI_CM_SNIFF	0x0002
#define HCI_CM_PARK	0x0003

/* Link policies */
#define HCI_LP_RSWITCH	0x0001
#define HCI_LP_HOLD	0x0002
#define HCI_LP_SNIFF	0x0004
#define HCI_LP_PARK	0x0008

/* Link modes */
#define HCI_LM_ACCEPT	0x8000
#define HCI_LM_MASTER	0x0001
#define HCI_LM_AUTH	0x0002
#define HCI_LM_ENCRYPT	0x0004
#define HCI_LM_TRUSTED	0x0008
#define HCI_LM_RELIABLE	0x0010
#define HCI_LM_SECURE	0x0020
#define HCI_LM_FIPS	0x0040

/* Authentication types */
#define HCI_AT_NO_BONDING		0x00
#define HCI_AT_NO_BONDING_MITM		0x01
#define HCI_AT_DEDICATED_BONDING	0x02
#define HCI_AT_DEDICATED_BONDING_MITM	0x03
#define HCI_AT_GENERAL_BONDING		0x04
#define HCI_AT_GENERAL_BONDING_MITM	0x05

/* I/O capabilities */
#define HCI_IO_DISPLAY_ONLY	0x00
#define HCI_IO_DISPLAY_YESNO	0x01
#define HCI_IO_KEYBOARD_ONLY	0x02
#define HCI_IO_NO_INPUT_OUTPUT	0x03

/* Link Key types */
#define HCI_LK_COMBINATION		0x00
#define HCI_LK_LOCAL_UNIT		0x01
#define HCI_LK_REMOTE_UNIT		0x02
#define HCI_LK_DEBUG_COMBINATION	0x03
#define HCI_LK_UNAUTH_COMBINATION_P192	0x04
#define HCI_LK_AUTH_COMBINATION_P192	0x05
#define HCI_LK_CHANGED_COMBINATION	0x06
#define HCI_LK_UNAUTH_COMBINATION_P256	0x07
#define HCI_LK_AUTH_COMBINATION_P256	0x08

/* ---- HCI Error Codes ---- */
#define HCI_ERROR_UNKNOWN_CONN_ID	0x02
#define HCI_ERROR_AUTH_FAILURE		0x05
#define HCI_ERROR_PIN_OR_KEY_MISSING	0x06
#define HCI_ERROR_MEMORY_EXCEEDED	0x07
#define HCI_ERROR_CONNECTION_TIMEOUT	0x08
#define HCI_ERROR_REJ_LIMITED_RESOURCES	0x0d
#define HCI_ERROR_REJ_BAD_ADDR		0x0f
#define HCI_ERROR_INVALID_PARAMETERS	0x12
#define HCI_ERROR_REMOTE_USER_TERM	0x13
#define HCI_ERROR_REMOTE_LOW_RESOURCES	0x14
#define HCI_ERROR_REMOTE_POWER_OFF	0x15
#define HCI_ERROR_LOCAL_HOST_TERM	0x16
#define HCI_ERROR_PAIRING_NOT_ALLOWED	0x18
#define HCI_ERROR_INVALID_LL_PARAMS	0x1e
#define HCI_ERROR_UNSPECIFIED		0x1f
#define HCI_ERROR_ADVERTISING_TIMEOUT	0x3c
#define HCI_ERROR_CANCELLED_BY_HOST	0x44

/* Flow control modes */
#define HCI_FLOW_CTL_MODE_PACKET_BASED	0x00
#define HCI_FLOW_CTL_MODE_BLOCK_BASED	0x01

/* The core spec defines 127 as the "not available" value */
#define HCI_TX_POWER_INVALID	127
#define HCI_RSSI_INVALID	127

#define HCI_ROLE_MASTER		0x00
#define HCI_ROLE_SLAVE		0x01

/* Extended Inquiry Response field types */
#define EIR_FLAGS		0x01 /* flags */
#define EIR_UUID16_SOME		0x02 /* 16-bit UUID, more available */
#define EIR_UUID16_ALL		0x03 /* 16-bit UUID, all listed */
#define EIR_UUID32_SOME		0x04 /* 32-bit UUID, more available */
#define EIR_UUID32_ALL		0x05 /* 32-bit UUID, all listed */
#define EIR_UUID128_SOME	0x06 /* 128-bit UUID, more available */
#define EIR_UUID128_ALL		0x07 /* 128-bit UUID, all listed */
#define EIR_NAME_SHORT		0x08 /* shortened local name */
#define EIR_NAME_COMPLETE	0x09 /* complete local name */
#define EIR_TX_POWER		0x0A /* transmit power level */
#define EIR_CLASS_OF_DEV	0x0D /* Class of Device */
#define EIR_SSP_HASH_C192	0x0E /* Simple Pairing Hash C-192 */
#define EIR_SSP_RAND_R192	0x0F /* Simple Pairing Randomizer R-192 */
#define EIR_DEVICE_ID		0x10 /* device ID */
#define EIR_APPEARANCE		0x19 /* Device appearance */
#define EIR_SERVICE_DATA	0x16 /* Service Data */
#define EIR_LE_BDADDR		0x1B /* LE Bluetooth device address */
#define EIR_LE_ROLE		0x1C /* LE role */
#define EIR_SSP_HASH_C256	0x1D /* Simple Pairing Hash C-256 */
#define EIR_SSP_RAND_R256	0x1E /* Simple Pairing Rand R-256 */
#define EIR_LE_SC_CONFIRM	0x22 /* LE SC Confirmation Value */
#define EIR_LE_SC_RANDOM	0x23 /* LE SC Random Value */

/* Low Energy Advertising Flags */
#define LE_AD_LIMITED		0x01 /* Limited Discoverable */
#define LE_AD_GENERAL		0x02 /* General Discoverable */
#define LE_AD_NO_BREDR		0x04 /* BR/EDR not supported */
#define LE_AD_SIM_LE_BREDR_CTRL	0x08 /* Simultaneous LE & BR/EDR Controller */
#define LE_AD_SIM_LE_BREDR_HOST	0x10 /* Simultaneous LE & BR/EDR Host */

/* -----  HCI Commands ---- */
#define HCI_OP_NOP			0x0000

#define HCI_OP_INQUIRY			0x0401
struct hci_cp_inquiry {
	__u8     lap[3];
	__u8     length;
	__u8     num_rsp;
} __packed;

#define HCI_OP_INQUIRY_CANCEL		0x0402

#define HCI_OP_PERIODIC_INQ		0x0403

#define HCI_OP_EXIT_PERIODIC_INQ	0x0404

#define HCI_OP_CREATE_CONN		0x0405
struct hci_cp_create_conn {
	bdaddr_t bdaddr;
	__le16   pkt_type;
	__u8     pscan_rep_mode;
	__u8     pscan_mode;
	__le16   clock_offset;
	__u8     role_switch;
} __packed;

#define HCI_OP_DISCONNECT		0x0406
struct hci_cp_disconnect {
	__le16   handle;
	__u8     reason;
} __packed;

#define HCI_OP_ADD_SCO			0x0407
struct hci_cp_add_sco {
	__le16   handle;
	__le16   pkt_type;
} __packed;

#define HCI_OP_CREATE_CONN_CANCEL	0x0408
struct hci_cp_create_conn_cancel {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_ACCEPT_CONN_REQ		0x0409
struct hci_cp_accept_conn_req {
	bdaddr_t bdaddr;
	__u8     role;
} __packed;

#define HCI_OP_REJECT_CONN_REQ		0x040a
struct hci_cp_reject_conn_req {
	bdaddr_t bdaddr;
	__u8     reason;
} __packed;

#define HCI_OP_LINK_KEY_REPLY		0x040b
struct hci_cp_link_key_reply {
	bdaddr_t bdaddr;
	__u8     link_key[HCI_LINK_KEY_SIZE];
} __packed;

#define HCI_OP_LINK_KEY_NEG_REPLY	0x040c
struct hci_cp_link_key_neg_reply {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_PIN_CODE_REPLY		0x040d
struct hci_cp_pin_code_reply {
	bdaddr_t bdaddr;
	__u8     pin_len;
	__u8     pin_code[16];
} __packed;
struct hci_rp_pin_code_reply {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_PIN_CODE_NEG_REPLY	0x040e
struct hci_cp_pin_code_neg_reply {
	bdaddr_t bdaddr;
} __packed;
struct hci_rp_pin_code_neg_reply {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_CHANGE_CONN_PTYPE	0x040f
struct hci_cp_change_conn_ptype {
	__le16   handle;
	__le16   pkt_type;
} __packed;

#define HCI_OP_AUTH_REQUESTED		0x0411
struct hci_cp_auth_requested {
	__le16   handle;
} __packed;

#define HCI_OP_SET_CONN_ENCRYPT		0x0413
struct hci_cp_set_conn_encrypt {
	__le16   handle;
	__u8     encrypt;
} __packed;

#define HCI_OP_CHANGE_CONN_LINK_KEY	0x0415
struct hci_cp_change_conn_link_key {
	__le16   handle;
} __packed;

#define HCI_OP_REMOTE_NAME_REQ		0x0419
struct hci_cp_remote_name_req {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_mode;
	__le16   clock_offset;
} __packed;

#define HCI_OP_REMOTE_NAME_REQ_CANCEL	0x041a
struct hci_cp_remote_name_req_cancel {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_READ_REMOTE_FEATURES	0x041b
struct hci_cp_read_remote_features {
	__le16   handle;
} __packed;

#define HCI_OP_READ_REMOTE_EXT_FEATURES	0x041c
struct hci_cp_read_remote_ext_features {
	__le16   handle;
	__u8     page;
} __packed;

#define HCI_OP_READ_REMOTE_VERSION	0x041d
struct hci_cp_read_remote_version {
	__le16   handle;
} __packed;

#define HCI_OP_READ_CLOCK_OFFSET	0x041f
struct hci_cp_read_clock_offset {
	__le16   handle;
} __packed;

#define HCI_OP_SETUP_SYNC_CONN		0x0428
struct hci_cp_setup_sync_conn {
	__le16   handle;
	__le32   tx_bandwidth;
	__le32   rx_bandwidth;
	__le16   max_latency;
	__le16   voice_setting;
	__u8     retrans_effort;
	__le16   pkt_type;
} __packed;

#define HCI_OP_ACCEPT_SYNC_CONN_REQ	0x0429
struct hci_cp_accept_sync_conn_req {
	bdaddr_t bdaddr;
	__le32   tx_bandwidth;
	__le32   rx_bandwidth;
	__le16   max_latency;
	__le16   content_format;
	__u8     retrans_effort;
	__le16   pkt_type;
} __packed;

#define HCI_OP_REJECT_SYNC_CONN_REQ	0x042a
struct hci_cp_reject_sync_conn_req {
	bdaddr_t bdaddr;
	__u8     reason;
} __packed;

#define HCI_OP_IO_CAPABILITY_REPLY	0x042b
struct hci_cp_io_capability_reply {
	bdaddr_t bdaddr;
	__u8     capability;
	__u8     oob_data;
	__u8     authentication;
} __packed;

#define HCI_OP_USER_CONFIRM_REPLY		0x042c
struct hci_cp_user_confirm_reply {
	bdaddr_t bdaddr;
} __packed;
struct hci_rp_user_confirm_reply {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_USER_CONFIRM_NEG_REPLY	0x042d

#define HCI_OP_USER_PASSKEY_REPLY		0x042e
struct hci_cp_user_passkey_reply {
	bdaddr_t bdaddr;
	__le32	passkey;
} __packed;

#define HCI_OP_USER_PASSKEY_NEG_REPLY	0x042f

#define HCI_OP_REMOTE_OOB_DATA_REPLY	0x0430
struct hci_cp_remote_oob_data_reply {
	bdaddr_t bdaddr;
	__u8     hash[16];
	__u8     rand[16];
} __packed;

#define HCI_OP_REMOTE_OOB_DATA_NEG_REPLY	0x0433
struct hci_cp_remote_oob_data_neg_reply {
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_IO_CAPABILITY_NEG_REPLY	0x0434
struct hci_cp_io_capability_neg_reply {
	bdaddr_t bdaddr;
	__u8     reason;
} __packed;

#define HCI_OP_CREATE_PHY_LINK		0x0435
struct hci_cp_create_phy_link {
	__u8     phy_handle;
	__u8     key_len;
	__u8     key_type;
	__u8     key[HCI_AMP_LINK_KEY_SIZE];
} __packed;

#define HCI_OP_ACCEPT_PHY_LINK		0x0436
struct hci_cp_accept_phy_link {
	__u8     phy_handle;
	__u8     key_len;
	__u8     key_type;
	__u8     key[HCI_AMP_LINK_KEY_SIZE];
} __packed;

#define HCI_OP_DISCONN_PHY_LINK		0x0437
struct hci_cp_disconn_phy_link {
	__u8     phy_handle;
	__u8     reason;
} __packed;

struct ext_flow_spec {
	__u8       id;
	__u8       stype;
	__le16     msdu;
	__le32     sdu_itime;
	__le32     acc_lat;
	__le32     flush_to;
} __packed;

#define HCI_OP_CREATE_LOGICAL_LINK	0x0438
#define HCI_OP_ACCEPT_LOGICAL_LINK	0x0439
struct hci_cp_create_accept_logical_link {
	__u8                  phy_handle;
	struct ext_flow_spec  tx_flow_spec;
	struct ext_flow_spec  rx_flow_spec;
} __packed;

#define HCI_OP_DISCONN_LOGICAL_LINK	0x043a
struct hci_cp_disconn_logical_link {
	__le16   log_handle;
} __packed;

#define HCI_OP_LOGICAL_LINK_CANCEL	0x043b
struct hci_cp_logical_link_cancel {
	__u8     phy_handle;
	__u8     flow_spec_id;
} __packed;

#define HCI_OP_ENHANCED_SETUP_SYNC_CONN		0x043d
struct hci_coding_format {
	__u8	id;
	__le16	cid;
	__le16	vid;
} __packed;

struct hci_cp_enhanced_setup_sync_conn {
	__le16   handle;
	__le32   tx_bandwidth;
	__le32   rx_bandwidth;
	struct	 hci_coding_format tx_coding_format;
	struct	 hci_coding_format rx_coding_format;
	__le16	 tx_codec_frame_size;
	__le16	 rx_codec_frame_size;
	__le32	 in_bandwidth;
	__le32	 out_bandwidth;
	struct	 hci_coding_format in_coding_format;
	struct	 hci_coding_format out_coding_format;
	__le16   in_coded_data_size;
	__le16	 out_coded_data_size;
	__u8	 in_pcm_data_format;
	__u8	 out_pcm_data_format;
	__u8	 in_pcm_sample_payload_msb_pos;
	__u8	 out_pcm_sample_payload_msb_pos;
	__u8	 in_data_path;
	__u8	 out_data_path;
	__u8	 in_transport_unit_size;
	__u8	 out_transport_unit_size;
	__le16   max_latency;
	__le16   pkt_type;
	__u8     retrans_effort;
} __packed;

struct hci_rp_logical_link_cancel {
	__u8     status;
	__u8     phy_handle;
	__u8     flow_spec_id;
} __packed;

#define HCI_OP_SET_CPB			0x0441
struct hci_cp_set_cpb {
	__u8	enable;
	__u8	lt_addr;
	__u8	lpo_allowed;
	__le16	packet_type;
	__le16	interval_min;
	__le16	interval_max;
	__le16	cpb_sv_tout;
} __packed;
struct hci_rp_set_cpb {
	__u8	status;
	__u8	lt_addr;
	__le16	interval;
} __packed;

#define HCI_OP_START_SYNC_TRAIN		0x0443

#define HCI_OP_REMOTE_OOB_EXT_DATA_REPLY	0x0445
struct hci_cp_remote_oob_ext_data_reply {
	bdaddr_t bdaddr;
	__u8     hash192[16];
	__u8     rand192[16];
	__u8     hash256[16];
	__u8     rand256[16];
} __packed;

#define HCI_OP_SNIFF_MODE		0x0803
struct hci_cp_sniff_mode {
	__le16   handle;
	__le16   max_interval;
	__le16   min_interval;
	__le16   attempt;
	__le16   timeout;
} __packed;

#define HCI_OP_EXIT_SNIFF_MODE		0x0804
struct hci_cp_exit_sniff_mode {
	__le16   handle;
} __packed;

#define HCI_OP_ROLE_DISCOVERY		0x0809
struct hci_cp_role_discovery {
	__le16   handle;
} __packed;
struct hci_rp_role_discovery {
	__u8     status;
	__le16   handle;
	__u8     role;
} __packed;

#define HCI_OP_SWITCH_ROLE		0x080b
struct hci_cp_switch_role {
	bdaddr_t bdaddr;
	__u8     role;
} __packed;

#define HCI_OP_READ_LINK_POLICY		0x080c
struct hci_cp_read_link_policy {
	__le16   handle;
} __packed;
struct hci_rp_read_link_policy {
	__u8     status;
	__le16   handle;
	__le16   policy;
} __packed;

#define HCI_OP_WRITE_LINK_POLICY	0x080d
struct hci_cp_write_link_policy {
	__le16   handle;
	__le16   policy;
} __packed;
struct hci_rp_write_link_policy {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_OP_READ_DEF_LINK_POLICY	0x080e
struct hci_rp_read_def_link_policy {
	__u8     status;
	__le16   policy;
} __packed;

#define HCI_OP_WRITE_DEF_LINK_POLICY	0x080f
struct hci_cp_write_def_link_policy {
	__le16   policy;
} __packed;

#define HCI_OP_SNIFF_SUBRATE		0x0811
struct hci_cp_sniff_subrate {
	__le16   handle;
	__le16   max_latency;
	__le16   min_remote_timeout;
	__le16   min_local_timeout;
} __packed;

#define HCI_OP_SET_EVENT_MASK		0x0c01

#define HCI_OP_RESET			0x0c03

#define HCI_OP_SET_EVENT_FLT		0x0c05
#define HCI_SET_EVENT_FLT_SIZE		9
struct hci_cp_set_event_filter {
	__u8		flt_type;
	__u8		cond_type;
	struct {
		bdaddr_t bdaddr;
		__u8 auto_accept;
	} __packed	addr_conn_flt;
} __packed;

/* Filter types */
#define HCI_FLT_CLEAR_ALL	0x00
#define HCI_FLT_INQ_RESULT	0x01
#define HCI_FLT_CONN_SETUP	0x02

/* CONN_SETUP Condition types */
#define HCI_CONN_SETUP_ALLOW_ALL	0x00
#define HCI_CONN_SETUP_ALLOW_CLASS	0x01
#define HCI_CONN_SETUP_ALLOW_BDADDR	0x02

/* CONN_SETUP Conditions */
#define HCI_CONN_SETUP_AUTO_OFF		0x01
#define HCI_CONN_SETUP_AUTO_ON		0x02
#define HCI_CONN_SETUP_AUTO_ON_WITH_RS	0x03

#define HCI_OP_READ_STORED_LINK_KEY	0x0c0d
struct hci_cp_read_stored_link_key {
	bdaddr_t bdaddr;
	__u8     read_all;
} __packed;
struct hci_rp_read_stored_link_key {
	__u8     status;
	__le16   max_keys;
	__le16   num_keys;
} __packed;

#define HCI_OP_DELETE_STORED_LINK_KEY	0x0c12
struct hci_cp_delete_stored_link_key {
	bdaddr_t bdaddr;
	__u8     delete_all;
} __packed;
struct hci_rp_delete_stored_link_key {
	__u8     status;
	__le16   num_keys;
} __packed;

#define HCI_MAX_NAME_LENGTH		248

#define HCI_OP_WRITE_LOCAL_NAME		0x0c13
struct hci_cp_write_local_name {
	__u8     name[HCI_MAX_NAME_LENGTH];
} __packed;

#define HCI_OP_READ_LOCAL_NAME		0x0c14
struct hci_rp_read_local_name {
	__u8     status;
	__u8     name[HCI_MAX_NAME_LENGTH];
} __packed;

#define HCI_OP_WRITE_CA_TIMEOUT		0x0c16

#define HCI_OP_WRITE_PG_TIMEOUT		0x0c18

#define HCI_OP_WRITE_SCAN_ENABLE	0x0c1a
	#define SCAN_DISABLED		0x00
	#define SCAN_INQUIRY		0x01
	#define SCAN_PAGE		0x02

#define HCI_OP_READ_AUTH_ENABLE		0x0c1f

#define HCI_OP_WRITE_AUTH_ENABLE	0x0c20
	#define AUTH_DISABLED		0x00
	#define AUTH_ENABLED		0x01

#define HCI_OP_READ_ENCRYPT_MODE	0x0c21

#define HCI_OP_WRITE_ENCRYPT_MODE	0x0c22
	#define ENCRYPT_DISABLED	0x00
	#define ENCRYPT_P2P		0x01
	#define ENCRYPT_BOTH		0x02

#define HCI_OP_READ_CLASS_OF_DEV	0x0c23
struct hci_rp_read_class_of_dev {
	__u8     status;
	__u8     dev_class[3];
} __packed;

#define HCI_OP_WRITE_CLASS_OF_DEV	0x0c24
struct hci_cp_write_class_of_dev {
	__u8     dev_class[3];
} __packed;

#define HCI_OP_READ_VOICE_SETTING	0x0c25
struct hci_rp_read_voice_setting {
	__u8     status;
	__le16   voice_setting;
} __packed;

#define HCI_OP_WRITE_VOICE_SETTING	0x0c26
struct hci_cp_write_voice_setting {
	__le16   voice_setting;
} __packed;

#define HCI_OP_HOST_BUFFER_SIZE		0x0c33
struct hci_cp_host_buffer_size {
	__le16   acl_mtu;
	__u8     sco_mtu;
	__le16   acl_max_pkt;
	__le16   sco_max_pkt;
} __packed;

#define HCI_OP_READ_NUM_SUPPORTED_IAC	0x0c38
struct hci_rp_read_num_supported_iac {
	__u8	status;
	__u8	num_iac;
} __packed;

#define HCI_OP_READ_CURRENT_IAC_LAP	0x0c39

#define HCI_OP_WRITE_CURRENT_IAC_LAP	0x0c3a
struct hci_cp_write_current_iac_lap {
	__u8	num_iac;
	__u8	iac_lap[6];
} __packed;

#define HCI_OP_WRITE_INQUIRY_MODE	0x0c45

#define HCI_MAX_EIR_LENGTH		240

#define HCI_OP_WRITE_EIR		0x0c52
struct hci_cp_write_eir {
	__u8	fec;
	__u8	data[HCI_MAX_EIR_LENGTH];
} __packed;

#define HCI_OP_READ_SSP_MODE		0x0c55
struct hci_rp_read_ssp_mode {
	__u8     status;
	__u8     mode;
} __packed;

#define HCI_OP_WRITE_SSP_MODE		0x0c56
struct hci_cp_write_ssp_mode {
	__u8     mode;
} __packed;

#define HCI_OP_READ_LOCAL_OOB_DATA		0x0c57
struct hci_rp_read_local_oob_data {
	__u8     status;
	__u8     hash[16];
	__u8     rand[16];
} __packed;

#define HCI_OP_READ_INQ_RSP_TX_POWER	0x0c58
struct hci_rp_read_inq_rsp_tx_power {
	__u8     status;
	__s8     tx_power;
} __packed;

#define HCI_OP_READ_DEF_ERR_DATA_REPORTING	0x0c5a
	#define ERR_DATA_REPORTING_DISABLED	0x00
	#define ERR_DATA_REPORTING_ENABLED	0x01
struct hci_rp_read_def_err_data_reporting {
	__u8     status;
	__u8     err_data_reporting;
} __packed;

#define HCI_OP_WRITE_DEF_ERR_DATA_REPORTING	0x0c5b
struct hci_cp_write_def_err_data_reporting {
	__u8     err_data_reporting;
} __packed;

#define HCI_OP_SET_EVENT_MASK_PAGE_2	0x0c63

#define HCI_OP_READ_LOCATION_DATA	0x0c64

#define HCI_OP_READ_FLOW_CONTROL_MODE	0x0c66
struct hci_rp_read_flow_control_mode {
	__u8     status;
	__u8     mode;
} __packed;

#define HCI_OP_WRITE_LE_HOST_SUPPORTED	0x0c6d
struct hci_cp_write_le_host_supported {
	__u8	le;
	__u8	simul;
} __packed;

#define HCI_OP_SET_RESERVED_LT_ADDR	0x0c74
struct hci_cp_set_reserved_lt_addr {
	__u8	lt_addr;
} __packed;
struct hci_rp_set_reserved_lt_addr {
	__u8	status;
	__u8	lt_addr;
} __packed;

#define HCI_OP_DELETE_RESERVED_LT_ADDR	0x0c75
struct hci_cp_delete_reserved_lt_addr {
	__u8	lt_addr;
} __packed;
struct hci_rp_delete_reserved_lt_addr {
	__u8	status;
	__u8	lt_addr;
} __packed;

#define HCI_OP_SET_CPB_DATA		0x0c76
struct hci_cp_set_cpb_data {
	__u8	lt_addr;
	__u8	fragment;
	__u8	data_length;
	__u8	data[HCI_MAX_CPB_DATA_SIZE];
} __packed;
struct hci_rp_set_cpb_data {
	__u8	status;
	__u8	lt_addr;
} __packed;

#define HCI_OP_READ_SYNC_TRAIN_PARAMS	0x0c77

#define HCI_OP_WRITE_SYNC_TRAIN_PARAMS	0x0c78
struct hci_cp_write_sync_train_params {
	__le16	interval_min;
	__le16	interval_max;
	__le32	sync_train_tout;
	__u8	service_data;
} __packed;
struct hci_rp_write_sync_train_params {
	__u8	status;
	__le16	sync_train_int;
} __packed;

#define HCI_OP_READ_SC_SUPPORT		0x0c79
struct hci_rp_read_sc_support {
	__u8	status;
	__u8	support;
} __packed;

#define HCI_OP_WRITE_SC_SUPPORT		0x0c7a
struct hci_cp_write_sc_support {
	__u8	support;
} __packed;

#define HCI_OP_READ_AUTH_PAYLOAD_TO    0x0c7b
struct hci_cp_read_auth_payload_to {
	__le16  handle;
} __packed;
struct hci_rp_read_auth_payload_to {
	__u8    status;
	__le16  handle;
	__le16  timeout;
} __packed;

#define HCI_OP_WRITE_AUTH_PAYLOAD_TO    0x0c7c
struct hci_cp_write_auth_payload_to {
	__le16  handle;
	__le16  timeout;
} __packed;
struct hci_rp_write_auth_payload_to {
	__u8    status;
	__le16  handle;
} __packed;

#define HCI_OP_READ_LOCAL_OOB_EXT_DATA	0x0c7d
struct hci_rp_read_local_oob_ext_data {
	__u8     status;
	__u8     hash192[16];
	__u8     rand192[16];
	__u8     hash256[16];
	__u8     rand256[16];
} __packed;

#define HCI_CONFIGURE_DATA_PATH	0x0c83
struct hci_op_configure_data_path {
	__u8	direction;
	__u8	data_path_id;
	__u8	vnd_len;
	__u8	vnd_data[];
} __packed;

#define HCI_OP_READ_LOCAL_VERSION	0x1001
struct hci_rp_read_local_version {
	__u8     status;
	__u8     hci_ver;
	__le16   hci_rev;
	__u8     lmp_ver;
	__le16   manufacturer;
	__le16   lmp_subver;
} __packed;

#define HCI_OP_READ_LOCAL_COMMANDS	0x1002
struct hci_rp_read_local_commands {
	__u8     status;
	__u8     commands[64];
} __packed;

#define HCI_OP_READ_LOCAL_FEATURES	0x1003
struct hci_rp_read_local_features {
	__u8     status;
	__u8     features[8];
} __packed;

#define HCI_OP_READ_LOCAL_EXT_FEATURES	0x1004
struct hci_cp_read_local_ext_features {
	__u8     page;
} __packed;
struct hci_rp_read_local_ext_features {
	__u8     status;
	__u8     page;
	__u8     max_page;
	__u8     features[8];
} __packed;

#define HCI_OP_READ_BUFFER_SIZE		0x1005
struct hci_rp_read_buffer_size {
	__u8     status;
	__le16   acl_mtu;
	__u8     sco_mtu;
	__le16   acl_max_pkt;
	__le16   sco_max_pkt;
} __packed;

#define HCI_OP_READ_BD_ADDR		0x1009
struct hci_rp_read_bd_addr {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_READ_DATA_BLOCK_SIZE	0x100a
struct hci_rp_read_data_block_size {
	__u8     status;
	__le16   max_acl_len;
	__le16   block_len;
	__le16   num_blocks;
} __packed;

#define HCI_OP_READ_LOCAL_CODECS	0x100b
struct hci_std_codecs {
	__u8	num;
	__u8	codec[];
} __packed;

struct hci_vnd_codec {
	/* company id */
	__le16	cid;
	/* vendor codec id */
	__le16	vid;
} __packed;

struct hci_vnd_codecs {
	__u8	num;
	struct hci_vnd_codec codec[];
} __packed;

struct hci_rp_read_local_supported_codecs {
	__u8	status;
	struct hci_std_codecs std_codecs;
	struct hci_vnd_codecs vnd_codecs;
} __packed;

#define HCI_OP_READ_LOCAL_PAIRING_OPTS	0x100c
struct hci_rp_read_local_pairing_opts {
	__u8     status;
	__u8     pairing_opts;
	__u8     max_key_size;
} __packed;

#define HCI_OP_READ_LOCAL_CODECS_V2	0x100d
struct hci_std_codec_v2 {
	__u8	id;
	__u8	transport;
} __packed;

struct hci_std_codecs_v2 {
	__u8	num;
	struct hci_std_codec_v2 codec[];
} __packed;

struct hci_vnd_codec_v2 {
	__le16	cid;
	__le16	vid;
	__u8	transport;
} __packed;

struct hci_vnd_codecs_v2 {
	__u8	num;
	struct hci_vnd_codec_v2 codec[];
} __packed;

struct hci_rp_read_local_supported_codecs_v2 {
	__u8	status;
	struct hci_std_codecs_v2 std_codecs;
	struct hci_vnd_codecs_v2 vendor_codecs;
} __packed;

#define HCI_OP_READ_LOCAL_CODEC_CAPS	0x100e
struct hci_op_read_local_codec_caps {
	__u8	id;
	__le16	cid;
	__le16	vid;
	__u8	transport;
	__u8	direction;
} __packed;

struct hci_codec_caps {
	__u8	len;
	__u8	data[];
} __packed;

struct hci_rp_read_local_codec_caps {
	__u8	status;
	__u8	num_caps;
} __packed;

#define HCI_OP_READ_PAGE_SCAN_ACTIVITY	0x0c1b
struct hci_rp_read_page_scan_activity {
	__u8     status;
	__le16   interval;
	__le16   window;
} __packed;

#define HCI_OP_WRITE_PAGE_SCAN_ACTIVITY	0x0c1c
struct hci_cp_write_page_scan_activity {
	__le16   interval;
	__le16   window;
} __packed;

#define HCI_OP_READ_TX_POWER		0x0c2d
struct hci_cp_read_tx_power {
	__le16   handle;
	__u8     type;
} __packed;
struct hci_rp_read_tx_power {
	__u8     status;
	__le16   handle;
	__s8     tx_power;
} __packed;

#define HCI_OP_READ_PAGE_SCAN_TYPE	0x0c46
struct hci_rp_read_page_scan_type {
	__u8     status;
	__u8     type;
} __packed;

#define HCI_OP_WRITE_PAGE_SCAN_TYPE	0x0c47
	#define PAGE_SCAN_TYPE_STANDARD		0x00
	#define PAGE_SCAN_TYPE_INTERLACED	0x01

#define HCI_OP_READ_RSSI		0x1405
struct hci_cp_read_rssi {
	__le16   handle;
} __packed;
struct hci_rp_read_rssi {
	__u8     status;
	__le16   handle;
	__s8     rssi;
} __packed;

#define HCI_OP_READ_CLOCK		0x1407
struct hci_cp_read_clock {
	__le16   handle;
	__u8     which;
} __packed;
struct hci_rp_read_clock {
	__u8     status;
	__le16   handle;
	__le32   clock;
	__le16   accuracy;
} __packed;

#define HCI_OP_READ_ENC_KEY_SIZE	0x1408
struct hci_cp_read_enc_key_size {
	__le16   handle;
} __packed;
struct hci_rp_read_enc_key_size {
	__u8     status;
	__le16   handle;
	__u8     key_size;
} __packed;

#define HCI_OP_READ_LOCAL_AMP_INFO	0x1409
struct hci_rp_read_local_amp_info {
	__u8     status;
	__u8     amp_status;
	__le32   total_bw;
	__le32   max_bw;
	__le32   min_latency;
	__le32   max_pdu;
	__u8     amp_type;
	__le16   pal_cap;
	__le16   max_assoc_size;
	__le32   max_flush_to;
	__le32   be_flush_to;
} __packed;

#define HCI_OP_READ_LOCAL_AMP_ASSOC	0x140a
struct hci_cp_read_local_amp_assoc {
	__u8     phy_handle;
	__le16   len_so_far;
	__le16   max_len;
} __packed;
struct hci_rp_read_local_amp_assoc {
	__u8     status;
	__u8     phy_handle;
	__le16   rem_len;
	__u8     frag[];
} __packed;

#define HCI_OP_WRITE_REMOTE_AMP_ASSOC	0x140b
struct hci_cp_write_remote_amp_assoc {
	__u8     phy_handle;
	__le16   len_so_far;
	__le16   rem_len;
	__u8     frag[];
} __packed;
struct hci_rp_write_remote_amp_assoc {
	__u8     status;
	__u8     phy_handle;
} __packed;

#define HCI_OP_GET_MWS_TRANSPORT_CONFIG	0x140c

#define HCI_OP_ENABLE_DUT_MODE		0x1803

#define HCI_OP_WRITE_SSP_DEBUG_MODE	0x1804

#define HCI_OP_LE_SET_EVENT_MASK	0x2001
struct hci_cp_le_set_event_mask {
	__u8     mask[8];
} __packed;

#define HCI_OP_LE_READ_BUFFER_SIZE	0x2002
struct hci_rp_le_read_buffer_size {
	__u8     status;
	__le16   le_mtu;
	__u8     le_max_pkt;
} __packed;

#define HCI_OP_LE_READ_LOCAL_FEATURES	0x2003
struct hci_rp_le_read_local_features {
	__u8     status;
	__u8     features[8];
} __packed;

#define HCI_OP_LE_SET_RANDOM_ADDR	0x2005

#define HCI_OP_LE_SET_ADV_PARAM		0x2006
struct hci_cp_le_set_adv_param {
	__le16   min_interval;
	__le16   max_interval;
	__u8     type;
	__u8     own_address_type;
	__u8     direct_addr_type;
	bdaddr_t direct_addr;
	__u8     channel_map;
	__u8     filter_policy;
} __packed;

#define HCI_OP_LE_READ_ADV_TX_POWER	0x2007
struct hci_rp_le_read_adv_tx_power {
	__u8	status;
	__s8	tx_power;
} __packed;

#define HCI_MAX_AD_LENGTH		31

#define HCI_OP_LE_SET_ADV_DATA		0x2008
struct hci_cp_le_set_adv_data {
	__u8	length;
	__u8	data[HCI_MAX_AD_LENGTH];
} __packed;

#define HCI_OP_LE_SET_SCAN_RSP_DATA	0x2009
struct hci_cp_le_set_scan_rsp_data {
	__u8	length;
	__u8	data[HCI_MAX_AD_LENGTH];
} __packed;

#define HCI_OP_LE_SET_ADV_ENABLE	0x200a

#define LE_SCAN_PASSIVE			0x00
#define LE_SCAN_ACTIVE			0x01

#define HCI_OP_LE_SET_SCAN_PARAM	0x200b
struct hci_cp_le_set_scan_param {
	__u8    type;
	__le16  interval;
	__le16  window;
	__u8    own_address_type;
	__u8    filter_policy;
} __packed;

#define LE_SCAN_DISABLE			0x00
#define LE_SCAN_ENABLE			0x01
#define LE_SCAN_FILTER_DUP_DISABLE	0x00
#define LE_SCAN_FILTER_DUP_ENABLE	0x01

#define HCI_OP_LE_SET_SCAN_ENABLE	0x200c
struct hci_cp_le_set_scan_enable {
	__u8     enable;
	__u8     filter_dup;
} __packed;

#define HCI_LE_USE_PEER_ADDR		0x00
#define HCI_LE_USE_ACCEPT_LIST		0x01

#define HCI_OP_LE_CREATE_CONN		0x200d
struct hci_cp_le_create_conn {
	__le16   scan_interval;
	__le16   scan_window;
	__u8     filter_policy;
	__u8     peer_addr_type;
	bdaddr_t peer_addr;
	__u8     own_address_type;
	__le16   conn_interval_min;
	__le16   conn_interval_max;
	__le16   conn_latency;
	__le16   supervision_timeout;
	__le16   min_ce_len;
	__le16   max_ce_len;
} __packed;

#define HCI_OP_LE_CREATE_CONN_CANCEL	0x200e

#define HCI_OP_LE_READ_ACCEPT_LIST_SIZE	0x200f
struct hci_rp_le_read_accept_list_size {
	__u8	status;
	__u8	size;
} __packed;

#define HCI_OP_LE_CLEAR_ACCEPT_LIST	0x2010

#define HCI_OP_LE_ADD_TO_ACCEPT_LIST	0x2011
struct hci_cp_le_add_to_accept_list {
	__u8     bdaddr_type;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_LE_DEL_FROM_ACCEPT_LIST	0x2012
struct hci_cp_le_del_from_accept_list {
	__u8     bdaddr_type;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_LE_CONN_UPDATE		0x2013
struct hci_cp_le_conn_update {
	__le16   handle;
	__le16   conn_interval_min;
	__le16   conn_interval_max;
	__le16   conn_latency;
	__le16   supervision_timeout;
	__le16   min_ce_len;
	__le16   max_ce_len;
} __packed;

#define HCI_OP_LE_READ_REMOTE_FEATURES	0x2016
struct hci_cp_le_read_remote_features {
	__le16	 handle;
} __packed;

#define HCI_OP_LE_START_ENC		0x2019
struct hci_cp_le_start_enc {
	__le16	handle;
	__le64	rand;
	__le16	ediv;
	__u8	ltk[16];
} __packed;

#define HCI_OP_LE_LTK_REPLY		0x201a
struct hci_cp_le_ltk_reply {
	__le16	handle;
	__u8	ltk[16];
} __packed;
struct hci_rp_le_ltk_reply {
	__u8	status;
	__le16	handle;
} __packed;

#define HCI_OP_LE_LTK_NEG_REPLY		0x201b
struct hci_cp_le_ltk_neg_reply {
	__le16	handle;
} __packed;
struct hci_rp_le_ltk_neg_reply {
	__u8	status;
	__le16	handle;
} __packed;

#define HCI_OP_LE_READ_SUPPORTED_STATES	0x201c
struct hci_rp_le_read_supported_states {
	__u8	status;
	__u8	le_states[8];
} __packed;

#define HCI_OP_LE_CONN_PARAM_REQ_REPLY	0x2020
struct hci_cp_le_conn_param_req_reply {
	__le16	handle;
	__le16	interval_min;
	__le16	interval_max;
	__le16	latency;
	__le16	timeout;
	__le16	min_ce_len;
	__le16	max_ce_len;
} __packed;

#define HCI_OP_LE_CONN_PARAM_REQ_NEG_REPLY	0x2021
struct hci_cp_le_conn_param_req_neg_reply {
	__le16	handle;
	__u8	reason;
} __packed;

#define HCI_OP_LE_SET_DATA_LEN		0x2022
struct hci_cp_le_set_data_len {
	__le16	handle;
	__le16	tx_len;
	__le16	tx_time;
} __packed;
struct hci_rp_le_set_data_len {
	__u8	status;
	__le16	handle;
} __packed;

#define HCI_OP_LE_READ_DEF_DATA_LEN	0x2023
struct hci_rp_le_read_def_data_len {
	__u8	status;
	__le16	tx_len;
	__le16	tx_time;
} __packed;

#define HCI_OP_LE_WRITE_DEF_DATA_LEN	0x2024
struct hci_cp_le_write_def_data_len {
	__le16	tx_len;
	__le16	tx_time;
} __packed;

#define HCI_OP_LE_ADD_TO_RESOLV_LIST	0x2027
struct hci_cp_le_add_to_resolv_list {
	__u8	 bdaddr_type;
	bdaddr_t bdaddr;
	__u8	 peer_irk[16];
	__u8	 local_irk[16];
} __packed;

#define HCI_OP_LE_DEL_FROM_RESOLV_LIST	0x2028
struct hci_cp_le_del_from_resolv_list {
	__u8	 bdaddr_type;
	bdaddr_t bdaddr;
} __packed;

#define HCI_OP_LE_CLEAR_RESOLV_LIST	0x2029

#define HCI_OP_LE_READ_RESOLV_LIST_SIZE	0x202a
struct hci_rp_le_read_resolv_list_size {
	__u8	status;
	__u8	size;
} __packed;

#define HCI_OP_LE_SET_ADDR_RESOLV_ENABLE 0x202d

#define HCI_OP_LE_SET_RPA_TIMEOUT	0x202e

#define HCI_OP_LE_READ_MAX_DATA_LEN	0x202f
struct hci_rp_le_read_max_data_len {
	__u8	status;
	__le16	tx_len;
	__le16	tx_time;
	__le16	rx_len;
	__le16	rx_time;
} __packed;

#define HCI_OP_LE_SET_DEFAULT_PHY	0x2031
struct hci_cp_le_set_default_phy {
	__u8    all_phys;
	__u8    tx_phys;
	__u8    rx_phys;
} __packed;

#define HCI_LE_SET_PHY_1M		0x01
#define HCI_LE_SET_PHY_2M		0x02
#define HCI_LE_SET_PHY_CODED		0x04

#define HCI_OP_LE_SET_EXT_SCAN_PARAMS   0x2041
struct hci_cp_le_set_ext_scan_params {
	__u8    own_addr_type;
	__u8    filter_policy;
	__u8    scanning_phys;
	__u8    data[];
} __packed;

#define LE_SCAN_PHY_1M		0x01
#define LE_SCAN_PHY_2M		0x02
#define LE_SCAN_PHY_CODED	0x04

struct hci_cp_le_scan_phy_params {
	__u8    type;
	__le16  interval;
	__le16  window;
} __packed;

#define HCI_OP_LE_SET_EXT_SCAN_ENABLE   0x2042
struct hci_cp_le_set_ext_scan_enable {
	__u8    enable;
	__u8    filter_dup;
	__le16  duration;
	__le16  period;
} __packed;

#define HCI_OP_LE_EXT_CREATE_CONN    0x2043
struct hci_cp_le_ext_create_conn {
	__u8      filter_policy;
	__u8      own_addr_type;
	__u8      peer_addr_type;
	bdaddr_t  peer_addr;
	__u8      phys;
	__u8      data[];
} __packed;

struct hci_cp_le_ext_conn_param {
	__le16 scan_interval;
	__le16 scan_window;
	__le16 conn_interval_min;
	__le16 conn_interval_max;
	__le16 conn_latency;
	__le16 supervision_timeout;
	__le16 min_ce_len;
	__le16 max_ce_len;
} __packed;

#define HCI_OP_LE_PA_CREATE_SYNC	0x2044
struct hci_cp_le_pa_create_sync {
	__u8      options;
	__u8      sid;
	__u8      addr_type;
	bdaddr_t  addr;
	__le16    skip;
	__le16    sync_timeout;
	__u8      sync_cte_type;
} __packed;

#define HCI_OP_LE_PA_TERM_SYNC		0x2046
struct hci_cp_le_pa_term_sync {
	__le16    handle;
} __packed;

#define HCI_OP_LE_READ_NUM_SUPPORTED_ADV_SETS	0x203b
struct hci_rp_le_read_num_supported_adv_sets {
	__u8  status;
	__u8  num_of_sets;
} __packed;

#define HCI_OP_LE_SET_EXT_ADV_PARAMS		0x2036
struct hci_cp_le_set_ext_adv_params {
	__u8      handle;
	__le16    evt_properties;
	__u8      min_interval[3];
	__u8      max_interval[3];
	__u8      channel_map;
	__u8      own_addr_type;
	__u8      peer_addr_type;
	bdaddr_t  peer_addr;
	__u8      filter_policy;
	__u8      tx_power;
	__u8      primary_phy;
	__u8      secondary_max_skip;
	__u8      secondary_phy;
	__u8      sid;
	__u8      notif_enable;
} __packed;

#define HCI_ADV_PHY_1M		0X01
#define HCI_ADV_PHY_2M		0x02
#define HCI_ADV_PHY_CODED	0x03

struct hci_rp_le_set_ext_adv_params {
	__u8  status;
	__u8  tx_power;
} __packed;

struct hci_cp_ext_adv_set {
	__u8  handle;
	__le16 duration;
	__u8  max_events;
} __packed;

#define HCI_MAX_EXT_AD_LENGTH	251

#define HCI_OP_LE_SET_EXT_ADV_DATA		0x2037
struct hci_cp_le_set_ext_adv_data {
	__u8  handle;
	__u8  operation;
	__u8  frag_pref;
	__u8  length;
	__u8  data[];
} __packed;

#define HCI_OP_LE_SET_EXT_SCAN_RSP_DATA		0x2038
struct hci_cp_le_set_ext_scan_rsp_data {
	__u8  handle;
	__u8  operation;
	__u8  frag_pref;
	__u8  length;
	__u8  data[];
} __packed;

#define HCI_OP_LE_SET_EXT_ADV_ENABLE		0x2039
struct hci_cp_le_set_ext_adv_enable {
	__u8  enable;
	__u8  num_of_sets;
	__u8  data[];
} __packed;

#define HCI_OP_LE_SET_PER_ADV_PARAMS		0x203e
struct hci_cp_le_set_per_adv_params {
	__u8      handle;
	__le16    min_interval;
	__le16    max_interval;
	__le16    periodic_properties;
} __packed;

#define HCI_MAX_PER_AD_LENGTH	252

#define HCI_OP_LE_SET_PER_ADV_DATA		0x203f
struct hci_cp_le_set_per_adv_data {
	__u8  handle;
	__u8  operation;
	__u8  length;
	__u8  data[];
} __packed;

#define HCI_OP_LE_SET_PER_ADV_ENABLE		0x2040
struct hci_cp_le_set_per_adv_enable {
	__u8  enable;
	__u8  handle;
} __packed;

#define LE_SET_ADV_DATA_OP_COMPLETE	0x03

#define LE_SET_ADV_DATA_NO_FRAG		0x01

#define HCI_OP_LE_REMOVE_ADV_SET	0x203c

#define HCI_OP_LE_CLEAR_ADV_SETS	0x203d

#define HCI_OP_LE_SET_ADV_SET_RAND_ADDR	0x2035
struct hci_cp_le_set_adv_set_rand_addr {
	__u8  handle;
	bdaddr_t  bdaddr;
} __packed;

#define HCI_OP_LE_READ_TRANSMIT_POWER	0x204b
struct hci_rp_le_read_transmit_power {
	__u8  status;
	__s8  min_le_tx_power;
	__s8  max_le_tx_power;
} __packed;

#define HCI_NETWORK_PRIVACY		0x00
#define HCI_DEVICE_PRIVACY		0x01

#define HCI_OP_LE_SET_PRIVACY_MODE	0x204e
struct hci_cp_le_set_privacy_mode {
	__u8  bdaddr_type;
	bdaddr_t  bdaddr;
	__u8  mode;
} __packed;

#define HCI_OP_LE_READ_BUFFER_SIZE_V2	0x2060
struct hci_rp_le_read_buffer_size_v2 {
	__u8    status;
	__le16  acl_mtu;
	__u8    acl_max_pkt;
	__le16  iso_mtu;
	__u8    iso_max_pkt;
} __packed;

#define HCI_OP_LE_READ_ISO_TX_SYNC		0x2061
struct hci_cp_le_read_iso_tx_sync {
	__le16  handle;
} __packed;

struct hci_rp_le_read_iso_tx_sync {
	__u8    status;
	__le16  handle;
	__le16  seq;
	__le32  imestamp;
	__u8    offset[3];
} __packed;

#define HCI_OP_LE_SET_CIG_PARAMS		0x2062
struct hci_cis_params {
	__u8    cis_id;
	__le16  c_sdu;
	__le16  p_sdu;
	__u8    c_phy;
	__u8    p_phy;
	__u8    c_rtn;
	__u8    p_rtn;
} __packed;

struct hci_cp_le_set_cig_params {
	__u8    cig_id;
	__u8    c_interval[3];
	__u8    p_interval[3];
	__u8    sca;
	__u8    packing;
	__u8    framing;
	__le16  c_latency;
	__le16  p_latency;
	__u8    num_cis;
	struct hci_cis_params cis[];
} __packed;

struct hci_rp_le_set_cig_params {
	__u8    status;
	__u8    cig_id;
	__u8    num_handles;
	__le16  handle[];
} __packed;

#define HCI_OP_LE_CREATE_CIS			0x2064
struct hci_cis {
	__le16  cis_handle;
	__le16  acl_handle;
} __packed;

struct hci_cp_le_create_cis {
	__u8    num_cis;
	struct hci_cis cis[];
} __packed;

#define HCI_OP_LE_REMOVE_CIG			0x2065
struct hci_cp_le_remove_cig {
	__u8    cig_id;
} __packed;

#define HCI_OP_LE_ACCEPT_CIS			0x2066
struct hci_cp_le_accept_cis {
	__le16  handle;
} __packed;

#define HCI_OP_LE_REJECT_CIS			0x2067
struct hci_cp_le_reject_cis {
	__le16  handle;
	__u8    reason;
} __packed;

#define HCI_OP_LE_CREATE_BIG			0x2068
struct hci_bis {
	__u8    sdu_interval[3];
	__le16  sdu;
	__le16  latency;
	__u8    rtn;
	__u8    phy;
	__u8    packing;
	__u8    framing;
	__u8    encryption;
	__u8    bcode[16];
} __packed;

struct hci_cp_le_create_big {
	__u8    handle;
	__u8    adv_handle;
	__u8    num_bis;
	struct hci_bis bis;
} __packed;

#define HCI_OP_LE_TERM_BIG			0x206a
struct hci_cp_le_term_big {
	__u8    handle;
	__u8    reason;
} __packed;

#define HCI_OP_LE_BIG_CREATE_SYNC		0x206b
struct hci_cp_le_big_create_sync {
	__u8    handle;
	__le16  sync_handle;
	__u8    encryption;
	__u8    bcode[16];
	__u8    mse;
	__le16  timeout;
	__u8    num_bis;
	__u8    bis[0];
} __packed;

#define HCI_OP_LE_BIG_TERM_SYNC			0x206c
struct hci_cp_le_big_term_sync {
	__u8    handle;
} __packed;

#define HCI_OP_LE_SETUP_ISO_PATH		0x206e
struct hci_cp_le_setup_iso_path {
	__le16  handle;
	__u8    direction;
	__u8    path;
	__u8    codec;
	__le16  codec_cid;
	__le16  codec_vid;
	__u8    delay[3];
	__u8    codec_cfg_len;
	__u8    codec_cfg[0];
} __packed;

struct hci_rp_le_setup_iso_path {
	__u8    status;
	__le16  handle;
} __packed;

#define HCI_OP_LE_SET_HOST_FEATURE		0x2074
struct hci_cp_le_set_host_feature {
	__u8     bit_number;
	__u8     bit_value;
} __packed;

/* ---- HCI Events ---- */
struct hci_ev_status {
	__u8    status;
} __packed;

#define HCI_EV_INQUIRY_COMPLETE		0x01

#define HCI_EV_INQUIRY_RESULT		0x02
struct inquiry_info {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     pscan_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
} __packed;

struct hci_ev_inquiry_result {
	__u8    num;
	struct inquiry_info info[];
};

#define HCI_EV_CONN_COMPLETE		0x03
struct hci_ev_conn_complete {
	__u8     status;
	__le16   handle;
	bdaddr_t bdaddr;
	__u8     link_type;
	__u8     encr_mode;
} __packed;

#define HCI_EV_CONN_REQUEST		0x04
struct hci_ev_conn_request {
	bdaddr_t bdaddr;
	__u8     dev_class[3];
	__u8     link_type;
} __packed;

#define HCI_EV_DISCONN_COMPLETE		0x05
struct hci_ev_disconn_complete {
	__u8     status;
	__le16   handle;
	__u8     reason;
} __packed;

#define HCI_EV_AUTH_COMPLETE		0x06
struct hci_ev_auth_complete {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_EV_REMOTE_NAME		0x07
struct hci_ev_remote_name {
	__u8     status;
	bdaddr_t bdaddr;
	__u8     name[HCI_MAX_NAME_LENGTH];
} __packed;

#define HCI_EV_ENCRYPT_CHANGE		0x08
struct hci_ev_encrypt_change {
	__u8     status;
	__le16   handle;
	__u8     encrypt;
} __packed;

#define HCI_EV_CHANGE_LINK_KEY_COMPLETE	0x09
struct hci_ev_change_link_key_complete {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_EV_REMOTE_FEATURES		0x0b
struct hci_ev_remote_features {
	__u8     status;
	__le16   handle;
	__u8     features[8];
} __packed;

#define HCI_EV_REMOTE_VERSION		0x0c
struct hci_ev_remote_version {
	__u8     status;
	__le16   handle;
	__u8     lmp_ver;
	__le16   manufacturer;
	__le16   lmp_subver;
} __packed;

#define HCI_EV_QOS_SETUP_COMPLETE	0x0d
struct hci_qos {
	__u8     service_type;
	__u32    token_rate;
	__u32    peak_bandwidth;
	__u32    latency;
	__u32    delay_variation;
} __packed;
struct hci_ev_qos_setup_complete {
	__u8     status;
	__le16   handle;
	struct   hci_qos qos;
} __packed;

#define HCI_EV_CMD_COMPLETE		0x0e
struct hci_ev_cmd_complete {
	__u8     ncmd;
	__le16   opcode;
} __packed;

#define HCI_EV_CMD_STATUS		0x0f
struct hci_ev_cmd_status {
	__u8     status;
	__u8     ncmd;
	__le16   opcode;
} __packed;

#define HCI_EV_HARDWARE_ERROR		0x10
struct hci_ev_hardware_error {
	__u8     code;
} __packed;

#define HCI_EV_ROLE_CHANGE		0x12
struct hci_ev_role_change {
	__u8     status;
	bdaddr_t bdaddr;
	__u8     role;
} __packed;

#define HCI_EV_NUM_COMP_PKTS		0x13
struct hci_comp_pkts_info {
	__le16   handle;
	__le16   count;
} __packed;

struct hci_ev_num_comp_pkts {
	__u8     num;
	struct hci_comp_pkts_info handles[];
} __packed;

#define HCI_EV_MODE_CHANGE		0x14
struct hci_ev_mode_change {
	__u8     status;
	__le16   handle;
	__u8     mode;
	__le16   interval;
} __packed;

#define HCI_EV_PIN_CODE_REQ		0x16
struct hci_ev_pin_code_req {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_LINK_KEY_REQ		0x17
struct hci_ev_link_key_req {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_LINK_KEY_NOTIFY		0x18
struct hci_ev_link_key_notify {
	bdaddr_t bdaddr;
	__u8     link_key[HCI_LINK_KEY_SIZE];
	__u8     key_type;
} __packed;

#define HCI_EV_CLOCK_OFFSET		0x1c
struct hci_ev_clock_offset {
	__u8     status;
	__le16   handle;
	__le16   clock_offset;
} __packed;

#define HCI_EV_PKT_TYPE_CHANGE		0x1d
struct hci_ev_pkt_type_change {
	__u8     status;
	__le16   handle;
	__le16   pkt_type;
} __packed;

#define HCI_EV_PSCAN_REP_MODE		0x20
struct hci_ev_pscan_rep_mode {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
} __packed;

#define HCI_EV_INQUIRY_RESULT_WITH_RSSI	0x22
struct inquiry_info_rssi {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
	__s8     rssi;
} __packed;
struct inquiry_info_rssi_pscan {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     pscan_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
	__s8     rssi;
} __packed;
struct hci_ev_inquiry_result_rssi {
	__u8     num;
	__u8     data[];
} __packed;

#define HCI_EV_REMOTE_EXT_FEATURES	0x23
struct hci_ev_remote_ext_features {
	__u8     status;
	__le16   handle;
	__u8     page;
	__u8     max_page;
	__u8     features[8];
} __packed;

#define HCI_EV_SYNC_CONN_COMPLETE	0x2c
struct hci_ev_sync_conn_complete {
	__u8     status;
	__le16   handle;
	bdaddr_t bdaddr;
	__u8     link_type;
	__u8     tx_interval;
	__u8     retrans_window;
	__le16   rx_pkt_len;
	__le16   tx_pkt_len;
	__u8     air_mode;
} __packed;

#define HCI_EV_SYNC_CONN_CHANGED	0x2d
struct hci_ev_sync_conn_changed {
	__u8     status;
	__le16   handle;
	__u8     tx_interval;
	__u8     retrans_window;
	__le16   rx_pkt_len;
	__le16   tx_pkt_len;
} __packed;

#define HCI_EV_SNIFF_SUBRATE		0x2e
struct hci_ev_sniff_subrate {
	__u8     status;
	__le16   handle;
	__le16   max_tx_latency;
	__le16   max_rx_latency;
	__le16   max_remote_timeout;
	__le16   max_local_timeout;
} __packed;

#define HCI_EV_EXTENDED_INQUIRY_RESULT	0x2f
struct extended_inquiry_info {
	bdaddr_t bdaddr;
	__u8     pscan_rep_mode;
	__u8     pscan_period_mode;
	__u8     dev_class[3];
	__le16   clock_offset;
	__s8     rssi;
	__u8     data[240];
} __packed;

struct hci_ev_ext_inquiry_result {
	__u8     num;
	struct extended_inquiry_info info[];
} __packed;

#define HCI_EV_KEY_REFRESH_COMPLETE	0x30
struct hci_ev_key_refresh_complete {
	__u8	status;
	__le16	handle;
} __packed;

#define HCI_EV_IO_CAPA_REQUEST		0x31
struct hci_ev_io_capa_request {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_IO_CAPA_REPLY		0x32
struct hci_ev_io_capa_reply {
	bdaddr_t bdaddr;
	__u8     capability;
	__u8     oob_data;
	__u8     authentication;
} __packed;

#define HCI_EV_USER_CONFIRM_REQUEST	0x33
struct hci_ev_user_confirm_req {
	bdaddr_t	bdaddr;
	__le32		passkey;
} __packed;

#define HCI_EV_USER_PASSKEY_REQUEST	0x34
struct hci_ev_user_passkey_req {
	bdaddr_t	bdaddr;
} __packed;

#define HCI_EV_REMOTE_OOB_DATA_REQUEST	0x35
struct hci_ev_remote_oob_data_request {
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_SIMPLE_PAIR_COMPLETE	0x36
struct hci_ev_simple_pair_complete {
	__u8     status;
	bdaddr_t bdaddr;
} __packed;

#define HCI_EV_USER_PASSKEY_NOTIFY	0x3b
struct hci_ev_user_passkey_notify {
	bdaddr_t	bdaddr;
	__le32		passkey;
} __packed;

#define HCI_KEYPRESS_STARTED		0
#define HCI_KEYPRESS_ENTERED		1
#define HCI_KEYPRESS_ERASED		2
#define HCI_KEYPRESS_CLEARED		3
#define HCI_KEYPRESS_COMPLETED		4

#define HCI_EV_KEYPRESS_NOTIFY		0x3c
struct hci_ev_keypress_notify {
	bdaddr_t	bdaddr;
	__u8		type;
} __packed;

#define HCI_EV_REMOTE_HOST_FEATURES	0x3d
struct hci_ev_remote_host_features {
	bdaddr_t bdaddr;
	__u8     features[8];
} __packed;

#define HCI_EV_LE_META			0x3e
struct hci_ev_le_meta {
	__u8     subevent;
} __packed;

#define HCI_EV_PHY_LINK_COMPLETE	0x40
struct hci_ev_phy_link_complete {
	__u8     status;
	__u8     phy_handle;
} __packed;

#define HCI_EV_CHANNEL_SELECTED		0x41
struct hci_ev_channel_selected {
	__u8     phy_handle;
} __packed;

#define HCI_EV_DISCONN_PHY_LINK_COMPLETE	0x42
struct hci_ev_disconn_phy_link_complete {
	__u8     status;
	__u8     phy_handle;
	__u8     reason;
} __packed;

#define HCI_EV_LOGICAL_LINK_COMPLETE		0x45
struct hci_ev_logical_link_complete {
	__u8     status;
	__le16   handle;
	__u8     phy_handle;
	__u8     flow_spec_id;
} __packed;

#define HCI_EV_DISCONN_LOGICAL_LINK_COMPLETE	0x46
struct hci_ev_disconn_logical_link_complete {
	__u8     status;
	__le16   handle;
	__u8     reason;
} __packed;

#define HCI_EV_NUM_COMP_BLOCKS		0x48
struct hci_comp_blocks_info {
	__le16   handle;
	__le16   pkts;
	__le16   blocks;
} __packed;

struct hci_ev_num_comp_blocks {
	__le16   num_blocks;
	__u8     num_hndl;
	struct hci_comp_blocks_info handles[];
} __packed;

#define HCI_EV_SYNC_TRAIN_COMPLETE	0x4F
struct hci_ev_sync_train_complete {
	__u8	status;
} __packed;

#define HCI_EV_PERIPHERAL_PAGE_RESP_TIMEOUT	0x54

#define HCI_EV_LE_CONN_COMPLETE		0x01
struct hci_ev_le_conn_complete {
	__u8     status;
	__le16   handle;
	__u8     role;
	__u8     bdaddr_type;
	bdaddr_t bdaddr;
	__le16   interval;
	__le16   latency;
	__le16   supervision_timeout;
	__u8     clk_accurancy;
} __packed;

/* Advertising report event types */
#define LE_ADV_IND		0x00
#define LE_ADV_DIRECT_IND	0x01
#define LE_ADV_SCAN_IND		0x02
#define LE_ADV_NONCONN_IND	0x03
#define LE_ADV_SCAN_RSP		0x04
#define LE_ADV_INVALID		0x05

/* Legacy event types in extended adv report */
#define LE_LEGACY_ADV_IND		0x0013
#define LE_LEGACY_ADV_DIRECT_IND 	0x0015
#define LE_LEGACY_ADV_SCAN_IND		0x0012
#define LE_LEGACY_NONCONN_IND		0x0010
#define LE_LEGACY_SCAN_RSP_ADV		0x001b
#define LE_LEGACY_SCAN_RSP_ADV_SCAN	0x001a

/* Extended Advertising event types */
#define LE_EXT_ADV_NON_CONN_IND		0x0000
#define LE_EXT_ADV_CONN_IND		0x0001
#define LE_EXT_ADV_SCAN_IND		0x0002
#define LE_EXT_ADV_DIRECT_IND		0x0004
#define LE_EXT_ADV_SCAN_RSP		0x0008
#define LE_EXT_ADV_LEGACY_PDU		0x0010

#define ADDR_LE_DEV_PUBLIC		0x00
#define ADDR_LE_DEV_RANDOM		0x01
#define ADDR_LE_DEV_PUBLIC_RESOLVED	0x02
#define ADDR_LE_DEV_RANDOM_RESOLVED	0x03

#define HCI_EV_LE_ADVERTISING_REPORT	0x02
struct hci_ev_le_advertising_info {
	__u8	 type;
	__u8	 bdaddr_type;
	bdaddr_t bdaddr;
	__u8	 length;
	__u8	 data[];
} __packed;

struct hci_ev_le_advertising_report {
	__u8    num;
	struct hci_ev_le_advertising_info info[];
} __packed;

#define HCI_EV_LE_CONN_UPDATE_COMPLETE	0x03
struct hci_ev_le_conn_update_complete {
	__u8     status;
	__le16   handle;
	__le16   interval;
	__le16   latency;
	__le16   supervision_timeout;
} __packed;

#define HCI_EV_LE_REMOTE_FEAT_COMPLETE	0x04
struct hci_ev_le_remote_feat_complete {
	__u8     status;
	__le16   handle;
	__u8     features[8];
} __packed;

#define HCI_EV_LE_LTK_REQ		0x05
struct hci_ev_le_ltk_req {
	__le16	handle;
	__le64	rand;
	__le16	ediv;
} __packed;

#define HCI_EV_LE_REMOTE_CONN_PARAM_REQ	0x06
struct hci_ev_le_remote_conn_param_req {
	__le16 handle;
	__le16 interval_min;
	__le16 interval_max;
	__le16 latency;
	__le16 timeout;
} __packed;

#define HCI_EV_LE_DATA_LEN_CHANGE	0x07
struct hci_ev_le_data_len_change {
	__le16	handle;
	__le16	tx_len;
	__le16	tx_time;
	__le16	rx_len;
	__le16	rx_time;
} __packed;

#define HCI_EV_LE_DIRECT_ADV_REPORT	0x0B
struct hci_ev_le_direct_adv_info {
	__u8	 type;
	__u8	 bdaddr_type;
	bdaddr_t bdaddr;
	__u8	 direct_addr_type;
	bdaddr_t direct_addr;
	__s8	 rssi;
} __packed;

struct hci_ev_le_direct_adv_report {
	__u8	 num;
	struct hci_ev_le_direct_adv_info info[];
} __packed;

#define HCI_EV_LE_PHY_UPDATE_COMPLETE	0x0c
struct hci_ev_le_phy_update_complete {
	__u8  status;
	__le16 handle;
	__u8  tx_phy;
	__u8  rx_phy;
} __packed;

#define HCI_EV_LE_EXT_ADV_REPORT    0x0d
struct hci_ev_le_ext_adv_info {
	__le16   type;
	__u8	 bdaddr_type;
	bdaddr_t bdaddr;
	__u8	 primary_phy;
	__u8	 secondary_phy;
	__u8	 sid;
	__u8	 tx_power;
	__s8	 rssi;
	__le16   interval;
	__u8     direct_addr_type;
	bdaddr_t direct_addr;
	__u8     length;
	__u8     data[];
} __packed;

struct hci_ev_le_ext_adv_report {
	__u8     num;
	struct hci_ev_le_ext_adv_info info[];
} __packed;

#define HCI_EV_LE_PA_SYNC_ESTABLISHED	0x0e
struct hci_ev_le_pa_sync_established {
	__u8      status;
	__le16    handle;
	__u8      sid;
	__u8      bdaddr_type;
	bdaddr_t  bdaddr;
	__u8      phy;
	__le16    interval;
	__u8      clock_accuracy;
} __packed;

#define HCI_EV_LE_ENHANCED_CONN_COMPLETE    0x0a
struct hci_ev_le_enh_conn_complete {
	__u8      status;
	__le16    handle;
	__u8      role;
	__u8      bdaddr_type;
	bdaddr_t  bdaddr;
	bdaddr_t  local_rpa;
	bdaddr_t  peer_rpa;
	__le16    interval;
	__le16    latency;
	__le16    supervision_timeout;
	__u8      clk_accurancy;
} __packed;

#define HCI_EV_LE_EXT_ADV_SET_TERM	0x12
struct hci_evt_le_ext_adv_set_term {
	__u8	status;
	__u8	handle;
	__le16	conn_handle;
	__u8	num_evts;
} __packed;

#define HCI_EVT_LE_CIS_ESTABLISHED	0x19
struct hci_evt_le_cis_established {
	__u8  status;
	__le16 handle;
	__u8  cig_sync_delay[3];
	__u8  cis_sync_delay[3];
	__u8  c_latency[3];
	__u8  p_latency[3];
	__u8  c_phy;
	__u8  p_phy;
	__u8  nse;
	__u8  c_bn;
	__u8  p_bn;
	__u8  c_ft;
	__u8  p_ft;
	__le16 c_mtu;
	__le16 p_mtu;
	__le16 interval;
} __packed;

#define HCI_EVT_LE_CIS_REQ		0x1a
struct hci_evt_le_cis_req {
	__le16 acl_handle;
	__le16 cis_handle;
	__u8  cig_id;
	__u8  cis_id;
} __packed;

#define HCI_EVT_LE_CREATE_BIG_COMPLETE	0x1b
struct hci_evt_le_create_big_complete {
	__u8    status;
	__u8    handle;
	__u8    sync_delay[3];
	__u8    transport_delay[3];
	__u8    phy;
	__u8    nse;
	__u8    bn;
	__u8    pto;
	__u8    irc;
	__le16  max_pdu;
	__le16  interval;
	__u8    num_bis;
	__le16  bis_handle[];
} __packed;

#define HCI_EVT_LE_BIG_SYNC_ESTABILISHED 0x1d
struct hci_evt_le_big_sync_estabilished {
	__u8    status;
	__u8    handle;
	__u8    latency[3];
	__u8    nse;
	__u8    bn;
	__u8    pto;
	__u8    irc;
	__le16  max_pdu;
	__le16  interval;
	__u8    num_bis;
	__le16  bis[];
} __packed;

#define HCI_EVT_LE_BIG_INFO_ADV_REPORT	0x22
struct hci_evt_le_big_info_adv_report {
	__le16  sync_handle;
	__u8    num_bis;
	__u8    nse;
	__le16  iso_interval;
	__u8    bn;
	__u8    pto;
	__u8    irc;
	__le16  max_pdu;
	__u8    sdu_interval[3];
	__le16  max_sdu;
	__u8    phy;
	__u8    framing;
	__u8    encryption;
} __packed;

#define HCI_EV_VENDOR			0xff

/* Internal events generated by Bluetooth stack */
#define HCI_EV_STACK_INTERNAL	0xfd
struct hci_ev_stack_internal {
	__u16    type;
	__u8     data[];
} __packed;

#define HCI_EV_SI_DEVICE	0x01
struct hci_ev_si_device {
	__u16    event;
	__u16    dev_id;
} __packed;

#define HCI_EV_SI_SECURITY	0x02
struct hci_ev_si_security {
	__u16    event;
	__u16    proto;
	__u16    subproto;
	__u8     incoming;
} __packed;

/* ---- HCI Packet structures ---- */
#define HCI_COMMAND_HDR_SIZE 3
#define HCI_EVENT_HDR_SIZE   2
#define HCI_ACL_HDR_SIZE     4
#define HCI_SCO_HDR_SIZE     3
#define HCI_ISO_HDR_SIZE     4

struct hci_command_hdr {
	__le16	opcode;		/* OCF & OGF */
	__u8	plen;
} __packed;

struct hci_event_hdr {
	__u8	evt;
	__u8	plen;
} __packed;

struct hci_acl_hdr {
	__le16	handle;		/* Handle & Flags(PB, BC) */
	__le16	dlen;
} __packed;

struct hci_sco_hdr {
	__le16	handle;
	__u8	dlen;
} __packed;

struct hci_iso_hdr {
	__le16	handle;
	__le16	dlen;
	__u8	data[];
} __packed;

/* ISO data packet status flags */
#define HCI_ISO_STATUS_VALID	0x00
#define HCI_ISO_STATUS_INVALID	0x01
#define HCI_ISO_STATUS_NOP	0x02

#define HCI_ISO_DATA_HDR_SIZE	4
struct hci_iso_data_hdr {
	__le16	sn;
	__le16	slen;
};

#define HCI_ISO_TS_DATA_HDR_SIZE 8
struct hci_iso_ts_data_hdr {
	__le32	ts;
	__le16	sn;
	__le16	slen;
};

static inline struct hci_event_hdr *hci_event_hdr(const struct sk_buff *skb)
{
	return (struct hci_event_hdr *) skb->data;
}

static inline struct hci_acl_hdr *hci_acl_hdr(const struct sk_buff *skb)
{
	return (struct hci_acl_hdr *) skb->data;
}

static inline struct hci_sco_hdr *hci_sco_hdr(const struct sk_buff *skb)
{
	return (struct hci_sco_hdr *) skb->data;
}

/* Command opcode pack/unpack */
#define hci_opcode_pack(ogf, ocf)	((__u16) ((ocf & 0x03ff)|(ogf << 10)))
#define hci_opcode_ogf(op)		(op >> 10)
#define hci_opcode_ocf(op)		(op & 0x03ff)

/* ACL handle and flags pack/unpack */
#define hci_handle_pack(h, f)	((__u16) ((h & 0x0fff)|(f << 12)))
#define hci_handle(h)		(h & 0x0fff)
#define hci_flags(h)		(h >> 12)

/* ISO handle and flags pack/unpack */
#define hci_iso_flags_pb(f)		(f & 0x0003)
#define hci_iso_flags_ts(f)		((f >> 2) & 0x0001)
#define hci_iso_flags_pack(pb, ts)	((pb & 0x03) | ((ts & 0x01) << 2))

/* ISO data length and flags pack/unpack */
#define hci_iso_data_len_pack(h, f)	((__u16) ((h) | ((f) << 14)))
#define hci_iso_data_len(h)		((h) & 0x3fff)
#define hci_iso_data_flags(h)		((h) >> 14)

/* codec transport types */
#define HCI_TRANSPORT_SCO_ESCO	0x01

/* le24 support */
static inline void hci_cpu_to_le24(__u32 val, __u8 dst[3])
{
	dst[0] = val & 0xff;
	dst[1] = (val & 0xff00) >> 8;
	dst[2] = (val & 0xff0000) >> 16;
}

#endif /* __HCI_H */
