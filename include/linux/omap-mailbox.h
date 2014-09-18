/*
 * omap-mailbox: interprocessor communication module for OMAP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OMAP_MAILBOX_H
#define OMAP_MAILBOX_H

typedef u32 mbox_msg_t;
struct omap_mbox;

typedef int __bitwise omap_mbox_irq_t;
#define IRQ_TX ((__force omap_mbox_irq_t) 1)
#define IRQ_RX ((__force omap_mbox_irq_t) 2)

int omap_mbox_msg_send(struct omap_mbox *, mbox_msg_t msg);

struct omap_mbox *omap_mbox_get(const char *, struct notifier_block *nb);
void omap_mbox_put(struct omap_mbox *mbox, struct notifier_block *nb);

void omap_mbox_save_ctx(struct omap_mbox *mbox);
void omap_mbox_restore_ctx(struct omap_mbox *mbox);
void omap_mbox_enable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq);
void omap_mbox_disable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq);

#endif /* OMAP_MAILBOX_H */
