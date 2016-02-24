/*
 * ethtool.h: Defines for Linux ethtool.
 *
 * Copyright (C) 1998 David S. Miller (davem@redhat.com)
 * Copyright 2001 Jeff Garzik <jgarzik@pobox.com>
 * Portions Copyright 2001 Sun Microsystems (thockin@sun.com)
 * Portions Copyright 2002 Intel (eli.kupermann@intel.com,
 *                                christopher.leech@intel.com,
 *                                scott.feldman@intel.com)
 * Portions Copyright (C) Sun Microsystems 2008
 */
#ifndef _LINUX_ETHTOOL_H
#define _LINUX_ETHTOOL_H

#include <linux/bitmap.h>
#include <linux/compat.h>
#include <uapi/linux/ethtool.h>

#ifdef CONFIG_COMPAT

struct compat_ethtool_rx_flow_spec {
	u32		flow_type;
	union ethtool_flow_union h_u;
	struct ethtool_flow_ext h_ext;
	union ethtool_flow_union m_u;
	struct ethtool_flow_ext m_ext;
	compat_u64	ring_cookie;
	u32		location;
};

struct compat_ethtool_rxnfc {
	u32				cmd;
	u32				flow_type;
	compat_u64			data;
	struct compat_ethtool_rx_flow_spec fs;
	u32				rule_cnt;
	u32				rule_locs[0];
};

#endif /* CONFIG_COMPAT */

#include <linux/rculist.h>

/**
 * enum ethtool_phys_id_state - indicator state for physical identification
 * @ETHTOOL_ID_INACTIVE: Physical ID indicator should be deactivated
 * @ETHTOOL_ID_ACTIVE: Physical ID indicator should be activated
 * @ETHTOOL_ID_ON: LED should be turned on (used iff %ETHTOOL_ID_ACTIVE
 *	is not supported)
 * @ETHTOOL_ID_OFF: LED should be turned off (used iff %ETHTOOL_ID_ACTIVE
 *	is not supported)
 */
enum ethtool_phys_id_state {
	ETHTOOL_ID_INACTIVE,
	ETHTOOL_ID_ACTIVE,
	ETHTOOL_ID_ON,
	ETHTOOL_ID_OFF
};

enum {
	ETH_RSS_HASH_TOP_BIT, /* Configurable RSS hash function - Toeplitz */
	ETH_RSS_HASH_XOR_BIT, /* Configurable RSS hash function - Xor */

	/*
	 * Add your fresh new hash function bits above and remember to update
	 * rss_hash_func_strings[] in ethtool.c
	 */
	ETH_RSS_HASH_FUNCS_COUNT
};

#define __ETH_RSS_HASH_BIT(bit)	((u32)1 << (bit))
#define __ETH_RSS_HASH(name)	__ETH_RSS_HASH_BIT(ETH_RSS_HASH_##name##_BIT)

#define ETH_RSS_HASH_TOP	__ETH_RSS_HASH(TOP)
#define ETH_RSS_HASH_XOR	__ETH_RSS_HASH(XOR)

#define ETH_RSS_HASH_UNKNOWN	0
#define ETH_RSS_HASH_NO_CHANGE	0

struct net_device;

/* Some generic methods drivers may use in their ethtool_ops */
u32 ethtool_op_get_link(struct net_device *dev);
int ethtool_op_get_ts_info(struct net_device *dev, struct ethtool_ts_info *eti);

/**
 * ethtool_rxfh_indir_default - get default value for RX flow hash indirection
 * @index: Index in RX flow hash indirection table
 * @n_rx_rings: Number of RX rings to use
 *
 * This function provides the default policy for RX flow hash indirection.
 */
static inline u32 ethtool_rxfh_indir_default(u32 index, u32 n_rx_rings)
{
	return index % n_rx_rings;
}

/* number of link mode bits/ulongs handled internally by kernel */
#define __ETHTOOL_LINK_MODE_MASK_NBITS			\
	(__ETHTOOL_LINK_MODE_LAST + 1)

/* declare a link mode bitmap */
#define __ETHTOOL_DECLARE_LINK_MODE_MASK(name)		\
	DECLARE_BITMAP(name, __ETHTOOL_LINK_MODE_MASK_NBITS)

/* drivers must ignore base.cmd and base.link_mode_masks_nwords
 * fields, but they are allowed to overwrite them (will be ignored).
 */
struct ethtool_link_ksettings {
	struct ethtool_link_settings base;
	struct {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertising);
	} link_modes;
};

