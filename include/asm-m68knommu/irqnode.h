#ifndef _M68K_IRQNODE_H_
#define _M68K_IRQNODE_H_

#include <linux/interrupt.h>

/*
 * This structure is used to chain together the ISRs for a particular
 * interrupt source (if it supports chaining).
 */
typedef struct irq_node {
	irq_handler_t	handler;
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	struct irq_node *next;
} irq_node_t;

/*
 * This structure has only 4 elements for speed reasons
 */
struct irq_entry {
	irq_handler_t	handler;
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
};

/* count of spurious interrupts */
extern volatile unsigned int num_spurious;

/*
 * This function returns a new irq_node_t
 */
extern irq_node_t *new_irq_node(void);

#endif /* _M68K_IRQNODE_H_ */
