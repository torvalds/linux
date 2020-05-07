#ifndef NETDEV_PCS_H
#define NETDEV_PCS_H

#include <linux/phy.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct device_node;
struct ethtool_cmd;
struct fwnode_handle;
struct net_device;

enum {
	MLO_PAUSE_NONE,
	MLO_PAUSE_RX = BIT(0),
	MLO_PAUSE_TX = BIT(1),
	MLO_PAUSE_TXRX_MASK = MLO_PAUSE_TX | MLO_PAUSE_RX,
	MLO_PAUSE_AN = BIT(2),

	MLO_AN_PHY = 0,	/* Conventional PHY */
	MLO_AN_FIXED,	/* Fixed-link mode */
	MLO_AN_INBAND,	/* In-band protocol */
};

static inline bool phylink_autoneg_inband(unsigned int mode)
{
	return mode == MLO_AN_INBAND;
}

/**
 * struct phylink_link_state - link state structure
 * @advertising: ethtool bitmask containing advertised link modes
 * @lp_advertising: ethtool bitmask containing link partner advertised link
 *   modes
 * @interface: link &typedef phy_interface_t mode
 * @speed: link speed, one of the SPEED_* constants.
 * @duplex: link duplex mode, one of DUPLEX_* constants.
 * @pause: link pause state, described by MLO_PAUSE_* constants.
 * @link: true if the link is up.
 * @an_enabled: true if autonegotiation is enabled/desired.
 * @an_complete: true if autonegotiation has completed.
 */
struct phylink_link_state {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertising);
	phy_interface_t interface;
	int speed;
	int duplex;
	int pause;
	unsigned int link:1;
	unsigned int an_enabled:1;
	unsigned int an_complete:1;
};

enum phylink_op_type {
	PHYLINK_NETDEV = 0,
	PHYLINK_DEV,
};

/**
 * struct phylink_config - PHYLINK configuration structure
 * @dev: a pointer to a struct device associated with the MAC
 * @type: operation type of PHYLINK instance
 * @pcs_poll: MAC PCS cannot provide link change interrupt
 */
struct phylink_config {
	struct device *dev;
	enum phylink_op_type type;
	bool pcs_poll;
};

/**
 * struct phylink_mac_ops - MAC operations structure.
 * @validate: Validate and update the link configuration.
 * @mac_pcs_get_state: Read the current link state from the hardware.
 * @mac_config: configure the MAC for the selected mode and state.
 * @mac_an_restart: restart 802.3z BaseX autonegotiation.
 * @mac_link_down: take the link down.
 * @mac_link_up: allow the link to come up.
 *
 * The individual methods are described more fully below.
 */
struct phylink_mac_ops {
	void (*validate)(struct phylink_config *config,
			 unsigned long *supported,
			 struct phylink_link_state *state);
	void (*mac_pcs_get_state)(struct phylink_config *config,
				  struct phylink_link_state *state);
	void (*mac_config)(struct phylink_config *config, unsigned int mode,
			   const struct phylink_link_state *state);
	void (*mac_an_restart)(struct phylink_config *config);
	void (*mac_link_down)(struct phylink_config *config, unsigned int mode,
			      phy_interface_t interface);
	void (*mac_link_up)(struct phylink_config *config,
			    struct phy_device *phy, unsigned int mode,
			    phy_interface_t interface, int speed, int duplex,
			    bool tx_pause, bool rx_pause);
};

#if 0 /* For kernel-doc purposes only. */
/**
 * validate - Validate and update the link configuration
 * @config: a pointer to a &struct phylink_config.
 * @supported: ethtool bitmask for supported link modes.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Clear bits in the @supported and @state->advertising masks that
 * are not supportable by the MAC.
 *
 * Note that the PHY may be able to transform from one connection
 * technology to another, so, eg, don't clear 1000BaseX just
 * because the MAC is unable to BaseX mode. This is more about
 * clearing unsupported speeds and duplex settings. The port modes
 * should not be cleared; phylink_set_port_modes() will help with this.
 *
 * If the @state->interface mode is %PHY_INTERFACE_MODE_1000BASEX
 * or %PHY_INTERFACE_MODE_2500BASEX, select the appropriate mode
 * based on @state->advertising and/or @state->speed and update
 * @state->interface accordingly. See phylink_helper_basex_speed().
 *
 * When @state->interface is %PHY_INTERFACE_MODE_NA, phylink expects the
 * MAC driver to return all supported link modes.
 *
 * If the @state->interface mode is not supported, then the @supported
 * mask must be cleared.
 */
