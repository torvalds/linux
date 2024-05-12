/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015-2017 Google, Inc
 */

#ifndef __LINUX_USB_TCPM_H
#define __LINUX_USB_TCPM_H

#include <linux/bitops.h>
#include <linux/usb/typec.h>
#include "pd.h"

enum typec_cc_status {
	TYPEC_CC_OPEN,
	TYPEC_CC_RA,
	TYPEC_CC_RD,
	TYPEC_CC_RP_DEF,
	TYPEC_CC_RP_1_5,
	TYPEC_CC_RP_3_0,
};

/* Collision Avoidance */
#define SINK_TX_NG	TYPEC_CC_RP_1_5
#define SINK_TX_OK	TYPEC_CC_RP_3_0

enum typec_cc_polarity {
	TYPEC_POLARITY_CC1,
	TYPEC_POLARITY_CC2,
};

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT	100		/* in ms	*/
#define PD_ROLE_SWAP_TIMEOUT	(MSEC_PER_SEC * 10)
#define PD_PPS_CTRL_TIMEOUT	(MSEC_PER_SEC * 10)

enum tcpm_transmit_status {
	TCPC_TX_SUCCESS = 0,
	TCPC_TX_DISCARDED = 1,
	TCPC_TX_FAILED = 2,
};

enum tcpm_transmit_type {
	TCPC_TX_SOP = 0,
	TCPC_TX_SOP_PRIME = 1,
	TCPC_TX_SOP_PRIME_PRIME = 2,
	TCPC_TX_SOP_DEBUG_PRIME = 3,
	TCPC_TX_SOP_DEBUG_PRIME_PRIME = 4,
	TCPC_TX_HARD_RESET = 5,
	TCPC_TX_CABLE_RESET = 6,
	TCPC_TX_BIST_MODE_2 = 7
};

/* Mux state attributes */
#define TCPC_MUX_USB_ENABLED		BIT(0)	/* USB enabled */
#define TCPC_MUX_DP_ENABLED		BIT(1)	/* DP enabled */
#define TCPC_MUX_POLARITY_INVERTED	BIT(2)	/* Polarity inverted */

/**
 * struct tcpc_dev - Port configuration and callback functions
 * @fwnode:	Pointer to port fwnode
 * @get_vbus:	Called to read current VBUS state
 * @get_current_limit:
 *		Optional; called by the tcpm core when configured as a snk
 *		and cc=Rp-def. This allows the tcpm to provide a fallback
 *		current-limit detection method for the cc=Rp-def case.
 *		For example, some tcpcs may include BC1.2 charger detection
 *		and use that in this case.
 * @set_cc:	Called to set value of CC pins
 * @apply_rc:	Optional; Needed to move TCPCI based chipset to APPLY_RC state
 *		as stated by the TCPCI specification.
 * @get_cc:	Called to read current CC pin values
 * @set_polarity:
 *		Called to set polarity
 * @set_vconn:	Called to enable or disable VCONN
 * @set_vbus:	Called to enable or disable VBUS
 * @set_current_limit:
 *		Optional; called to set current limit as negotiated
 *		with partner.
 * @set_pd_rx:	Called to enable or disable reception of PD messages
 * @set_roles:	Called to set power and data roles
 * @start_toggling:
 *		Optional; if supported by hardware, called to start dual-role
 *		toggling or single-role connection detection. Toggling stops
 *		automatically if a connection is established.
 * @try_role:	Optional; called to set a preferred role
 * @pd_transmit:Called to transmit PD message
 * @set_bist_data: Turn on/off bist data mode for compliance testing
 * @enable_frs:
 *		Optional; Called to enable/disable PD 3.0 fast role swap.
 *		Enabling frs is accessory dependent as not all PD3.0
 *		accessories support fast role swap.
 * @frs_sourcing_vbus:
 *		Optional; Called to notify that vbus is now being sourced.
 *		Low level drivers can perform chip specific operations, if any.
 * @enable_auto_vbus_discharge:
 *		Optional; TCPCI spec based TCPC implementations can optionally
 *		support hardware to autonomously dischrge vbus upon disconnecting
 *		as sink or source. TCPM signals TCPC to enable the mechanism upon
 *		entering connected state and signals disabling upon disconnect.
 * @set_auto_vbus_discharge_threshold:
 *		Mandatory when enable_auto_vbus_discharge is implemented. TCPM
 *		calls this function to allow lower levels drivers to program the
 *		vbus threshold voltage below which the vbus discharge circuit
 *		will be turned on. requested_vbus_voltage is set to 0 when vbus
 *		is going to disappear knowingly i.e. during PR_SWAP and
 *		HARD_RESET etc.
 * @is_vbus_vsafe0v:
 *		Optional; TCPCI spec based TCPC implementations are expected to
 *		detect VSAFE0V voltage level at vbus. When detection of VSAFE0V
 *		is supported by TCPC, set this callback for TCPM to query
 *		whether vbus is at VSAFE0V when needed.
 *		Returns true when vbus is at VSAFE0V, false otherwise.
 * @set_partner_usb_comm_capable:
 *              Optional; The USB Communications Capable bit indicates if port
 *              partner is capable of communication over the USB data lines
 *              (e.g. D+/- or SS Tx/Rx). Called to notify the status of the bit.
 * @check_contaminant:
 *		Optional; The callback is called when CC pins report open status
 *		at the end of the deboumce period or when the port is still
 *		toggling. Chip level drivers are expected to check for contaminant
 *		and call tcpm_clean_port when the port is clean.
 */
