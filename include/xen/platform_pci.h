#ifndef _XEN_PLATFORM_PCI_H
#define _XEN_PLATFORM_PCI_H

#define XEN_IOPORT_MAGIC_VAL 0x49d2
#define XEN_IOPORT_LINUX_PRODNUM 0x0003
#define XEN_IOPORT_LINUX_DRVVER  0x0001

#define XEN_IOPORT_BASE 0x10

#define XEN_IOPORT_PLATFLAGS	(XEN_IOPORT_BASE + 0) /* 1 byte access (R/W) */
#define XEN_IOPORT_MAGIC	(XEN_IOPORT_BASE + 0) /* 2 byte access (R) */
#define XEN_IOPORT_UNPLUG	(XEN_IOPORT_BASE + 0) /* 2 byte access (W) */
#define XEN_IOPORT_DRVVER	(XEN_IOPORT_BASE + 0) /* 4 byte access (W) */

#define XEN_IOPORT_SYSLOG	(XEN_IOPORT_BASE + 2) /* 1 byte access (W) */
#define XEN_IOPORT_PROTOVER	(XEN_IOPORT_BASE + 2) /* 1 byte access (R) */
#define XEN_IOPORT_PRODNUM	(XEN_IOPORT_BASE + 2) /* 2 byte access (W) */

#define XEN_UNPLUG_ALL_IDE_DISKS	(1<<0)
#define XEN_UNPLUG_ALL_NICS		(1<<1)
#define XEN_UNPLUG_AUX_IDE_DISKS	(1<<2)
#define XEN_UNPLUG_ALL			(XEN_UNPLUG_ALL_IDE_DISKS|\
					 XEN_UNPLUG_ALL_NICS|\
					 XEN_UNPLUG_AUX_IDE_DISKS)

#define XEN_UNPLUG_UNNECESSARY 		(1<<16)
#define XEN_UNPLUG_NEVER	 		(1<<17)

static inline int xen_must_unplug_nics(void) {
#if (defined(CONFIG_XEN_NETDEV_FRONTEND) || \
		defined(CONFIG_XEN_NETDEV_FRONTEND_MODULE)) && \
		defined(CONFIG_XEN_PVHVM)
        return 1;
#else
        return 0;
#endif
}

static inline int xen_must_unplug_disks(void) {
#if (defined(CONFIG_XEN_BLKDEV_FRONTEND) || \
		defined(CONFIG_XEN_BLKDEV_FRONTEND_MODULE)) && \
		defined(CONFIG_XEN_PVHVM)
        return 1;
#else
        return 0;
#endif
}

extern int xen_platform_pci_unplug;

#if defined(CONFIG_XEN_PVHVM)
extern bool xen_has_pv_devices(void);
extern bool xen_has_pv_disk_devices(void);
extern bool xen_has_pv_nic_devices(void);
extern bool xen_has_pv_and_legacy_disk_devices(void);
#else
static inline bool xen_has_pv_devices(void)
{
	return IS_ENABLED(CONFIG_XEN);
}
static inline bool xen_has_pv_disk_devices(void)
{
	return IS_ENABLED(CONFIG_XEN);
}
static inline bool xen_has_pv_nic_devices(void)
{
	return IS_ENABLED(CONFIG_XEN);
}
static inline bool xen_has_pv_and_legacy_disk_devices(void)
{
	return false;
}
#endif
#endif /* _XEN_PLATFORM_PCI_H */