/**
 * ethtool_link_ksettings_zero_link_mode - clear link_ksettings link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 */
#define ethtool_link_ksettings_zero_link_mode(ptr, name)		\
	bitmap_zero((ptr)->link_modes.name, __ETHTOOL_LINK_MODE_MASK_NBITS)

/**
 * ethtool_link_ksettings_add_link_mode - set bit in link_ksettings
 * link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 *   @mode : one of the ETHTOOL_LINK_MODE_*_BIT
 * (not atomic, no bound checking)
 */
#define ethtool_link_ksettings_add_link_mode(ptr, name, mode)		\
	__set_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, (ptr)->link_modes.name)

/**
 * ethtool_link_ksettings_test_link_mode - test bit in ksettings link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 *   @mode : one of the ETHTOOL_LINK_MODE_*_BIT
 * (not atomic, no bound checking)
 *
 * Returns true/false.
 */
#define ethtool_link_ksettings_test_link_mode(ptr, name, mode)		\
	test_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, (ptr)->link_modes.name)

extern int
__ethtool_get_link_ksettings(struct net_device *dev,
			     struct ethtool_link_ksettings *link_ksettings);

/* DEPRECATED, use __ethtool_get_link_ksettings */
extern int __ethtool_get_settings(struct net_device *dev,
				  struct ethtool_cmd *cmd);