struct tcpc_dev {
	struct fwnode_handle *fwnode;

	int (*init)(struct tcpc_dev *dev);
	int (*get_vbus)(struct tcpc_dev *dev);
	int (*get_current_limit)(struct tcpc_dev *dev);
	int (*set_cc)(struct tcpc_dev *dev, enum typec_cc_status cc);
	int (*apply_rc)(struct tcpc_dev *dev, enum typec_cc_status cc,
			enum typec_cc_polarity polarity);
	int (*get_cc)(struct tcpc_dev *dev, enum typec_cc_status *cc1,
		      enum typec_cc_status *cc2);
	int (*set_polarity)(struct tcpc_dev *dev,
			    enum typec_cc_polarity polarity);
	int (*set_vconn)(struct tcpc_dev *dev, bool on);
	int (*set_vbus)(struct tcpc_dev *dev, bool on, bool charge);
	int (*set_current_limit)(struct tcpc_dev *dev, u32 max_ma, u32 mv);
	int (*set_pd_rx)(struct tcpc_dev *dev, bool on);
	int (*set_roles)(struct tcpc_dev *dev, bool attached,
			 enum typec_role role, enum typec_data_role data);
	int (*start_toggling)(struct tcpc_dev *dev,
			      enum typec_port_type port_type,
			      enum typec_cc_status cc);
	int (*try_role)(struct tcpc_dev *dev, int role);
	int (*pd_transmit)(struct tcpc_dev *dev, enum tcpm_transmit_type type,
			   const struct pd_message *msg, unsigned int negotiated_rev);
	int (*set_bist_data)(struct tcpc_dev *dev, bool on);
	int (*enable_frs)(struct tcpc_dev *dev, bool enable);
	void (*frs_sourcing_vbus)(struct tcpc_dev *dev);
	int (*enable_auto_vbus_discharge)(struct tcpc_dev *dev, bool enable);
	int (*set_auto_vbus_discharge_threshold)(struct tcpc_dev *dev, enum typec_pwr_opmode mode,
						 bool pps_active, u32 requested_vbus_voltage);
	bool (*is_vbus_vsafe0v)(struct tcpc_dev *dev);
	void (*set_partner_usb_comm_capable)(struct tcpc_dev *dev, bool enable);
	void (*check_contaminant)(struct tcpc_dev *dev);
};

struct tcpm_port;

struct tcpm_port *tcpm_register_port(struct device *dev, struct tcpc_dev *tcpc);
void tcpm_unregister_port(struct tcpm_port *port);

void tcpm_vbus_change(struct tcpm_port *port);
void tcpm_cc_change(struct tcpm_port *port);
void tcpm_sink_frs(struct tcpm_port *port);
void tcpm_sourcing_vbus(struct tcpm_port *port);
void tcpm_pd_receive(struct tcpm_port *port,
		     const struct pd_message *msg);
void tcpm_pd_transmit_complete(struct tcpm_port *port,
			       enum tcpm_transmit_status status);
void tcpm_pd_hard_reset(struct tcpm_port *port);
void tcpm_tcpc_reset(struct tcpm_port *port);
void tcpm_port_clean(struct tcpm_port *port);
bool tcpm_port_is_toggling(struct tcpm_port *port);

#endif /* __LINUX_USB_TCPM_H */
