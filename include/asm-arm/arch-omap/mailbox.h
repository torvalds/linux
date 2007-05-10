/* mailbox.h */

#ifndef MAILBOX_H
#define MAILBOX_H

#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>

typedef u32 mbox_msg_t;
typedef void (mbox_receiver_t)(mbox_msg_t msg);
struct omap_mbox;

typedef int __bitwise omap_mbox_irq_t;
#define IRQ_TX ((__force omap_mbox_irq_t) 1)
#define IRQ_RX ((__force omap_mbox_irq_t) 2)

typedef int __bitwise omap_mbox_type_t;
#define OMAP_MBOX_TYPE1 ((__force omap_mbox_type_t) 1)
#define OMAP_MBOX_TYPE2 ((__force omap_mbox_type_t) 2)

struct omap_mbox_ops {
	omap_mbox_type_t	type;
	int		(*startup)(struct omap_mbox *mbox);
	void		(*shutdown)(struct omap_mbox *mbox);
	/* fifo */
	mbox_msg_t	(*fifo_read)(struct omap_mbox *mbox);
	void		(*fifo_write)(struct omap_mbox *mbox, mbox_msg_t msg);
	int		(*fifo_empty)(struct omap_mbox *mbox);
	int		(*fifo_full)(struct omap_mbox *mbox);
	/* irq */
	void		(*enable_irq)(struct omap_mbox *mbox, omap_mbox_irq_t irq);
	void		(*disable_irq)(struct omap_mbox *mbox, omap_mbox_irq_t irq);
	void		(*ack_irq)(struct omap_mbox *mbox, omap_mbox_irq_t irq);
	int		(*is_irq)(struct omap_mbox *mbox, omap_mbox_irq_t irq);
};

struct omap_mbox_queue {
	spinlock_t		lock;
	request_queue_t		*queue;
	struct work_struct	work;
	int	(*callback)(void *);
	struct omap_mbox	*mbox;
};

struct omap_mbox {
	char			*name;
	unsigned int		irq;

	struct omap_mbox_queue	*txq, *rxq;

	struct omap_mbox_ops	*ops;

	mbox_msg_t		seq_snd, seq_rcv;

	struct device		dev;

	struct omap_mbox	*next;
	void			*priv;

	void			(*err_notify)(void);
};

int omap_mbox_msg_send(struct omap_mbox *, mbox_msg_t msg, void *);
void omap_mbox_init_seq(struct omap_mbox *);

struct omap_mbox *omap_mbox_get(const char *);
void omap_mbox_put(struct omap_mbox *);

int omap_mbox_register(struct omap_mbox *);
int omap_mbox_unregister(struct omap_mbox *);

#endif /* MAILBOX_H */
