/* Public domain. */

#ifndef _LINUX_VGA_SWITCHEROO_H
#define _LINUX_VGA_SWITCHEROO_H

#include <linux/fb.h>

#define VGA_SWITCHEROO_CAN_SWITCH_DDC		1

struct pci_dev;
struct dev_pm_domain;

struct vga_switcheroo_client_ops {
};

static inline int
vga_switcheroo_register_client(struct pci_dev *pdev,
    const struct vga_switcheroo_client_ops *ops, bool x)
{
	return 0;
}

static inline void
vga_switcheroo_unregister_client(struct pci_dev *pdev)
{
}

static inline void
vga_switcheroo_process_delayed_switch(void)
{
}

static inline void
vga_switcheroo_fini_domain_pm_ops(struct device *dev)
{
}

static inline int
vga_switcheroo_handler_flags(void)
{
	return 0;
}

static inline void
vga_switcheroo_client_fb_set(struct pci_dev *pdev, struct fb_info *fbi)
{
}

static inline void
vga_switcheroo_init_domain_pm_ops(struct device *dev, struct dev_pm_domain *dom)
{
}

static inline int
vga_switcheroo_lock_ddc(struct pci_dev *pdev)
{
	return -ENOSYS;
}

static inline int
vga_switcheroo_unlock_ddc(struct pci_dev *pdev)
{
	return -ENOSYS;
}

static inline bool
vga_switcheroo_client_probe_defer(struct pci_dev *pdev)
{
	return false;
}

#endif
