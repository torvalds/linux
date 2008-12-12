/*
 * WiMedia Logical Link Control Protocol (WLP)
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 *
 * - Does not (yet) include support for WLP control frames
 *   WLP Draft 0.99 [6.5].
 *
 *   A visual representation of the data structures.
 *
 *                              wssidB      wssidB
 *                               ^           ^
 *                               |           |
 *                              wssidA      wssidA
 *   wlp interface {             ^           ^
 *       ...                     |           |
 *       ...               ...  wssid      wssid ...
 *       wlp --- ...             |           |
 *   };          neighbors --> neighbA --> neighbB
 *               ...
 *               wss
 *               ...
 *               eda cache  --> neighborA --> neighborB --> neighborC ...
 */

#ifndef __LINUX__WLP_H_
#define __LINUX__WLP_H_

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/uwb.h>

/**
 * WLP Protocol ID
 * WLP Draft 0.99 [6.2]
 *
 * The MUX header for all WLP frames
 */
#define WLP_PROTOCOL_ID 0x0100

/**
 * WLP Version
 * WLP version placed in the association frames (WLP 0.99 [6.6])
 */
#define WLP_VERSION 0x10

/**
 * Bytes needed to print UUID as string
 */
#define WLP_WSS_UUID_STRSIZE 48

/**
 * Bytes needed to print nonce as string
 */
#define WLP_WSS_NONCE_STRSIZE 48


/**
 * Size used for WLP name size
 *
 * The WSS name is set to 65 bytes, 1 byte larger than the maximum
 * allowed by the WLP spec. This is to have a null terminated string
 * for display to the user. A maximum of 64 bytes will still be used
 * when placing the WSS name field in association frames.
 */
#define WLP_WSS_NAME_SIZE 65

/**
 * Number of bytes added by WLP to data frame
 *
 * A data frame transmitted from a host will be placed in a Standard or
 * Abbreviated WLP frame. These have an extra 4 bytes of header (struct
 * wlp_frame_std_abbrv_hdr).
 * When the stack sends this data frame for transmission it needs to ensure
 * there is enough headroom for this header.
 */
#define WLP_DATA_HLEN 4

/**
 * State of device regarding WLP Service Set
 *
 * WLP_WSS_STATE_NONE: the host does not participate in any WSS
 * WLP_WSS_STATE_PART_ENROLLED: used as part of the enrollment sequence
 *                            ("Partial Enroll"). This state is used to
 *                            indicate the first part of enrollment that is
 *                            unsecure. If the WSS is unsecure then the
 *                            state will promptly go to WLP_WSS_STATE_ENROLLED,
 *                            if the WSS is not secure then the enrollment
 *                            procedure is a few more steps before we are
 *                            enrolled.
 * WLP_WSS_STATE_ENROLLED: the host is enrolled in a WSS
 * WLP_WSS_STATE_ACTIVE: WSS is activated
 * WLP_WSS_STATE_CONNECTED: host is connected to neighbor in WSS
 *
 */
enum wlp_wss_state {
	WLP_WSS_STATE_NONE = 0,
	WLP_WSS_STATE_PART_ENROLLED,
	WLP_WSS_STATE_ENROLLED,
	WLP_WSS_STATE_ACTIVE,
	WLP_WSS_STATE_CONNECTED,
};

/**
 * WSS Secure status
 * WLP 0.99 Table 6
 *
 * Set to one if the WSS is secure, zero if it is not secure
 */
enum wlp_wss_sec_status {
	WLP_WSS_UNSECURE = 0,
	WLP_WSS_SECURE,
};

/**
 * WLP frame type
 * WLP Draft 0.99 [6.2 Table 1]
 */
enum wlp_frame_type {
	WLP_FRAME_STANDARD = 0,
	WLP_FRAME_ABBREVIATED,
	WLP_FRAME_CONTROL,
	WLP_FRAME_ASSOCIATION,
};

