/* SPDX-License-Identifier: GPL-2.0 */
/*
 * omap-mailbox: interprocessor communication module for OMAP
 */

#ifndef OMAP_MAILBOX_H
#define OMAP_MAILBOX_H

typedef u32 mbox_msg_t;

#define omap_mbox_to_message(data) ((void *)(uintptr_t)(data))
#define omap_mbox_from_message(data) ((mbox_msg_t)(uintptr_t)(data))

#endif /* OMAP_MAILBOX_H */
