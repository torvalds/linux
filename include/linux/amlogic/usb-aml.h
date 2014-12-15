
#ifndef AML_MACH_USB
#define AML_MACH_USB

#include <mach/io.h>
#include <mach/usb.h>

struct aml_usb_platform{
		const char * port_name[MESON_USB_PORT_NUM];
		void * ctrl_regaddr[MESON_USB_PORT_NUM];
		int ctrl_size[MESON_USB_PORT_NUM];
		void * phy_regaddr[MESON_USB_PORT_NUM];
		int phy_size[MESON_USB_PORT_NUM];
		int irq_no[MESON_USB_PORT_NUM];
		int fifo_size[MESON_USB_PORT_NUM];
};

#endif