/**
 * WLP Association Message Type
 * WLP Draft 0.99 [6.6.1.2 Table 8]
 */
enum wlp_assoc_type {
	WLP_ASSOC_D1 = 2,
	WLP_ASSOC_D2 = 3,
	WLP_ASSOC_M1 = 4,
	WLP_ASSOC_M2 = 5,
	WLP_ASSOC_M3 = 7,
	WLP_ASSOC_M4 = 8,
	WLP_ASSOC_M5 = 9,
	WLP_ASSOC_M6 = 10,
	WLP_ASSOC_M7 = 11,
	WLP_ASSOC_M8 = 12,
	WLP_ASSOC_F0 = 14,
	WLP_ASSOC_E1 = 32,
	WLP_ASSOC_E2 = 33,
	WLP_ASSOC_C1 = 34,
	WLP_ASSOC_C2 = 35,
	WLP_ASSOC_C3 = 36,
	WLP_ASSOC_C4 = 37,
};

/**
 * WLP Attribute Type
 * WLP Draft 0.99 [6.6.1 Table 6]
 */
enum wlp_attr_type {
	WLP_ATTR_AUTH		= 0x1005, /* Authenticator */
	WLP_ATTR_DEV_NAME 	= 0x1011, /* Device Name */
	WLP_ATTR_DEV_PWD_ID 	= 0x1012, /* Device Password ID */
	WLP_ATTR_E_HASH1	= 0x1014, /* E-Hash1 */
	WLP_ATTR_E_HASH2	= 0x1015, /* E-Hash2 */
	WLP_ATTR_E_SNONCE1	= 0x1016, /* E-SNonce1 */
	WLP_ATTR_E_SNONCE2	= 0x1017, /* E-SNonce2 */
	WLP_ATTR_ENCR_SET	= 0x1018, /* Encrypted Settings */
	WLP_ATTR_ENRL_NONCE	= 0x101A, /* Enrollee Nonce */
	WLP_ATTR_KEYWRAP_AUTH	= 0x101E, /* Key Wrap Authenticator */
	WLP_ATTR_MANUF		= 0x1021, /* Manufacturer */
	WLP_ATTR_MSG_TYPE	= 0x1022, /* Message Type */
	WLP_ATTR_MODEL_NAME	= 0x1023, /* Model Name */
	WLP_ATTR_MODEL_NR	= 0x1024, /* Model Number */
	WLP_ATTR_PUB_KEY	= 0x1032, /* Public Key */
	WLP_ATTR_REG_NONCE	= 0x1039, /* Registrar Nonce */
	WLP_ATTR_R_HASH1	= 0x103D, /* R-Hash1 */
	WLP_ATTR_R_HASH2	= 0x103E, /* R-Hash2 */
	WLP_ATTR_R_SNONCE1	= 0x103F, /* R-SNonce1 */
	WLP_ATTR_R_SNONCE2	= 0x1040, /* R-SNonce2 */
	WLP_ATTR_SERIAL		= 0x1042, /* Serial number */
	WLP_ATTR_UUID_E		= 0x1047, /* UUID-E */
	WLP_ATTR_UUID_R		= 0x1048, /* UUID-R */
	WLP_ATTR_PRI_DEV_TYPE	= 0x1054, /* Primary Device Type */
	WLP_ATTR_SEC_DEV_TYPE	= 0x1055, /* Secondary Device Type */
	WLP_ATTR_PORT_DEV	= 0x1056, /* Portable Device */
	WLP_ATTR_APP_EXT	= 0x1058, /* Application Extension */
	WLP_ATTR_WLP_VER	= 0x2000, /* WLP Version */
	WLP_ATTR_WSSID		= 0x2001, /* WSSID */
	WLP_ATTR_WSS_NAME	= 0x2002, /* WSS Name */
	WLP_ATTR_WSS_SEC_STAT	= 0x2003, /* WSS Secure Status */
	WLP_ATTR_WSS_BCAST	= 0x2004, /* WSS Broadcast Address */
	WLP_ATTR_WSS_M_KEY	= 0x2005, /* WSS Master Key */
	WLP_ATTR_ACC_ENRL	= 0x2006, /* Accepting Enrollment */
	WLP_ATTR_WSS_INFO	= 0x2007, /* WSS Information */
	WLP_ATTR_WSS_SEL_MTHD	= 0x2008, /* WSS Selection Method */
	WLP_ATTR_ASSC_MTHD_LIST	= 0x2009, /* Association Methods List */
	WLP_ATTR_SEL_ASSC_MTHD	= 0x200A, /* Selected Association Method */
	WLP_ATTR_ENRL_HASH_COMM	= 0x200B, /* Enrollee Hash Commitment */
	WLP_ATTR_WSS_TAG	= 0x200C, /* WSS Tag */
	WLP_ATTR_WSS_VIRT	= 0x200D, /* WSS Virtual EUI-48 */
	WLP_ATTR_WLP_ASSC_ERR	= 0x200E, /* WLP Association Error */
	WLP_ATTR_VNDR_EXT	= 0x200F, /* Vendor Extension */
};

