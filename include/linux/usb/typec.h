/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_USB_TYPEC_H
#define __LINUX_USB_TYPEC_H

#include <linux/types.h>

/* USB Type-C Specification releases */
#define USB_TYPEC_REV_1_0	0x100 /* 1.0 */
#define USB_TYPEC_REV_1_1	0x110 /* 1.1 */
#define USB_TYPEC_REV_1_2	0x120 /* 1.2 */
#define USB_TYPEC_REV_1_3	0x130 /* 1.3 */
#define USB_TYPEC_REV_1_4	0x140 /* 1.4 */
#define USB_TYPEC_REV_2_0	0x200 /* 2.0 */

struct typec_partner;
struct typec_cable;
struct typec_plug;
struct typec_port;

struct fwnode_handle;
struct device;

enum typec_port_type {
	TYPEC_PORT_SRC,
	TYPEC_PORT_SNK,
	TYPEC_PORT_DRP,
};

enum typec_port_data {
	TYPEC_PORT_DFP,
	TYPEC_PORT_UFP,
	TYPEC_PORT_DRD,
};

enum typec_plug_type {
	USB_PLUG_NONE,
	USB_PLUG_TYPE_A,
	USB_PLUG_TYPE_B,
	USB_PLUG_TYPE_C,
	USB_PLUG_CAPTIVE,
};

enum typec_data_role {
	TYPEC_DEVICE,
	TYPEC_HOST,
};

enum typec_role {
	TYPEC_SINK,
	TYPEC_SOURCE,
};

enum typec_pwr_opmode {
	TYPEC_PWR_MODE_USB,
	TYPEC_PWR_MODE_1_5A,
	TYPEC_PWR_MODE_3_0A,
	TYPEC_PWR_MODE_PD,
};

enum typec_accessory {
	TYPEC_ACCESSORY_NONE,
	TYPEC_ACCESSORY_AUDIO,
	TYPEC_ACCESSORY_DEBUG,
};

#define TYPEC_MAX_ACCESSORY	3

enum typec_orientation {
	TYPEC_ORIENTATION_NONE,
	TYPEC_ORIENTATION_NORMAL,
	TYPEC_ORIENTATION_REVERSE,
};

/*
 * struct enter_usb_data - Enter_USB Message details
 * @eudo: Enter_USB Data Object
 * @active_link_training: Active Cable Plug Link Training
 *
 * @active_link_training is a flag that should be set with uni-directional SBRX
 * communication, and left 0 with passive cables and with bi-directional SBRX
 * communication.
 */
struct enter_usb_data {
	u32			eudo;
	unsigned char		active_link_training:1;
};

/*
 * struct usb_pd_identity - USB Power Delivery identity data
 * @id_header: ID Header VDO
 * @cert_stat: Cert Stat VDO
 * @product: Product VDO
 * @vdo: Product Type Specific VDOs
 *
 * USB power delivery Discover Identity command response data.
 *
 * REVISIT: This is USB Power Delivery specific information, so this structure
 * probable belongs to USB Power Delivery header file once we have them.
 */
struct usb_pd_identity {
	u32			id_header;
	u32			cert_stat;
	u32			product;
	u32			vdo[3];
};

int typec_partner_set_identity(struct typec_partner *partner);
int typec_cable_set_identity(struct typec_cable *cable);

/*
 * struct typec_altmode_desc - USB Type-C Alternate Mode Descriptor
 * @svid: Standard or Vendor ID
 * @mode: Index of the Mode
 * @vdo: VDO returned by Discover Modes USB PD command
 * @roles: Only for ports. DRP if the mode is available in both roles
 *
 * Description of an Alternate Mode which a connector, cable plug or partner
 * supports.
 */
struct typec_altmode_desc {
	u16			svid;
	u8			mode;
	u32			vdo;
	/* Only used with ports */
	enum typec_port_data	roles;
};

int typec_partner_set_num_altmodes(struct typec_partner *partner, int num_altmodes);
struct typec_altmode
*typec_partner_register_altmode(struct typec_partner *partner,
				const struct typec_altmode_desc *desc);
int typec_plug_set_num_altmodes(struct typec_plug *plug, int num_altmodes);
struct typec_altmode
*typec_plug_register_altmode(struct typec_plug *plug,
			     const struct typec_altmode_desc *desc);
struct typec_altmode
*typec_port_register_altmode(struct typec_port *port,
			     const struct typec_altmode_desc *desc);
void typec_unregister_altmode(struct typec_altmode *altmode);

struct typec_port *typec_altmode2port(struct typec_altmode *alt);

void typec_altmode_update_active(struct typec_altmode *alt, bool active);

enum typec_plug_index {
	TYPEC_PLUG_SOP_P,
	TYPEC_PLUG_SOP_PP,
};

/*
 * struct typec_plug_desc - USB Type-C Cable Plug Descriptor
 * @index: SOP Prime for the plug connected to DFP and SOP Double Prime for the
 *         plug connected to UFP
 *
 * Represents USB Type-C Cable Plug.
 */
