/*
 * Copyright 2015-2017 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

/**
 * struct tcpc_config - Port configuration
 * @src_pdo:	PDO parameters sent to port partner as response to
 *		PD_CTRL_GET_SOURCE_CAP message
 * @nr_src_pdo:	Number of entries in @src_pdo
 * @snk_pdo:	PDO parameters sent to partner as response to
 *		PD_CTRL_GET_SINK_CAP message
 * @nr_snk_pdo:	Number of entries in @snk_pdo
 * @operating_snk_mw:
 *		Required operating sink power in mW
 * @type:	Port type (TYPEC_PORT_DFP, TYPEC_PORT_UFP, or
 *		TYPEC_PORT_DRP)
 * @default_role:
 *		Default port role (TYPEC_SINK or TYPEC_SOURCE).
 *		Set to TYPEC_NO_PREFERRED_ROLE if no default role.
 * @try_role_hw:True if try.{Src,Snk} is implemented in hardware
 * @alt_modes:	List of supported alternate modes
 */
struct tcpc_config {
	const u32 *src_pdo;
	unsigned int nr_src_pdo;

	const u32 *snk_pdo;
	unsigned int nr_snk_pdo;

	const u32 *snk_vdo;
	unsigned int nr_snk_vdo;

	unsigned int operating_snk_mw;

	enum typec_port_type type;
	enum typec_port_data data;
	enum typec_role default_role;
	bool try_role_hw;	/* try.{src,snk} implemented in hardware */
	bool self_powered;	/* port belongs to a self powered device */

	const struct typec_altmode_desc *alt_modes;
};

/* Mux state attributes */
#define TCPC_MUX_USB_ENABLED		BIT(0)	/* USB enabled */
#define TCPC_MUX_DP_ENABLED		BIT(1)	/* DP enabled */
#define TCPC_MUX_POLARITY_INVERTED	BIT(2)	/* Polarity inverted */

/**
 * struct tcpc_dev - Port configuration and callback functions
 * @config:	Pointer to port configuration
 * @fwnode:	Pointer to port fwnode
 * @get_vbus:	Called to read current VBUS state
 * @get_current_limit:
 *		Optional; called by the tcpm core when configured as a snk
 *		and cc=Rp-def. This allows the tcpm to provide a fallback
 *		current-limit detection method for the cc=Rp-def case.
 *		For example, some tcpcs may include BC1.2 charger detection
 *		and use that in this case.
 * @set_cc:	Called to set value of CC pins
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
 * @start_drp_toggling:
 *		Optional; if supported by hardware, called to start DRP
 *		toggling. DRP toggling is stopped automatically if
 *		a connection is established.
 * @try_role:	Optional; called to set a preferred role
 * @pd_transmit:Called to transmit PD message
 * @mux:	Pointer to multiplexer data
 */
struct tcpc_dev {
	const struct tcpc_config *config;
	struct fwnode_handle *fwnode;

	int (*init)(struct tcpc_dev *dev);
	int (*get_vbus)(struct tcpc_dev *dev);
	int (*get_current_limit)(struct tcpc_dev *dev);
	int (*set_cc)(struct tcpc_dev *dev, enum typec_cc_status cc);
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
	int (*start_drp_toggling)(struct tcpc_dev *dev,
				  enum typec_cc_status cc);
	int (*try_role)(struct tcpc_dev *dev, int role);
	int (*pd_transmit)(struct tcpc_dev *dev, enum tcpm_transmit_type type,
			   const struct pd_message *msg);
};

struct tcpm_port;

struct tcpm_port *tcpm_register_port(struct device *dev, struct tcpc_dev *tcpc);
void tcpm_unregister_port(struct tcpm_port *port);

int tcpm_update_source_capabilities(struct tcpm_port *port, const u32 *pdo,
				    unsigned int nr_pdo);
int tcpm_update_sink_capabilities(struct tcpm_port *port, const u32 *pdo,
				  unsigned int nr_pdo,
				  unsigned int operating_snk_mw);

void tcpm_vbus_change(struct tcpm_port *port);
void tcpm_cc_change(struct tcpm_port *port);
void tcpm_pd_receive(struct tcpm_port *port,
		     const struct pd_message *msg);
void tcpm_pd_transmit_complete(struct tcpm_port *port,
			       enum tcpm_transmit_status status);
void tcpm_pd_hard_reset(struct tcpm_port *port);
void tcpm_tcpc_reset(struct tcpm_port *port);

#endif /* __LINUX_USB_TCPM_H */