/**
 * WLP Category ID of primary/secondary device
 * WLP Draft 0.99 [6.6.1.8 Table 12]
 */
enum wlp_dev_category_id {
	WLP_DEV_CAT_COMPUTER = 1,
	WLP_DEV_CAT_INPUT,
	WLP_DEV_CAT_PRINT_SCAN_FAX_COPIER,
	WLP_DEV_CAT_CAMERA,
	WLP_DEV_CAT_STORAGE,
	WLP_DEV_CAT_INFRASTRUCTURE,
	WLP_DEV_CAT_DISPLAY,
	WLP_DEV_CAT_MULTIM,
	WLP_DEV_CAT_GAMING,
	WLP_DEV_CAT_TELEPHONE,
	WLP_DEV_CAT_OTHER = 65535,
};

/**
 * WLP WSS selection method
 * WLP Draft 0.99 [6.6.1.6 Table 10]
 */
enum wlp_wss_sel_mthd {
	WLP_WSS_ENRL_SELECT = 1,	/* Enrollee selects */
	WLP_WSS_REG_SELECT,		/* Registrar selects */
};

/**
 * WLP association error values
 * WLP Draft 0.99 [6.6.1.5 Table 9]
 */
enum wlp_assc_error {
	WLP_ASSOC_ERROR_NONE,
	WLP_ASSOC_ERROR_AUTH,		/* Authenticator Failure */
	WLP_ASSOC_ERROR_ROGUE,		/* Rogue activity suspected */
	WLP_ASSOC_ERROR_BUSY,		/* Device busy */
	WLP_ASSOC_ERROR_LOCK,		/* Setup Locked */
	WLP_ASSOC_ERROR_NOT_READY,	/* Registrar not ready */
	WLP_ASSOC_ERROR_INV,		/* Invalid WSS selection */
	WLP_ASSOC_ERROR_MSG_TIME,	/* Message timeout */
	WLP_ASSOC_ERROR_ENR_TIME,	/* Enrollment session timeout */
	WLP_ASSOC_ERROR_PW,		/* Device password invalid */
	WLP_ASSOC_ERROR_VER,		/* Unsupported version */
	WLP_ASSOC_ERROR_INT,		/* Internal error */
	WLP_ASSOC_ERROR_UNDEF,		/* Undefined error */
	WLP_ASSOC_ERROR_NUM,		/* Numeric comparison failure */
	WLP_ASSOC_ERROR_WAIT,		/* Waiting for user input */
};

/**
 * WLP Parameters
 * WLP 0.99 [7.7]
 */
enum wlp_parameters {
	WLP_PER_MSG_TIMEOUT = 15,	/* Seconds to wait for response to
					   association message. */
};