void validate(struct phylink_config *config, unsigned long *supported,
	      struct phylink_link_state *state);

/**
 * mac_pcs_get_state() - Read the current inband link state from the hardware
 * @config: a pointer to a &struct phylink_config.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Read the current inband link state from the MAC PCS, reporting the
 * current speed in @state->speed, duplex mode in @state->duplex, pause
 * mode in @state->pause using the %MLO_PAUSE_RX and %MLO_PAUSE_TX bits,
 * negotiation completion state in @state->an_complete, and link up state
 * in @state->link. If possible, @state->lp_advertising should also be
 * populated.
 */
void mac_pcs_get_state(struct phylink_config *config,
		       struct phylink_link_state *state);

/**
 * mac_config() - configure the MAC for the selected mode and state
 * @config: a pointer to a &struct phylink_config.
 * @mode: one of %MLO_AN_FIXED, %MLO_AN_PHY, %MLO_AN_INBAND.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Note - not all members of @state are valid.  In particular,
 * @state->lp_advertising, @state->link, @state->an_complete are never
 * guaranteed to be correct, and so any mac_config() implementation must
 * never reference these fields.
 *
 * (this requires a rewrite - please refer to mac_link_up() for situations
 *  where the PCS and MAC are not tightly integrated.)
 *
 * In all negotiation modes, as defined by @mode, @state->pause indicates the
 * pause settings which should be applied as follows. If %MLO_PAUSE_AN is not
 * set, %MLO_PAUSE_TX and %MLO_PAUSE_RX indicate whether the MAC should send
 * pause frames and/or act on received pause frames respectively. Otherwise,
 * the results of in-band negotiation/status from the MAC PCS should be used
 * to control the MAC pause mode settings.
 *
 * The action performed depends on the currently selected mode:
 *
 * %MLO_AN_FIXED, %MLO_AN_PHY:
 *   Configure for non-inband negotiation mode, where the link settings
 *   are completely communicated via mac_link_up().  The physical link
 *   protocol from the MAC is specified by @state->interface.
 *
 *   @state->advertising may be used, but is not required.
 *
 *   Older drivers (prior to the mac_link_up() change) may use @state->speed,
 *   @state->duplex and @state->pause to configure the MAC, but this is
 *   deprecated; such drivers should be converted to use mac_link_up().
 *
 *   Other members of @state must be ignored.
 *
 *   Valid state members: interface, advertising.
 *   Deprecated state members: speed, duplex, pause.
 *
 * %MLO_AN_INBAND:
 *   place the link in an inband negotiation mode (such as 802.3z
 *   1000base-X or Cisco SGMII mode depending on the @state->interface
 *   mode). In both cases, link state management (whether the link
 *   is up or not) is performed by the MAC, and reported via the
 *   mac_pcs_get_state() callback. Changes in link state must be made
 *   by calling phylink_mac_change().
 *
 *   Interface mode specific details are mentioned below.
 *
 *   If in 802.3z mode, the link speed is fixed, dependent on the
 *   @state->interface. Duplex and pause modes are negotiated via
 *   the in-band configuration word. Advertised pause modes are set
 *   according to the @state->an_enabled and @state->advertising
 *   flags. Beware of MACs which only support full duplex at gigabit
 *   and higher speeds.
 *
 *   If in Cisco SGMII mode, the link speed and duplex mode are passed
 *   in the serial bitstream 16-bit configuration word, and the MAC
 *   should be configured to read these bits and acknowledge the
 *   configuration word. Nothing is advertised by the MAC. The MAC is
 *   responsible for reading the configuration word and configuring
 *   itself accordingly.
 *
 *   Valid state members: interface, an_enabled, pause, advertising.
 *
 * Implementations are expected to update the MAC to reflect the
 * requested settings - i.o.w., if nothing has changed between two
 * calls, no action is expected.  If only flow control settings have
 * changed, flow control should be updated *without* taking the link
 * down.  This "update" behaviour is critical to avoid bouncing the
 * link up status.
 */
void mac_config(struct phylink_config *config, unsigned int mode,
		const struct phylink_link_state *state);

