/*
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_USB_OTG_ID_H
#define __LINUX_USB_OTG_ID_H

#include <linux/notifier.h>
#include <linux/plist.h>

/**
 * otg_id_notifier_block
 *
 * @priority: Order the notifications will be called in.  Higher numbers
 *    get called first.
 * @detect: Called during otg_id_notify.  Return OTG_ID_HANDLED if the USB cable
 *    has been identified
 * @proxy_wait: Called during otg_id_notify if a previous handler returns
 *    OTG_ID_PROXY_WAIT.  This should wait on ID change then call otg_id_notify.
 *    This is used when a handler knows what's connected but can't detect
 *    the change itself.
 * @cancel: Called after detect has returned OTG_ID_HANDLED to ask it to
 *    release detection resources to allow a new identification to occur.
 */

struct otg_id_notifier_block {
	int priority;
	int (*detect)(struct otg_id_notifier_block *otg_id_nb);
	int (*proxy_wait)(struct otg_id_notifier_block *otg_id_nb);
	void (*cancel)(struct otg_id_notifier_block *otg_id_nb);
	struct plist_node p;
};

#define OTG_ID_PROXY_WAIT 2
#define OTG_ID_HANDLED 1
#define OTG_ID_UNHANDLED 0

int otg_id_register_notifier(struct otg_id_notifier_block *otg_id_nb);
void otg_id_unregister_notifier(struct otg_id_notifier_block *otg_id_nb);

void otg_id_notify(void);

#endif /* __LINUX_USB_OTG_ID_H */
