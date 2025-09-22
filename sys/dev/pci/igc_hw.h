/*	$OpenBSD: igc_hw.h,v 1.3 2024/05/13 01:22:47 jsg Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

#ifndef _IGC_HW_H_
#define _IGC_HW_H_

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/intrmap.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/igc_base.h>
#include <dev/pci/igc_defines.h>
#include <dev/pci/igc_i225.h>
#include <dev/pci/igc_mac.h>
#include <dev/pci/igc_nvm.h>
#include <dev/pci/igc_phy.h>
#include <dev/pci/igc_regs.h>

struct igc_hw;

#define IGC_FUNC_1	1

#define IGC_ALT_MAC_ADDRESS_OFFSET_LAN0	0
#define IGC_ALT_MAC_ADDRESS_OFFSET_LAN1	3

enum igc_mac_type {
	igc_undefined = 0,
	igc_i225,
	igc_num_macs	/* List is 1-based, so subtract 1 for TRUE count. */
};

enum igc_media_type {
	igc_media_type_unknown = 0,
	igc_media_type_copper = 1,
	igc_num_media_types
};

enum igc_nvm_type {
	igc_nvm_unknown = 0,
	igc_nvm_eeprom_spi,
	igc_nvm_flash_hw,
	igc_nvm_invm
};

enum igc_phy_type {
	igc_phy_unknown = 0,
	igc_phy_none,
	igc_phy_i225
};

enum igc_bus_type {
	igc_bus_type_unknown = 0,
	igc_bus_type_pci,
	igc_bus_type_pcix,
	igc_bus_type_pci_express,
	igc_bus_type_reserved
};

enum igc_bus_speed {
	igc_bus_speed_unknown = 0,
	igc_bus_speed_33,
	igc_bus_speed_66,
	igc_bus_speed_100,
	igc_bus_speed_120,
	igc_bus_speed_133,
	igc_bus_speed_2500,
	igc_bus_speed_5000,
	igc_bus_speed_reserved
};

enum igc_bus_width {
	igc_bus_width_unknown = 0,
	igc_bus_width_pcie_x1,
	igc_bus_width_pcie_x2,
	igc_bus_width_pcie_x4 = 4,
	igc_bus_width_pcie_x8 = 8,
	igc_bus_width_32,
	igc_bus_width_64,
	igc_bus_width_reserved
};

enum igc_fc_mode {
	igc_fc_none = 0,
	igc_fc_rx_pause,
	igc_fc_tx_pause,
	igc_fc_full,
	igc_fc_default = 0xFF
};

enum igc_ms_type {
	igc_ms_hw_default = 0,
	igc_ms_force_master,
	igc_ms_force_slave,
	igc_ms_auto
};

enum igc_smart_speed {
	igc_smart_speed_default = 0,
	igc_smart_speed_on,
	igc_smart_speed_off
};

/* Receive Descriptor */
struct igc_rx_desc {
	uint64_t buffer_addr;	/* Address of the descriptor's data buffer */
	uint64_t length;	/* Length of data DMAed into data buffer */
	uint16_t csum;		/* Packet checksum */
	uint8_t  status;	/* Descriptor status */
	uint8_t  errors;	/* Descriptor errors */
	uint16_t special;
};

