#ifndef FC_TRANSPORT_FCOE_H
#define FC_TRANSPORT_FCOE_H

#include <linux/device.h>
#include <linux/netdevice.h>
#include <scsi/scsi_host.h>
#include <scsi/libfc.h>

/**
 * struct fcoe_transport - FCoE transport struct for generic transport
 * for Ethernet devices as well as pure HBAs
 *
 * @name: name for thsi transport
 * @bus: physical bus type (pci_bus_type)
 * @driver: physical bus driver for network device
 * @create: entry create function
 * @destroy: exit destroy function
 * @list: list of transports
 */
struct fcoe_transport {
	char *name;
	unsigned short vendor;
	unsigned short device;
	struct bus_type *bus;
	struct device_driver *driver;
	int (*create)(struct net_device *device);
	int (*destroy)(struct net_device *device);
	bool (*match)(struct net_device *device);
	struct list_head list;
	struct list_head devlist;
	struct mutex devlock;
};

/**
 * MODULE_ALIAS_FCOE_PCI
 *
 * some care must be taken with this, vendor and device MUST be a hex value
 * preceded with 0x and with letters in lower case (0x12ab, not 0x12AB or 12AB)
 */
#define MODULE_ALIAS_FCOE_PCI(vendor, device) \
	MODULE_ALIAS("fcoe-pci-" __stringify(vendor) "-" __stringify(device))

/* exported funcs */
int fcoe_transport_attach(struct net_device *netdev);
int fcoe_transport_release(struct net_device *netdev);
int fcoe_transport_register(struct fcoe_transport *t);
int fcoe_transport_unregister(struct fcoe_transport *t);
int fcoe_load_transport_driver(struct net_device *netdev);
int __init fcoe_transport_init(void);
int __exit fcoe_transport_exit(void);

/* fcow_sw is the default transport */
extern struct fcoe_transport fcoe_sw_transport;
#endif /* FC_TRANSPORT_FCOE_H */