struct typec_plug_desc {
	enum typec_plug_index	index;
};

/*
 * struct typec_cable_desc - USB Type-C Cable Descriptor
 * @type: The plug type from USB PD Cable VDO
 * @active: Is the cable active or passive
 * @identity: Result of Discover Identity command
 * @pd_revision: USB Power Delivery Specification revision if supported
 *
 * Represents USB Type-C Cable attached to USB Type-C port.
 */
struct typec_cable_desc {
	enum typec_plug_type	type;
	unsigned int		active:1;
	struct usb_pd_identity	*identity;
	u16			pd_revision; /* 0300H = "3.0" */

};

/*
 * struct typec_partner_desc - USB Type-C Partner Descriptor
 * @usb_pd: USB Power Delivery support
 * @accessory: Audio, Debug or none.
 * @identity: Discover Identity command data
 * @pd_revision: USB Power Delivery Specification Revision if supported
 *
 * Details about a partner that is attached to USB Type-C port. If @identity
 * member exists when partner is registered, a directory named "identity" is
 * created to sysfs for the partner device.
 *
 * @pd_revision is based on the setting of the "Specification Revision" field
 * in the message header on the initial "Source Capabilities" message received
 * from the partner, or a "Request" message received from the partner, depending
 * on whether our port is a Sink or a Source.
 */
struct typec_partner_desc {
	unsigned int		usb_pd:1;
	enum typec_accessory	accessory;
	struct usb_pd_identity	*identity;
	u16			pd_revision; /* 0300H = "3.0" */
};

/**
 * struct typec_operations - USB Type-C Port Operations
 * @try_role: Set data role preference for DRP port
 * @dr_set: Set Data Role
 * @pr_set: Set Power Role
 * @vconn_set: Source VCONN
 * @port_type_set: Set port type
 */
struct typec_operations {
	int (*try_role)(struct typec_port *port, int role);
	int (*dr_set)(struct typec_port *port, enum typec_data_role role);
	int (*pr_set)(struct typec_port *port, enum typec_role role);
	int (*vconn_set)(struct typec_port *port, enum typec_role role);
	int (*port_type_set)(struct typec_port *port,
			     enum typec_port_type type);
};

/*
 * struct typec_capability - USB Type-C Port Capabilities
 * @type: Supported power role of the port
 * @data: Supported data role of the port
 * @revision: USB Type-C Specification release. Binary coded decimal
 * @pd_revision: USB Power Delivery Specification revision if supported
 * @prefer_role: Initial role preference (DRP ports).
 * @accessory: Supported Accessory Modes
 * @fwnode: Optional fwnode of the port
 * @driver_data: Private pointer for driver specific info
 * @ops: Port operations vector
 *
 * Static capabilities of a single USB Type-C port.
 */
struct typec_capability {
	enum typec_port_type	type;
	enum typec_port_data	data;
	u16			revision; /* 0120H = "1.2" */
	u16			pd_revision; /* 0300H = "3.0" */
	int			prefer_role;
	enum typec_accessory	accessory[TYPEC_MAX_ACCESSORY];
	unsigned int		orientation_aware:1;

	struct fwnode_handle	*fwnode;
	void			*driver_data;

	const struct typec_operations	*ops;
};

/* Specific to try_role(). Indicates the user want's to clear the preference. */
#define TYPEC_NO_PREFERRED_ROLE	(-1)

struct typec_port *typec_register_port(struct device *parent,
				       const struct typec_capability *cap);
void typec_unregister_port(struct typec_port *port);

struct typec_partner *typec_register_partner(struct typec_port *port,
					     struct typec_partner_desc *desc);
void typec_unregister_partner(struct typec_partner *partner);

struct typec_cable *typec_register_cable(struct typec_port *port,
					 struct typec_cable_desc *desc);
void typec_unregister_cable(struct typec_cable *cable);

struct typec_cable *typec_cable_get(struct typec_port *port);
void typec_cable_put(struct typec_cable *cable);
int typec_cable_is_active(struct typec_cable *cable);

struct typec_plug *typec_register_plug(struct typec_cable *cable,
				       struct typec_plug_desc *desc);
void typec_unregister_plug(struct typec_plug *plug);

void typec_set_data_role(struct typec_port *port, enum typec_data_role role);
void typec_set_pwr_role(struct typec_port *port, enum typec_role role);
void typec_set_vconn_role(struct typec_port *port, enum typec_role role);
void typec_set_pwr_opmode(struct typec_port *port, enum typec_pwr_opmode mode);

int typec_set_orientation(struct typec_port *port,
			  enum typec_orientation orientation);
enum typec_orientation typec_get_orientation(struct typec_port *port);
int typec_set_mode(struct typec_port *port, int mode);

void *typec_get_drvdata(struct typec_port *port);

int typec_find_pwr_opmode(const char *name);
int typec_find_orientation(const char *name);
int typec_find_port_power_role(const char *name);
int typec_find_power_role(const char *name);
int typec_find_port_data_role(const char *name);
#endif /* __LINUX_USB_TYPEC_H */