/**
 * WLP IE
 *
 * The WLP IE should be included in beacons by all devices.
 *
 * The driver can set only a few of the fields in this information element,
 * most fields are managed by the device self. When the driver needs to set
 * a field it will only provide values for the fields of interest, the rest
 * will be filled with zeroes. The fields of interest are:
 *
 * Element ID
 * Length
 * Capabilities (only to include WSSID Hash list length)
 * WSSID Hash List fields
 *
 * WLP 0.99 [6.7]
 *
 * Only the fields that will be used are detailed in this structure, rest
 * are not detailed or marked as "notused".
 */
struct wlp_ie {
	struct uwb_ie_hdr hdr;
	__le16 capabilities;
	__le16 cycle_param;
	__le16 acw_anchor_addr;
	u8 wssid_hash_list[];
} __attribute__((packed));

static inline int wlp_ie_hash_length(struct wlp_ie *ie)
{
	return (le16_to_cpu(ie->capabilities) >> 12) & 0xf;
}

static inline void wlp_ie_set_hash_length(struct wlp_ie *ie, int hash_length)
{
	u16 caps = le16_to_cpu(ie->capabilities);
	caps = (caps & ~(0xf << 12)) | (hash_length << 12);
	ie->capabilities = cpu_to_le16(caps);
}

/**
 * WLP nonce
 * WLP Draft 0.99 [6.6.1 Table 6]
 *
 * A 128-bit random number often used (E-SNonce1, E-SNonce2, Enrollee
 * Nonce, Registrar Nonce, R-SNonce1, R-SNonce2). It is passed to HW so
 * it is packed.
 */
struct wlp_nonce {
	u8 data[16];
} __attribute__((packed));

/**
 * WLP UUID
 * WLP Draft 0.99 [6.6.1 Table 6]
 *
 * Universally Unique Identifier (UUID) encoded as an octet string in the
 * order the octets are shown in string representation in RFC4122. A UUID
 * is often used (UUID-E, UUID-R, WSSID). It is passed to HW so it is packed.
 */
struct wlp_uuid {
	u8 data[16];
} __attribute__((packed));


/**
 * Primary and secondary device type attributes
 * WLP Draft 0.99 [6.6.1.8]
 */
struct wlp_dev_type {
	enum wlp_dev_category_id category:16;
	u8 OUI[3];
	u8 OUIsubdiv;
	__le16 subID;
} __attribute__((packed));

/**
 * WLP frame header
 * WLP Draft 0.99 [6.2]
 */
struct wlp_frame_hdr {
	__le16 mux_hdr;			/* WLP_PROTOCOL_ID */
	enum wlp_frame_type type:8;
} __attribute__((packed));

/**
 * WLP attribute field header
 * WLP Draft 0.99 [6.6.1]
 *
 * Header of each attribute found in an association frame
 */
struct wlp_attr_hdr {
	__le16 type;
	__le16 length;
} __attribute__((packed));

/**
 * Device information commonly used together
 *
 * Each of these device information elements has a specified range in which it
 * should fit (WLP 0.99 [Table 6]). This range provided in the spec does not
 * include the termination null '\0' character (when used in the
 * association protocol the attribute fields are accompanied
 * with a "length" field so the full range from the spec can be used for
 * the value). We thus allocate an extra byte to be able to store a string
 * of max length with a terminating '\0'.
 */
struct wlp_device_info {
	char name[33];
	char model_name[33];
	char manufacturer[65];
	char model_nr[33];
	char serial[33];
	struct wlp_dev_type prim_dev_type;
};

/**
 * Macros for the WLP attributes
 *
 * There are quite a few attributes (total is 43). The attribute layout can be
 * in one of three categories: one value, an array, an enum forced to 8 bits.
 * These macros help with their definitions.
 */
