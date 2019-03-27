/*-
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2016 Avago Technologies
 * Copyright 2000-2020 Broadcom Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

#ifndef _MPR_MAPPING_H
#define _MPR_MAPPING_H

/**
 * struct _map_phy_change - PHY entries received in Topology change list
 * @physical_id: SAS address of the device attached with the associate PHY
 * @device_info: bitfield provides detailed info about the device
 * @dev_handle: device handle for the device pointed by this entry
 * @slot: slot ID
 * @is_processed: Flag to indicate whether this entry is processed or not
 * @is_SATA_SSD: 1 if this is a SATA device AND an SSD, 0 otherwise
 */
struct _map_phy_change {
	uint64_t	physical_id;
	uint32_t	device_info;
	uint16_t	dev_handle;
	uint16_t	slot;
	uint8_t	reason;
	uint8_t	is_processed;
	uint8_t	is_SATA_SSD;
	uint8_t reserved;
};

/**
 * struct _map_port_change - PCIe Port entries received in PCIe Topology change
 * list event
 * @physical_id: WWID of the device attached to the associated port
 * @device_info: bitfield provides detailed info about the device
 * @MDTS: Maximum Data Transfer Size for the device
 * @dev_handle: device handle for the device pointed by this entry
 * @slot: slot ID
 * @is_processed: Flag to indicate whether this entry is processed or not
 */
struct _map_port_change {
	uint64_t	physical_id;
	uint32_t	device_info;
	uint32_t	MDTS;
	uint16_t	dev_handle;
	uint16_t	slot;
	uint8_t		reason;
	uint8_t		is_processed;
	uint8_t		reserved[2];
};

/**
 * struct _map_topology_change - SAS/SATA entries to be removed from mapping
 * table
 * @enc_handle: enclosure handle where this device is located
 * @exp_handle: expander handle where this device is located
 * @num_entries: number of entries in the SAS Topology Change List event
 * @start_phy_num: PHY number of the first PHY in the event data
 * @num_phys: number of PHYs in the expander where this device is located
 * @exp_status: status for the expander where this device is located
 * @phy_details: more details about each PHY in the event data
 */
struct _map_topology_change {
	uint16_t	enc_handle;
	uint16_t	exp_handle;
	uint8_t	num_entries;
	uint8_t	start_phy_num;
	uint8_t	num_phys;
	uint8_t	exp_status;
	struct _map_phy_change *phy_details;
};

/**
 * struct _map_pcie_topology_change - PCIe entries to be removed from mapping
 * table
 * @enc_handle: enclosure handle where this device is located
 * @switch_dev_handle:  PCIe switch device handle where this device is located
 * @num_entries: number of entries in the PCIe Topology Change List event
 * @start_port_num: port number of the first port in the event data
 * @num_ports: number of ports in the PCIe switch device
 * @switch_status: status for the PCIe switch where this device is located
 * @port_details: more details about each Port in the event data
 */
struct _map_pcie_topology_change {
	uint16_t	enc_handle;
	uint16_t	switch_dev_handle;
	uint8_t	num_entries;
	uint8_t	start_port_num;
	uint8_t	num_ports;
	uint8_t switch_status;
	struct _map_port_change *port_details;
};

extern int
mprsas_get_sas_address_for_sata_disk(struct mpr_softc *ioc,
    u64 *sas_address, u16 handle, u32 device_info, u8 *is_SATA_SSD);

#endif