/**
 * struct ethtool_ops - optional netdev operations
 * @get_settings: DEPRECATED, use %get_link_ksettings/%set_link_ksettings
 *	API. Get various device settings including Ethernet link
 *	settings. The @cmd parameter is expected to have been cleared
 *	before get_settings is called. Returns a negative error code
 *	or zero.
 * @set_settings: DEPRECATED, use %get_link_ksettings/%set_link_ksettings
 *	API. Set various device settings including Ethernet link
 *	settings.  Returns a negative error code or zero.
 * @get_drvinfo: Report driver/device information.  Should only set the
 *	@driver, @version, @fw_version and @bus_info fields.  If not
 *	implemented, the @driver and @bus_info fields will be filled in
 *	according to the netdev's parent device.
 * @get_regs_len: Get buffer length required for @get_regs
 * @get_regs: Get device registers
 * @get_wol: Report whether Wake-on-Lan is enabled
 * @set_wol: Turn Wake-on-Lan on or off.  Returns a negative error code
 *	or zero.
 * @get_msglevel: Report driver message level.  This should be the value
 *	of the @msg_enable field used by netif logging functions.
 * @set_msglevel: Set driver message level
 * @nway_reset: Restart autonegotiation.  Returns a negative error code
 *	or zero.
 * @get_link: Report whether physical link is up.  Will only be called if
 *	the netdev is up.  Should usually be set to ethtool_op_get_link(),
 *	which uses netif_carrier_ok().
 * @get_eeprom: Read data from the device EEPROM.
 *	Should fill in the magic field.  Don't need to check len for zero
 *	or wraparound.  Fill in the data argument with the eeprom values
 *	from offset to offset + len.  Update len to the amount read.
 *	Returns an error or zero.
 * @set_eeprom: Write data to the device EEPROM.
 *	Should validate the magic field.  Don't need to check len for zero
 *	or wraparound.  Update len to the amount written.  Returns an error
 *	or zero.
 * @get_coalesce: Get interrupt coalescing parameters.  Returns a negative
 *	error code or zero.
 * @set_coalesce: Set interrupt coalescing parameters.  Returns a negative
 *	error code or zero.
 * @get_ringparam: Report ring sizes
 * @set_ringparam: Set ring sizes.  Returns a negative error code or zero.
 * @get_pauseparam: Report pause parameters
 * @set_pauseparam: Set pause parameters.  Returns a negative error code
 *	or zero.
 * @self_test: Run specified self-tests
 * @get_strings: Return a set of strings that describe the requested objects
 * @set_phys_id: Identify the physical devices, e.g. by flashing an LED
 *	attached to it.  The implementation may update the indicator
 *	asynchronously or synchronously, but in either case it must return
 *	quickly.  It is initially called with the argument %ETHTOOL_ID_ACTIVE,
 *	and must either activate asynchronous updates and return zero, return
 *	a negative error or return a positive frequency for synchronous
 *	indication (e.g. 1 for one on/off cycle per second).  If it returns
 *	a frequency then it will be called again at intervals with the
 *	argument %ETHTOOL_ID_ON or %ETHTOOL_ID_OFF and should set the state of
 *	the indicator accordingly.  Finally, it is called with the argument
 *	%ETHTOOL_ID_INACTIVE and must deactivate the indicator.  Returns a
 *	negative error code or zero.
 * @get_ethtool_stats: Return extended statistics about the device.
 *	This is only useful if the device maintains statistics not
 *	included in &struct rtnl_link_stats64.
 * @begin: Function to be called before any other operation.  Returns a
 *	negative error code or zero.
 * @complete: Function to be called after any other operation except
 *	@begin.  Will be called even if the other operation failed.
 * @get_priv_flags: Report driver-specific feature flags.
 * @set_priv_flags: Set driver-specific feature flags.  Returns a negative
 *	error code or zero.
 * @get_sset_count: Get number of strings that @get_strings will write.
 * @get_rxnfc: Get RX flow classification rules.  Returns a negative
 *	error code or zero.
 * @set_rxnfc: Set RX flow classification rules.  Returns a negative
 *	error code or zero.
 * @flash_device: Write a firmware image to device's flash memory.
 *	Returns a negative error code or zero.
 * @reset: Reset (part of) the device, as specified by a bitmask of
 *	flags from &enum ethtool_reset_flags.  Returns a negative
 *	error code or zero.
 * @get_rxfh_key_size: Get the size of the RX flow hash key.
 *	Returns zero if not supported for this specific device.
 * @get_rxfh_indir_size: Get the size of the RX flow hash indirection table.
 *	Returns zero if not supported for this specific device.
 * @get_rxfh: Get the contents of the RX flow hash indirection table, hash key
 *	and/or hash function.
 *	Returns a negative error code or zero.
 * @set_rxfh: Set the contents of the RX flow hash indirection table, hash
 *	key, and/or hash function.  Arguments which are set to %NULL or zero
 *	will remain unchanged.
 *	Returns a negative error code or zero. An error code must be returned
 *	if at least one unsupported change was requested.
 * @get_channels: Get number of channels.
 * @set_channels: Set number of channels.  Returns a negative error code or
 *	zero.
 * @get_dump_flag: Get dump flag indicating current dump length, version,
 * 		   and flag of the device.
 * @get_dump_data: Get dump data.
 * @set_dump: Set dump specific flags to the device.
 * @get_ts_info: Get the time stamping and PTP hardware clock capabilities.
 *	Drivers supporting transmit time stamps in software should set this to
 *	ethtool_op_get_ts_info().
 * @get_module_info: Get the size and type of the eeprom contained within
 *	a plug-in module.
 * @get_module_eeprom: Get the eeprom information from the plug-in module
 * @get_eee: Get Energy-Efficient (EEE) supported and status.
 * @set_eee: Set EEE status (enable/disable) as well as LPI timers.
 * @get_per_queue_coalesce: Get interrupt coalescing parameters per queue.
 *	It must check that the given queue number is valid. If neither a RX nor
 *	a TX queue has this number, return -EINVAL. If only a RX queue or a TX
 *	queue has this number, set the inapplicable fields to ~0 and return 0.
 *	Returns a negative error code or zero.
 * @set_per_queue_coalesce: Set interrupt coalescing parameters per queue.
 *	It must check that the given queue number is valid. If neither a RX nor
 *	a TX queue has this number, return -EINVAL. If only a RX queue or a TX
 *	queue has this number, ignore the inapplicable fields.
 *	Returns a negative error code or zero.
 * @get_link_ksettings: When defined, takes precedence over the
 *	%get_settings method. Get various device settings
 *	including Ethernet link settings. The %cmd and
 *	%link_mode_masks_nwords fields should be ignored (use
 *	%__ETHTOOL_LINK_MODE_MASK_NBITS instead of the latter), any
 *	change to them will be overwritten by kernel. Returns a
 *	negative error code or zero.
 * @set_link_ksettings: When defined, takes precedence over the
 *	%set_settings method. Set various device settings including
 *	Ethernet link settings. The %cmd and %link_mode_masks_nwords
 *	fields should be ignored (use %__ETHTOOL_LINK_MODE_MASK_NBITS
 *	instead of the latter), any change to them will be overwritten
 *	by kernel. Returns a negative error code or zero.
 *
 * All operations are optional (i.e. the function pointer may be set
 * to %NULL) and callers must take this into account.  Callers must
 * hold the RTNL lock.
 *
 * See the structures used by these operations for further documentation.
 * Note that for all operations using a structure ending with a zero-
 * length array, the array is allocated separately in the kernel and
 * is passed to the driver as an additional parameter.
 *
 * See &struct net_device and &struct net_device_ops for documentation
 * of the generic netdev features interface.
 */