/**
 * mac_an_restart() - restart 802.3z BaseX autonegotiation
 * @config: a pointer to a &struct phylink_config.
 */
void mac_an_restart(struct phylink_config *config);

/**
 * mac_link_down() - take the link down
 * @config: a pointer to a &struct phylink_config.
 * @mode: link autonegotiation mode
 * @interface: link &typedef phy_interface_t mode
 *
 * If @mode is not an in-band negotiation mode (as defined by
 * phylink_autoneg_inband()), force the link down and disable any
 * Energy Efficient Ethernet MAC configuration. Interface type
 * selection must be done in mac_config().
 */
void mac_link_down(struct phylink_config *config, unsigned int mode,
		   phy_interface_t interface);

/**
 * mac_link_up() - allow the link to come up
 * @config: a pointer to a &struct phylink_config.
 * @phy: any attached phy
 * @mode: link autonegotiation mode
 * @interface: link &typedef phy_interface_t mode
 * @speed: link speed
 * @duplex: link duplex
 * @tx_pause: link transmit pause enablement status
 * @rx_pause: link receive pause enablement status
 *
 * Configure the MAC for an established link.
 *
 * @speed, @duplex, @tx_pause and @rx_pause indicate the finalised link
 * settings, and should be used to configure the MAC block appropriately
 * where these settings are not automatically conveyed from the PCS block,
 * or if in-band negotiation (as defined by phylink_autoneg_inband(@mode))
 * is disabled.
 *
 * Note that when 802.3z in-band negotiation is in use, it is possible
 * that the user wishes to override the pause settings, and this should
 * be allowed when considering the implementation of this method.
 *
 * If in-band negotiation mode is disabled, allow the link to come up. If
 * @phy is non-%NULL, configure Energy Efficient Ethernet by calling
 * phy_init_eee() and perform appropriate MAC configuration for EEE.
 * Interface type selection must be done in mac_config().
 */
void mac_link_up(struct phylink_config *config, struct phy_device *phy,
		 unsigned int mode, phy_interface_t interface,
		 int speed, int duplex, bool tx_pause, bool rx_pause);
#endif

/**
 * struct phylink_pcs_ops - MAC PCS operations structure.
 * @pcs_get_state: read the current MAC PCS link state from the hardware.
 * @pcs_config: configure the MAC PCS for the selected mode and state.
 * @pcs_an_restart: restart 802.3z BaseX autonegotiation.
 * @pcs_link_up: program the PCS for the resolved link configuration
 *               (where necessary).
 */
struct phylink_pcs_ops {
	void (*pcs_get_state)(struct phylink_config *config,
			      struct phylink_link_state *state);
	int (*pcs_config)(struct phylink_config *config, unsigned int mode,
			  phy_interface_t interface,
			  const unsigned long *advertising);
	void (*pcs_an_restart)(struct phylink_config *config);
	void (*pcs_link_up)(struct phylink_config *config, unsigned int mode,
			    phy_interface_t interface, int speed, int duplex);
};

#if 0 /* For kernel-doc purposes only. */
/**
 * pcs_get_state() - Read the current inband link state from the hardware
 * @config: a pointer to a &struct phylink_config.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Read the current inband link state from the MAC PCS, reporting the
 * current speed in @state->speed, duplex mode in @state->duplex, pause
 * mode in @state->pause using the %MLO_PAUSE_RX and %MLO_PAUSE_TX bits,
 * negotiation completion state in @state->an_complete, and link up state
 * in @state->link. If possible, @state->lp_advertising should also be
 * populated.
 *
 * When present, this overrides mac_pcs_get_state() in &struct
 * phylink_mac_ops.
 */
void pcs_get_state(struct phylink_config *config,
		   struct phylink_link_state *state);

/**
 * pcs_config() - Configure the PCS mode and advertisement
 * @config: a pointer to a &struct phylink_config.
 * @mode: one of %MLO_AN_FIXED, %MLO_AN_PHY, %MLO_AN_INBAND.
 * @interface: interface mode to be used
 * @advertising: adertisement ethtool link mode mask
 *
 * Configure the PCS for the operating mode, the interface mode, and set
 * the advertisement mask.
 *
 * When operating in %MLO_AN_INBAND, inband should always be enabled,
 * otherwise inband should be disabled.
 *
 * For SGMII, there is no advertisement from the MAC side, the PCS should
 * be programmed to acknowledge the inband word from the PHY.
 *
 * For 1000BASE-X, the advertisement should be programmed into the PCS.
 *
 * For most 10GBASE-R, there is no advertisement.
 */
