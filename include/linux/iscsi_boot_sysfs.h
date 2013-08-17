/*
 * Export the iSCSI boot info to userland via sysfs.
 *
 * Copyright (C) 2010 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2010 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _ISCSI_BOOT_SYSFS_
#define _ISCSI_BOOT_SYSFS_

/*
 * The text attributes names for each of the kobjects.
*/
enum iscsi_boot_eth_properties_enum {
	ISCSI_BOOT_ETH_INDEX,
	ISCSI_BOOT_ETH_FLAGS,
	ISCSI_BOOT_ETH_IP_ADDR,
	ISCSI_BOOT_ETH_SUBNET_MASK,
	ISCSI_BOOT_ETH_ORIGIN,
	ISCSI_BOOT_ETH_GATEWAY,
	ISCSI_BOOT_ETH_PRIMARY_DNS,
	ISCSI_BOOT_ETH_SECONDARY_DNS,
	ISCSI_BOOT_ETH_DHCP,
	ISCSI_BOOT_ETH_VLAN,
	ISCSI_BOOT_ETH_MAC,
	/* eth_pci_bdf - this is replaced by link to the device itself. */
	ISCSI_BOOT_ETH_HOSTNAME,
	ISCSI_BOOT_ETH_END_MARKER,
};

enum iscsi_boot_tgt_properties_enum {
	ISCSI_BOOT_TGT_INDEX,
	ISCSI_BOOT_TGT_FLAGS,
	ISCSI_BOOT_TGT_IP_ADDR,
	ISCSI_BOOT_TGT_PORT,
	ISCSI_BOOT_TGT_LUN,
	ISCSI_BOOT_TGT_CHAP_TYPE,
	ISCSI_BOOT_TGT_NIC_ASSOC,
	ISCSI_BOOT_TGT_NAME,
	ISCSI_BOOT_TGT_CHAP_NAME,
	ISCSI_BOOT_TGT_CHAP_SECRET,
	ISCSI_BOOT_TGT_REV_CHAP_NAME,
	ISCSI_BOOT_TGT_REV_CHAP_SECRET,
	ISCSI_BOOT_TGT_END_MARKER,
};

enum iscsi_boot_initiator_properties_enum {
	ISCSI_BOOT_INI_INDEX,
	ISCSI_BOOT_INI_FLAGS,
	ISCSI_BOOT_INI_ISNS_SERVER,
	ISCSI_BOOT_INI_SLP_SERVER,
	ISCSI_BOOT_INI_PRI_RADIUS_SERVER,
	ISCSI_BOOT_INI_SEC_RADIUS_SERVER,
	ISCSI_BOOT_INI_INITIATOR_NAME,
	ISCSI_BOOT_INI_END_MARKER,
};

struct attribute_group;

struct iscsi_boot_kobj {
	struct kobject kobj;
	struct attribute_group *attr_group;
	struct list_head list;

	/*
	 * Pointer to store driver specific info. If set this will
	 * be freed for the LLD when the kobj release function is called.
	 */
	void *data;
	/*
	 * Driver specific show function.
	 *
	 * The enum of the type. This can be any value of the above
	 * properties.
	 */
	ssize_t (*show) (void *data, int type, char *buf);

	/*
	 * Drivers specific visibility function.
	 * The function should return if they the attr should be readable
	 * writable or should not be shown.
	 *
	 * The enum of the type. This can be any value of the above
	 * properties.
	 */
	umode_t (*is_visible) (void *data, int type);

	/*
	 * Driver specific release function.
	 *
	 * The function should free the data passed in.
	 */
	void (*release) (void *data);
};

struct iscsi_boot_kset {
	struct list_head kobj_list;
	struct kset *kset;
};

struct iscsi_boot_kobj *
iscsi_boot_create_initiator(struct iscsi_boot_kset *boot_kset, int index,
			    void *data,
			    ssize_t (*show) (void *data, int type, char *buf),
			    umode_t (*is_visible) (void *data, int type),
			    void (*release) (void *data));

struct iscsi_boot_kobj *
iscsi_boot_create_ethernet(struct iscsi_boot_kset *boot_kset, int index,
			   void *data,
			   ssize_t (*show) (void *data, int type, char *buf),
			   umode_t (*is_visible) (void *data, int type),
			   void (*release) (void *data));
struct iscsi_boot_kobj *
iscsi_boot_create_target(struct iscsi_boot_kset *boot_kset, int index,
			 void *data,
			 ssize_t (*show) (void *data, int type, char *buf),
			 umode_t (*is_visible) (void *data, int type),
			 void (*release) (void *data));

struct iscsi_boot_kset *iscsi_boot_create_kset(const char *set_name);
struct iscsi_boot_kset *iscsi_boot_create_host_kset(unsigned int hostno);
void iscsi_boot_destroy_kset(struct iscsi_boot_kset *boot_kset);

#endif