#define wlp_attr(type, name)						\
struct wlp_attr_##name {						\
	struct wlp_attr_hdr hdr;					\
	type name;							\
} __attribute__((packed));

#define wlp_attr_array(type, name)					\
struct wlp_attr_##name {						\
	struct wlp_attr_hdr hdr;					\
	type name[];							\
} __attribute__((packed));

/**
 * WLP association attribute fields
 * WLP Draft 0.99 [6.6.1 Table 6]
 *
 * Attributes appear in same order as the Table in the spec
 * FIXME Does not define all attributes yet
 */

/* Device name: Friendly name of sending device */
wlp_attr_array(u8, dev_name)

/* Enrollee Nonce: Random number generated by enrollee for an enrollment
 * session */
wlp_attr(struct wlp_nonce, enonce)

/* Manufacturer name: Name of manufacturer of the sending device */
wlp_attr_array(u8, manufacturer)

/* WLP Message Type */
wlp_attr(u8, msg_type)

/* WLP Model name: Model name of sending device */
wlp_attr_array(u8, model_name)

/* WLP Model number: Model number of sending device */
wlp_attr_array(u8, model_nr)

/* Registrar Nonce: Random number generated by registrar for an enrollment
 * session */
wlp_attr(struct wlp_nonce, rnonce)

/* Serial number of device */
wlp_attr_array(u8, serial)

/* UUID of enrollee */
wlp_attr(struct wlp_uuid, uuid_e)

/* UUID of registrar */
wlp_attr(struct wlp_uuid, uuid_r)

/* WLP Primary device type */
wlp_attr(struct wlp_dev_type, prim_dev_type)

/* WLP Secondary device type */
wlp_attr(struct wlp_dev_type, sec_dev_type)

/* WLP protocol version */
wlp_attr(u8, version)

/* WLP service set identifier */
wlp_attr(struct wlp_uuid, wssid)

/* WLP WSS name */
wlp_attr_array(u8, wss_name)

/* WLP WSS Secure Status */
wlp_attr(u8, wss_sec_status)

/* WSS Broadcast Address */
wlp_attr(struct uwb_mac_addr, wss_bcast)

/* WLP Accepting Enrollment */
wlp_attr(u8, accept_enrl)

/**
 * WSS information attributes
 * WLP Draft 0.99 [6.6.3 Table 15]
 */
struct wlp_wss_info {
	struct wlp_attr_wssid wssid;
	struct wlp_attr_wss_name name;
	struct wlp_attr_accept_enrl accept;
	struct wlp_attr_wss_sec_status sec_stat;
	struct wlp_attr_wss_bcast bcast;
} __attribute__((packed));

/* WLP WSS Information */
wlp_attr_array(struct wlp_wss_info, wss_info)

/* WLP WSS Selection method */
wlp_attr(u8, wss_sel_mthd)

/* WLP WSS tag */
wlp_attr(u8, wss_tag)

/* WSS Virtual Address */
wlp_attr(struct uwb_mac_addr, wss_virt)

/* WLP association error */
wlp_attr(u8, wlp_assc_err)

/**
 * WLP standard and abbreviated frames
 *
 * WLP Draft 0.99 [6.3] and [6.4]
 *
 * The difference between the WLP standard frame and the WLP
 * abbreviated frame is that the standard frame includes the src
 * and dest addresses from the Ethernet header, the abbreviated frame does
 * not.
 * The src/dest (as well as the type/length and client data) are already
 * defined as part of the Ethernet header, we do not do this here.
 * From this perspective the standard and abbreviated frames appear the
 * same - they will be treated differently though.
 *
 * The size of this header is also captured in WLP_DATA_HLEN to enable
 * interfaces to prepare their headroom.
 */
struct wlp_frame_std_abbrv_hdr {
	struct wlp_frame_hdr hdr;
	u8 tag;
} __attribute__((packed));

/**
 * WLP association frames
 *
 * WLP Draft 0.99 [6.6]
 */
