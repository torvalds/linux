#ifndef __RK_USBBC_H
#define __RK_USBBC_H

/* USB Charger Types */
enum bc_port_type{
	USB_BC_TYPE_DISCNT = 0,
	USB_BC_TYPE_SDP,
	USB_BC_TYPE_DCP,
	USB_BC_TYPE_CDP,
	USB_BC_TYPE_UNKNOW,
	USB_OTG_POWER_ON,
	USB_OTG_POWER_OFF,
	USB_BC_TYPE_MAX,
};

/***********************************
 * USB Port Type
 * 0 : Disconnect
 * 1 : SDP - pc
 * 2 : DCP - charger
 * 3 : CDP - pc with big currect charge
 ************************************/
extern int dwc_otg_check_dpdm(bool wait);
extern int rk_bc_detect_notifier_register(struct notifier_block *nb,
					  enum bc_port_type *type);
extern int rk_bc_detect_notifier_unregister(struct notifier_block *nb);

#endif