int (*pcs_config)(struct phylink_config *config, unsigned int mode,
		  phy_interface_t interface, const unsigned long *advertising);

/**
 * pcs_an_restart() - restart 802.3z BaseX autonegotiation
 * @config: a pointer to a &struct phylink_config.
 *
 * When PCS ops are present, this overrides mac_an_restart() in &struct
 * phylink_mac_ops.
 */
void (*pcs_an_restart)(struct phylink_config *config);

/**
 * pcs_link_up() - program the PCS for the resolved link configuration
 * @config: a pointer to a &struct phylink_config.
 * @mode: link autonegotiation mode
 * @interface: link &typedef phy_interface_t mode
 * @speed: link speed
 * @duplex: link duplex
 *
 * This call will be made just before mac_link_up() to inform the PCS of
 * the resolved link parameters. For example, a PCS operating in SGMII
 * mode without in-band AN needs to be manually configured for the link
 * and duplex setting. Otherwise, this should be a no-op.
 */
void (*pcs_link_up)(struct phylink_config *config, unsigned int mode,
		    phy_interface_t interface, int speed, int duplex);
#endif

struct phylink *phylink_create(struct phylink_config *, struct fwnode_handle *,
			       phy_interface_t iface,
			       const struct phylink_mac_ops *mac_ops);
void phylink_add_pcs(struct phylink *, const struct phylink_pcs_ops *ops);
void phylink_destroy(struct phylink *);

int phylink_connect_phy(struct phylink *, struct phy_device *);
int phylink_of_phy_connect(struct phylink *, struct device_node *, u32 flags);
void phylink_disconnect_phy(struct phylink *);
int phylink_fixed_state_cb(struct phylink *,
			   void (*cb)(struct net_device *dev,
				      struct phylink_link_state *));

void phylink_mac_change(struct phylink *, bool up);

void phylink_start(struct phylink *);
void phylink_stop(struct phylink *);

void phylink_ethtool_get_wol(struct phylink *, struct ethtool_wolinfo *);
int phylink_ethtool_set_wol(struct phylink *, struct ethtool_wolinfo *);

int phylink_ethtool_ksettings_get(struct phylink *,
				  struct ethtool_link_ksettings *);
int phylink_ethtool_ksettings_set(struct phylink *,
				  const struct ethtool_link_ksettings *);
int phylink_ethtool_nway_reset(struct phylink *);
void phylink_ethtool_get_pauseparam(struct phylink *,
				    struct ethtool_pauseparam *);
int phylink_ethtool_set_pauseparam(struct phylink *,
				   struct ethtool_pauseparam *);
int phylink_get_eee_err(struct phylink *);
int phylink_init_eee(struct phylink *, bool);
int phylink_ethtool_get_eee(struct phylink *, struct ethtool_eee *);
int phylink_ethtool_set_eee(struct phylink *, struct ethtool_eee *);
int phylink_mii_ioctl(struct phylink *, struct ifreq *, int);

#define phylink_zero(bm) \
	bitmap_zero(bm, __ETHTOOL_LINK_MODE_MASK_NBITS)
#define __phylink_do_bit(op, bm, mode) \
	op(ETHTOOL_LINK_MODE_ ## mode ## _BIT, bm)

#define phylink_set(bm, mode)	__phylink_do_bit(__set_bit, bm, mode)
#define phylink_clear(bm, mode)	__phylink_do_bit(__clear_bit, bm, mode)
#define phylink_test(bm, mode)	__phylink_do_bit(test_bit, bm, mode)

void phylink_set_port_modes(unsigned long *bits);
void phylink_helper_basex_speed(struct phylink_link_state *state);

void phylink_mii_c22_pcs_get_state(struct mdio_device *pcs,
				   struct phylink_link_state *state);
int phylink_mii_c22_pcs_set_advertisement(struct mdio_device *pcs,
					  phy_interface_t interface,
					  const unsigned long *advertising);
void phylink_mii_c22_pcs_an_restart(struct mdio_device *pcs);

void phylink_mii_c45_pcs_get_state(struct mdio_device *pcs,
				   struct phylink_link_state *state);
#endif