struct wlp_frame_assoc {
	struct wlp_frame_hdr hdr;
	enum wlp_assoc_type type:8;
	struct wlp_attr_version version;
	struct wlp_attr_msg_type msg_type;
	u8 attr[];
} __attribute__((packed));

/* Ethernet to dev address mapping */
struct wlp_eda {
	spinlock_t lock;
	struct list_head cache;	/* Eth<->Dev Addr cache */
};

/**
 * WSS information temporary storage
 *
 * This information is only stored temporarily during discovery. It should
 * not be stored unless the device is enrolled in the advertised WSS. This
 * is done mainly because we follow the letter of the spec in this regard.
 * See WLP 0.99 [7.2.3].
 * When the device does become enrolled in a WSS the WSS information will
 * be stored as part of the more comprehensive struct wlp_wss.
 */
struct wlp_wss_tmp_info {
	char name[WLP_WSS_NAME_SIZE];
	u8 accept_enroll;
	u8 sec_status;
	struct uwb_mac_addr bcast;
};

struct wlp_wssid_e {
	struct list_head node;
	struct wlp_uuid wssid;
	struct wlp_wss_tmp_info *info;
};

/**
 * A cache entry of WLP neighborhood
 *
 * @node: head of list is wlp->neighbors
 * @wssid: list of wssids of this neighbor, element is wlp_wssid_e
 * @info:  temporary storage for information learned during discovery. This
 *         storage is used together with the wssid_e temporary storage
 *         during discovery.
 */
struct wlp_neighbor_e {
	struct list_head node;
	struct wlp_uuid uuid;
	struct uwb_dev *uwb_dev;
	struct list_head wssid; /* Elements are wlp_wssid_e */
	struct wlp_device_info *info;
};

struct wlp;
/**
 * Information for an association session in progress.
 *
 * @exp_message: The type of the expected message. Both this message and a
 *               F0 message (which can be sent in response to any
 *               association frame) will be accepted as a valid message for
 *               this session.
 * @cb:          The function that will be called upon receipt of this
 *               message.
 * @cb_priv:     Private data of callback
 * @data:        Data used in association process (always a sk_buff?)
 * @neighbor:    Address of neighbor with which association session is in
 *               progress.
 */
struct wlp_session {
	enum wlp_assoc_type exp_message;
	void (*cb)(struct wlp *);
	void *cb_priv;
	void *data;
	struct uwb_dev_addr neighbor_addr;
};

/**
 * WLP Service Set
 *
 * @mutex: used to protect entire WSS structure.
 *
 * @name: The WSS name is set to 65 bytes, 1 byte larger than the maximum
 *        allowed by the WLP spec. This is to have a null terminated string
 *        for display to the user. A maximum of 64 bytes will still be used
 *        when placing the WSS name field in association frames.
 *
 * @accept_enroll: Accepting enrollment: Set to one if registrar is
 *                 accepting enrollment in WSS, or zero otherwise.
 *
 * Global and local information for each WSS in which we are enrolled.
 * WLP 0.99 Section 7.2.1 and Section 7.2.2
 */
struct wlp_wss {
	struct mutex mutex;
	struct kobject kobj;
	/* Global properties. */
	struct wlp_uuid wssid;
	u8 hash;
	char name[WLP_WSS_NAME_SIZE];
	struct uwb_mac_addr bcast;
	u8 secure_status:1;
	u8 master_key[16];
	/* Local properties. */
	u8 tag;
	struct uwb_mac_addr virtual_addr;
	/* Extra */
	u8 accept_enroll:1;
	enum wlp_wss_state state;
};

/**
 * WLP main structure
 * @mutex: protect changes to WLP structure. We only allow changes to the
 *         uuid, so currently this mutex only protects this field.
 */
