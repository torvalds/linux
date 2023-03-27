/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2008-2010, 2012-2014, The Linux Foundation.
 * All rights reserved.
 */

#ifndef _DRIVERS_USB_DIAG_H_
#define _DRIVERS_USB_DIAG_H_

#include <linux/err.h>

#define DIAG_LEGACY		"diag"
#define DIAG_MDM		"diag_mdm"
#define DIAG_QSC		"diag_qsc"
#define DIAG_MDM2		"diag_mdm2"

#define USB_DIAG_CONNECT	0
#define USB_DIAG_DISCONNECT	1
#define USB_DIAG_WRITE_DONE	2
#define USB_DIAG_READ_DONE	3

struct diag_request {
	char *buf;
	int length;
	int actual;
	int status;
	void *context;
};

struct usb_diag_ch {
	const char *name;
	struct list_head list;
	void (*notify)(void *priv, unsigned int event,
			struct diag_request *d_req);
	void *priv;
	void *priv_usb;
};

#if IS_ENABLED(CONFIG_USB_F_DIAG)
int usb_diag_request_size(struct usb_diag_ch *ch);
struct usb_diag_ch *usb_diag_open(const char *name, void *priv,
		void (*notify)(void *, unsigned int, struct diag_request *));
void usb_diag_close(struct usb_diag_ch *ch);
int usb_diag_alloc_req(struct usb_diag_ch *ch, int n_write, int n_read);
int usb_diag_read(struct usb_diag_ch *ch, struct diag_request *d_req);
int usb_diag_write(struct usb_diag_ch *ch, struct diag_request *d_req);
#else
static inline struct usb_diag_ch *usb_diag_open(const char *name, void *priv,
		void (*notify)(void *, unsigned int, struct diag_request *))
{
	return ERR_PTR(-ENODEV);
}
static inline void usb_diag_close(struct usb_diag_ch *ch)
{
}
static inline
int usb_diag_alloc_req(struct usb_diag_ch *ch, int n_write, int n_read)
{
	return -ENODEV;
}
static inline
int usb_diag_read(struct usb_diag_ch *ch, struct diag_request *d_req)
{
	return -ENODEV;
}
static inline
int usb_diag_write(struct usb_diag_ch *ch, struct diag_request *d_req)
{
	return -ENODEV;
}
#endif /* CONFIG_USB_F_DIAG */
#endif /* _DRIVERS_USB_DIAG_H_ */
