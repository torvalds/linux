/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __PHY_PORT_H
#define __PHY_PORT_H

#include <linux/ethtool.h>
#include <linux/types.h>
#include <linux/phy.h>

struct phy_port;

/**
 * enum phy_port_parent - The device this port is attached to
 *
 * @PHY_PORT_PHY: Indicates that the port is driven by a PHY device
 */
enum phy_port_parent {
	PHY_PORT_PHY,
};

struct phy_port_ops {
	/* Sometimes, the link state can be retrieved from physical,
	 * out-of-band channels such as the LOS signal on SFP. These
	 * callbacks allows notifying the port about state changes
	 */
	void (*link_up)(struct phy_port *port);
	void (*link_down)(struct phy_port *port);

	/* If the port acts as a Media Independent Interface (Serdes port),
	 * configures the port with the relevant state and mode. When enable is
	 * not set, interface should be ignored
	 */
	int (*configure_mii)(struct phy_port *port, bool enable, phy_interface_t interface);
};

/**
 * struct phy_port - A representation of a network device physical interface
 *
 * @head: Used by the port's parent to list ports
 * @parent_type: The type of device this port is directly connected to
 * @phy: If the parent is PHY_PORT_PHYDEV, the PHY controlling that port
 * @ops: Callback ops implemented by the port controller
 * @pairs: The number of  pairs this port has, 0 if not applicable
 * @mediums: Bitmask of the physical mediums this port provides access to
 * @supported: The link modes this port can expose, if this port is MDI (not MII)
 * @interfaces: The MII interfaces this port supports, if this port is MII
 * @not_described: Indicates to the parent driver if this port isn't described,
 *		   so it's up to the parent to filter its capabilities.
 * @active: Indicates if the port is currently part of the active link.
 * @is_mii: Indicates if this port is MII (Media Independent Interface),
 *          or MDI (Media Dependent Interface).
 * @is_sfp: Indicates if this port drives an SFP cage.
 */
struct phy_port {
	struct list_head head;
	enum phy_port_parent parent_type;
	union {
		struct phy_device *phy;
	};

	const struct phy_port_ops *ops;

	int pairs;
	unsigned long mediums;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	DECLARE_PHY_INTERFACE_MASK(interfaces);

	unsigned int not_described:1;
	unsigned int active:1;
	unsigned int is_mii:1;
	unsigned int is_sfp:1;
};

struct phy_port *phy_port_alloc(void);
void phy_port_destroy(struct phy_port *port);

static inline struct phy_device *port_phydev(struct phy_port *port)
{
	return port->phy;
}

struct phy_port *phy_of_parse_port(struct device_node *dn);

static inline bool phy_port_is_copper(struct phy_port *port)
{
	return port->mediums == BIT(ETHTOOL_LINK_MEDIUM_BASET);
}

static inline bool phy_port_is_fiber(struct phy_port *port)
{
	return !!(port->mediums & ETHTOOL_MEDIUM_FIBER_BITS);
}

void phy_port_update_supported(struct phy_port *port);
int phy_port_restrict_mediums(struct phy_port *port, unsigned long mediums);

int phy_port_get_type(struct phy_port *port);

#endif