struct wlp {
	struct mutex mutex;
	struct uwb_rc *rc;		/* UWB radio controller */
	struct uwb_pal pal;
	struct wlp_eda eda;
	struct wlp_uuid uuid;
	struct wlp_session *session;
	struct wlp_wss wss;
	struct mutex nbmutex; /* Neighbor mutex protects neighbors list */
	struct list_head neighbors; /* Elements are wlp_neighbor_e */
	struct uwb_notifs_handler uwb_notifs_handler;
	struct wlp_device_info *dev_info;
	void (*fill_device_info)(struct wlp *wlp, struct wlp_device_info *info);
	int (*xmit_frame)(struct wlp *, struct sk_buff *,
			  struct uwb_dev_addr *);
	void (*stop_queue)(struct wlp *);
	void (*start_queue)(struct wlp *);
};

/* sysfs */


struct wlp_wss_attribute {
	struct attribute attr;
	ssize_t (*show)(struct wlp_wss *wss, char *buf);
	ssize_t (*store)(struct wlp_wss *wss, const char *buf, size_t count);
};

#define WSS_ATTR(_name, _mode, _show, _store) \
static struct wlp_wss_attribute wss_attr_##_name = __ATTR(_name, _mode,	\
							  _show, _store)

extern int wlp_setup(struct wlp *, struct uwb_rc *);
extern void wlp_remove(struct wlp *);
extern ssize_t wlp_neighborhood_show(struct wlp *, char *);
extern int wlp_wss_setup(struct net_device *, struct wlp_wss *);
extern void wlp_wss_remove(struct wlp_wss *);
extern ssize_t wlp_wss_activate_show(struct wlp_wss *, char *);
extern ssize_t wlp_wss_activate_store(struct wlp_wss *, const char *, size_t);
extern ssize_t wlp_eda_show(struct wlp *, char *);
extern ssize_t wlp_eda_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_uuid_show(struct wlp *, char *);
extern ssize_t wlp_uuid_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_name_show(struct wlp *, char *);
extern ssize_t wlp_dev_name_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_manufacturer_show(struct wlp *, char *);
extern ssize_t wlp_dev_manufacturer_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_model_name_show(struct wlp *, char *);
extern ssize_t wlp_dev_model_name_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_model_nr_show(struct wlp *, char *);
extern ssize_t wlp_dev_model_nr_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_serial_show(struct wlp *, char *);
extern ssize_t wlp_dev_serial_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_prim_category_show(struct wlp *, char *);
extern ssize_t wlp_dev_prim_category_store(struct wlp *, const char *,
					   size_t);
extern ssize_t wlp_dev_prim_OUI_show(struct wlp *, char *);
extern ssize_t wlp_dev_prim_OUI_store(struct wlp *, const char *, size_t);
extern ssize_t wlp_dev_prim_OUI_sub_show(struct wlp *, char *);
extern ssize_t wlp_dev_prim_OUI_sub_store(struct wlp *, const char *,
					  size_t);
extern ssize_t wlp_dev_prim_subcat_show(struct wlp *, char *);
extern ssize_t wlp_dev_prim_subcat_store(struct wlp *, const char *,
					 size_t);
extern int wlp_receive_frame(struct device *, struct wlp *, struct sk_buff *,
			     struct uwb_dev_addr *);
extern int wlp_prepare_tx_frame(struct device *, struct wlp *,
			       struct sk_buff *, struct uwb_dev_addr *);
void wlp_reset_all(struct wlp *wlp);

/**
 * Initialize WSS
 */
static inline
void wlp_wss_init(struct wlp_wss *wss)
{
	mutex_init(&wss->mutex);
}

static inline
void wlp_init(struct wlp *wlp)
{
	INIT_LIST_HEAD(&wlp->neighbors);
	mutex_init(&wlp->mutex);
	mutex_init(&wlp->nbmutex);
	wlp_wss_init(&wlp->wss);
}


#endif /* #ifndef __LINUX__WLP_H_ */