struct ethtool_ops {
	int	(*get_settings)(struct net_device *, struct ethtool_cmd *);
	int	(*set_settings)(struct net_device *, struct ethtool_cmd *);
	void	(*get_drvinfo)(struct net_device *, struct ethtool_drvinfo *);
	int	(*get_regs_len)(struct net_device *);
	void	(*get_regs)(struct net_device *, struct ethtool_regs *, void *);
	void	(*get_wol)(struct net_device *, struct ethtool_wolinfo *);
	int	(*set_wol)(struct net_device *, struct ethtool_wolinfo *);
	u32	(*get_msglevel)(struct net_device *);
	void	(*set_msglevel)(struct net_device *, u32);
	int	(*nway_reset)(struct net_device *);
	u32	(*get_link)(struct net_device *);
	int	(*get_eeprom_len)(struct net_device *);
	int	(*get_eeprom)(struct net_device *,
			      struct ethtool_eeprom *, u8 *);
	int	(*set_eeprom)(struct net_device *,
			      struct ethtool_eeprom *, u8 *);
	int	(*get_coalesce)(struct net_device *, struct ethtool_coalesce *);
	int	(*set_coalesce)(struct net_device *, struct ethtool_coalesce *);
	void	(*get_ringparam)(struct net_device *,
				 struct ethtool_ringparam *);
	int	(*set_ringparam)(struct net_device *,
				 struct ethtool_ringparam *);
	void	(*get_pauseparam)(struct net_device *,
				  struct ethtool_pauseparam*);
	int	(*set_pauseparam)(struct net_device *,
				  struct ethtool_pauseparam*);
	void	(*self_test)(struct net_device *, struct ethtool_test *, u64 *);
	void	(*get_strings)(struct net_device *, u32 stringset, u8 *);
	int	(*set_phys_id)(struct net_device *, enum ethtool_phys_id_state);
	void	(*get_ethtool_stats)(struct net_device *,
				     struct ethtool_stats *, u64 *);
	int	(*begin)(struct net_device *);
	void	(*complete)(struct net_device *);
	u32	(*get_priv_flags)(struct net_device *);
	int	(*set_priv_flags)(struct net_device *, u32);
	int	(*get_sset_count)(struct net_device *, int);
	int	(*get_rxnfc)(struct net_device *,
			     struct ethtool_rxnfc *, u32 *rule_locs);
	int	(*set_rxnfc)(struct net_device *, struct ethtool_rxnfc *);
	int	(*flash_device)(struct net_device *, struct ethtool_flash *);
	int	(*reset)(struct net_device *, u32 *);
	u32	(*get_rxfh_key_size)(struct net_device *);
	u32	(*get_rxfh_indir_size)(struct net_device *);
	int	(*get_rxfh)(struct net_device *, u32 *indir, u8 *key,
			    u8 *hfunc);
	int	(*set_rxfh)(struct net_device *, const u32 *indir,
			    const u8 *key, const u8 hfunc);
	void	(*get_channels)(struct net_device *, struct ethtool_channels *);
	int	(*set_channels)(struct net_device *, struct ethtool_channels *);
	int	(*get_dump_flag)(struct net_device *, struct ethtool_dump *);
	int	(*get_dump_data)(struct net_device *,
				 struct ethtool_dump *, void *);
	int	(*set_dump)(struct net_device *, struct ethtool_dump *);
	int	(*get_ts_info)(struct net_device *, struct ethtool_ts_info *);
	int     (*get_module_info)(struct net_device *,
				   struct ethtool_modinfo *);
	int     (*get_module_eeprom)(struct net_device *,
				     struct ethtool_eeprom *, u8 *);
	int	(*get_eee)(struct net_device *, struct ethtool_eee *);
	int	(*set_eee)(struct net_device *, struct ethtool_eee *);
	int	(*get_tunable)(struct net_device *,
			       const struct ethtool_tunable *, void *);
	int	(*set_tunable)(struct net_device *,
			       const struct ethtool_tunable *, const void *);
	int	(*get_per_queue_coalesce)(struct net_device *, u32,
					  struct ethtool_coalesce *);
	int	(*set_per_queue_coalesce)(struct net_device *, u32,
					  struct ethtool_coalesce *);
	int	(*get_link_ksettings)(struct net_device *,
				      struct ethtool_link_ksettings *);
	int	(*set_link_ksettings)(struct net_device *,
				      const struct ethtool_link_ksettings *);
};
#endif /* _LINUX_ETHTOOL_H */
