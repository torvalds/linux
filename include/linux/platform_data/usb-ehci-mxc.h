#ifndef __INCLUDE_ASM_ARCH_MXC_EHCI_H
#define __INCLUDE_ASM_ARCH_MXC_EHCI_H

struct mxc_usbh_platform_data {
	int (*init)(struct platform_device *pdev);
	int (*exit)(struct platform_device *pdev);

	unsigned int		 portsc;
	struct usb_phy		*otg;
};

#endif /* __INCLUDE_ASM_ARCH_MXC_EHCI_H */

