/*
 * IRQ subsystem internal functions and variables:
 */

extern int noirqdebug;

/* Set default functions for irq_chip structures: */
extern void irq_chip_set_defaults(struct irq_chip *chip);

/* Set default handler: */
extern void compat_irq_chip_set_default_handler(struct irq_desc *desc);

#ifdef CONFIG_PROC_FS
extern void register_irq_proc(unsigned int irq);
extern void register_handler_proc(unsigned int irq, struct irqaction *action);
extern void unregister_handler_proc(unsigned int irq, struct irqaction *action);
#else
static inline void register_irq_proc(unsigned int irq) { }
static inline void register_handler_proc(unsigned int irq,
					 struct irqaction *action) { }
static inline void unregister_handler_proc(unsigned int irq,
					   struct irqaction *action) { }
#endif

