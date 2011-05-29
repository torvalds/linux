/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CFCNFG_H_
#define CFCNFG_H_
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfctrl.h>

struct cfcnfg;

/**
 * enum cfcnfg_phy_type -  Types of physical layers defined in CAIF Stack
 *
 * @CFPHYTYPE_FRAG:	Fragmented frames physical interface.
 * @CFPHYTYPE_CAIF:	Generic CAIF physical interface
 */
enum cfcnfg_phy_type {
	CFPHYTYPE_FRAG = 1,
	CFPHYTYPE_CAIF,
	CFPHYTYPE_MAX
};

/**
 * enum cfcnfg_phy_preference - Physical preference HW Abstraction
 *
 * @CFPHYPREF_UNSPECIFIED:	Default physical interface
 *
 * @CFPHYPREF_LOW_LAT:		Default physical interface for low-latency
 *				traffic
 * @CFPHYPREF_HIGH_BW:		Default physical interface for high-bandwidth
 *				traffic
 * @CFPHYPREF_LOOP:		TEST only Loopback interface simulating modem
 *				responses.
 *
 */
enum cfcnfg_phy_preference {
	CFPHYPREF_UNSPECIFIED,
	CFPHYPREF_LOW_LAT,
	CFPHYPREF_HIGH_BW,
	CFPHYPREF_LOOP
};

/**
 * cfcnfg_create() - Create the CAIF configuration object.
 */
struct cfcnfg *cfcnfg_create(void);

/**
 * cfcnfg_remove() -  Remove the CFCNFG object
 * @cfg: config object
 */
void cfcnfg_remove(struct cfcnfg *cfg);

/**
 * cfcnfg_add_phy_layer() - Adds a physical layer to the CAIF stack.
 * @cnfg:	Pointer to a CAIF configuration object, created by
 *		cfcnfg_create().
 * @phy_type:	Specifies the type of physical interface, e.g.
 *			CFPHYTYPE_FRAG.
 * @dev:	Pointer to link layer device
 * @phy_layer:	Specify the physical layer. The transmit function
 *		MUST be set in the structure.
 * @phyid:	The assigned physical ID for this layer, used in
 *		cfcnfg_add_adapt_layer to specify PHY for the link.
 * @pref:	The phy (link layer) preference.
 * @fcs:	Specify if checksum is used in CAIF Framing Layer.
 * @stx:	Specify if Start Of Frame extension is used.
 */

void
cfcnfg_add_phy_layer(struct cfcnfg *cnfg, enum cfcnfg_phy_type phy_type,
		     struct net_device *dev, struct cflayer *phy_layer,
		     u16 *phyid, enum cfcnfg_phy_preference pref,
		     bool fcs, bool stx);

/**
 * cfcnfg_del_phy_layer - Deletes an phy layer from the CAIF stack.
 *
 * @cnfg:	Pointer to a CAIF configuration object, created by
 *		cfcnfg_create().
 * @phy_layer:	Adaptation layer to be removed.
 */
int cfcnfg_del_phy_layer(struct cfcnfg *cnfg, struct cflayer *phy_layer);

/**
 * cfcnfg_disconn_adapt_layer - Disconnects an adaptation layer.
 *
 * @cnfg:	Pointer to a CAIF configuration object, created by
 *		cfcnfg_create().
 * @adap_layer: Adaptation layer to be removed.
 */
int cfcnfg_disconn_adapt_layer(struct cfcnfg *cnfg,
			struct cflayer *adap_layer);

/**
 * cfcnfg_release_adap_layer - Used by client to release the adaptation layer.
 *
 * @adap_layer: Adaptation layer.
 */
void cfcnfg_release_adap_layer(struct cflayer *adap_layer);

/**
 * cfcnfg_add_adaptation_layer - Add an adaptation layer to the CAIF stack.
 *
 * The adaptation Layer is where the interface to application or higher-level
 * driver functionality is implemented.
 *
 * @cnfg:		Pointer to a CAIF configuration object, created by
 *			cfcnfg_create().
 * @param:		Link setup parameters.
 * @adap_layer:		Specify the adaptation layer; the receive and
 *			flow-control functions MUST be set in the structure.
 * @ifindex:		Link layer interface index used for this connection.
 * @proto_head:		Protocol head-space needed by CAIF protocol,
 *			excluding link layer.
 * @proto_tail:		Protocol tail-space needed by CAIF protocol,
 *			excluding link layer.
 */
int cfcnfg_add_adaptation_layer(struct cfcnfg *cnfg,
			    struct cfctrl_link_param *param,
			    struct cflayer *adap_layer,
			    int *ifindex,
			    int *proto_head,
			    int *proto_tail);

/**
 * cfcnfg_get_phyid() - Get physical ID, given type.
 * Returns one of the physical interfaces matching the given type.
 * Zero if no match is found.
 * @cnfg:	Configuration object
 * @phy_pref:	Caif Link Layer preference
 */
struct dev_info *cfcnfg_get_phyid(struct cfcnfg *cnfg,
		     enum cfcnfg_phy_preference phy_pref);

/**
 * cfcnfg_get_id_from_ifi() - Get the Physical Identifier of ifindex,
 * 			it matches caif physical id with the kernel interface id.
 * @cnfg:	Configuration object
 * @ifi:	ifindex obtained from socket.c bindtodevice.
 */
int cfcnfg_get_id_from_ifi(struct cfcnfg *cnfg, int ifi);
#endif				/* CFCNFG_H_ */
