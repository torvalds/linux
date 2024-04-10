/* SPDX-License-Identifier: GPL-2.0 */
/*
 * omap-mailbox: interprocessor communication module for OMAP
 */

#ifndef OMAP_MAILBOX_H
#define OMAP_MAILBOX_H

typedef uintptr_t mbox_msg_t;

#define omap_mbox_message(data) (u32)(mbox_msg_t)(data)

typedef int __bitwise omap_mbox_irq_t;
#define IRQ_TX ((__force omap_mbox_irq_t) 1)
#define IRQ_RX ((__force omap_mbox_irq_t) 2)

#endif /* OMAP_MAILBOX_H */