/* Receive Descriptor - Extended */
union igc_rx_desc_extended {
	struct {
		uint64_t buffer_addr;
		uint64_t reserved;
	} read;
	struct {
		struct {
			uint32_t mrq;	/* Multiple Rx queues */
			union {
				uint32_t rss;	/* RSS hash */
				struct {
					uint16_t ip_id;	/* IP id */
					uint16_t csum;	/* Packet checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			uint32_t status_error;	/* ext status/error */
			uint16_t length;
			uint16_t vlan;	/* VLAN tag */
		} upper;
	} wb;	/* writeback */
};

/* Transmit Descriptor */
struct igc_tx_desc {
	uint64_t buffer_addr;	/* Address of the descriptor's data buffer */
	union {
		uint32_t data;
		struct {
			uint16_t length;	/* Data buffer length */
			uint8_t cso;	/* Checksum offset */
			uint8_t cmd;	/* Descriptor control */
		} flags;
	} lower;
	union {
		uint32_t data;
		struct {
			uint8_t status;	/* Descriptor status */
			uint8_t css;	/* Checksum start */
			uint16_t special;
		} fields;
	} upper;
};

/* Function pointers for the MAC. */
struct igc_mac_operations {
	int	(*init_params)(struct igc_hw *);
	int	(*check_for_link)(struct igc_hw *);
	void	(*clear_hw_cntrs)(struct igc_hw *);
	void	(*clear_vfta)(struct igc_hw *);
	int	(*get_bus_info)(struct igc_hw *);
	void	(*set_lan_id)(struct igc_hw *);
	int	(*get_link_up_info)(struct igc_hw *, uint16_t *, uint16_t *);
	void	(*update_mc_addr_list)(struct igc_hw *, uint8_t *, uint32_t);
	int	(*reset_hw)(struct igc_hw *);
	int	(*init_hw)(struct igc_hw *);
	int	(*setup_link)(struct igc_hw *);
	int	(*setup_physical_interface)(struct igc_hw *);
	void	(*write_vfta)(struct igc_hw *, uint32_t, uint32_t);
	void	(*config_collision_dist)(struct igc_hw *);
	int	(*rar_set)(struct igc_hw *, uint8_t *, uint32_t);
	int	(*read_mac_addr)(struct igc_hw *);
	int	(*validate_mdi_setting)(struct igc_hw *);
	int	(*acquire_swfw_sync)(struct igc_hw *, uint16_t);
	void	(*release_swfw_sync)(struct igc_hw *, uint16_t);
};

/* When to use various PHY register access functions:
 *
 *                 Func   Caller
 *   Function      Does   Does    When to use
 *   ~~~~~~~~~~~~  ~~~~~  ~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *   X_reg         L,P,A  n/a     for simple PHY reg accesses
 *   X_reg_locked  P,A    L       for multiple accesses of different regs
 *                                on different pages
 *   X_reg_page    A      L,P     for multiple accesses of different regs
 *                                on the same page
 *
 * Where X=[read|write], L=locking, P=sets page, A=register access
 *
 */
struct igc_phy_operations {
	int	(*init_params)(struct igc_hw *);
	int	(*acquire)(struct igc_hw *);
	int	(*check_reset_block)(struct igc_hw *);
	int	(*force_speed_duplex)(struct igc_hw *);
	int	(*get_info)(struct igc_hw *);
	int	(*set_page)(struct igc_hw *, uint16_t);
	int	(*read_reg)(struct igc_hw *, uint32_t, uint16_t *);
	int	(*read_reg_locked)(struct igc_hw *, uint32_t, uint16_t *);
	int	(*read_reg_page)(struct igc_hw *, uint32_t, uint16_t *);
	void	(*release)(struct igc_hw *);
	int	(*reset)(struct igc_hw *);
	int	(*set_d0_lplu_state)(struct igc_hw *, bool);
	int	(*set_d3_lplu_state)(struct igc_hw *, bool);
	int	(*write_reg)(struct igc_hw *, uint32_t, uint16_t);
	int	(*write_reg_locked)(struct igc_hw *, uint32_t, uint16_t);
	int	(*write_reg_page)(struct igc_hw *, uint32_t, uint16_t);
	void	(*power_up)(struct igc_hw *);
	void	(*power_down)(struct igc_hw *);
};

/* Function pointers for the NVM. */
struct igc_nvm_operations {
	int	(*init_params)(struct igc_hw *);
	int	(*acquire)(struct igc_hw *);
	int	(*read)(struct igc_hw *, uint16_t, uint16_t, uint16_t *);
	void	(*release)(struct igc_hw *);
	void	(*reload)(struct igc_hw *);
	int	(*update)(struct igc_hw *);
	int	(*validate)(struct igc_hw *);
	int	(*write)(struct igc_hw *, uint16_t, uint16_t, uint16_t *);
};

struct igc_mac_info {
	struct igc_mac_operations	ops;
	uint8_t				addr[ETHER_ADDR_LEN];
	uint8_t				perm_addr[ETHER_ADDR_LEN];

	enum igc_mac_type		type;

	uint32_t			mc_filter_type;

	uint16_t			current_ifs_val;
	uint16_t			ifs_max_val;
	uint16_t			ifs_min_val;
	uint16_t			ifs_ratio;
	uint16_t			ifs_step_size;
	uint16_t			mta_reg_count;
	uint16_t			uta_reg_count;

	/* Maximum size of the MTA register table in all supported adapters */
#define MAX_MTA_REG	128
	uint32_t			mta_shadow[MAX_MTA_REG];
	uint16_t			rar_entry_count;

	uint8_t				forced_speed_duplex;

	bool				asf_firmware_present;
	bool				autoneg;
	bool				get_link_status;
	uint32_t			max_frame_size;
};

struct igc_phy_info {
	struct igc_phy_operations	ops;
	enum igc_phy_type		type;

	enum igc_smart_speed		smart_speed;

	uint32_t			addr;
	uint32_t			id;
	uint32_t			reset_delay_us;	/* in usec */
	uint32_t			revision;

	enum igc_media_type		media_type;

	uint16_t			autoneg_advertised;
	uint16_t			autoneg_mask;

	uint8_t				mdix;

	bool				polarity_correction;
	bool				speed_downgraded;
	bool				autoneg_wait_to_complete;
};

struct igc_nvm_info {
	struct igc_nvm_operations	ops;
	enum igc_nvm_type		type;

	uint16_t			word_size;
	uint16_t			delay_usec;
	uint16_t			address_bits;
	uint16_t			opcode_bits;
	uint16_t			page_size;
};

struct igc_bus_info {
	enum igc_bus_type	type;
	enum igc_bus_speed	speed;
	enum igc_bus_width	width;

	uint16_t		func;
	uint16_t		pci_cmd_word;
};

struct igc_fc_info {
	uint32_t	high_water;
	uint32_t	low_water;
	uint16_t	pause_time;
	uint16_t	refresh_time;
	bool		send_xon;
	bool		strict_ieee;
	enum		igc_fc_mode current_mode;
	enum		igc_fc_mode requested_mode;
};

struct igc_dev_spec_i225 {
	bool		eee_disable;
	bool		clear_semaphore_once;
	uint32_t	mtu;
};

struct igc_hw {
	void			*back;

	uint8_t			*hw_addr;

	struct igc_mac_info	mac;
	struct igc_fc_info	fc;
	struct igc_phy_info	phy;
	struct igc_nvm_info	nvm;
	struct igc_bus_info	bus;

	union {
		struct igc_dev_spec_i225 _i225;
	} dev_spec;

	uint16_t		device_id;
};

#endif	/* _IGC_HW_H_ */
