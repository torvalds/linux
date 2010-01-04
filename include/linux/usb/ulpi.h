#ifndef __LINUX_USB_ULPI_H
#define __LINUX_USB_ULPI_H

struct otg_transceiver *otg_ulpi_create(struct otg_io_access_ops *ops,
					unsigned int flags);

#endif /* __LINUX_USB_ULPI_H */
