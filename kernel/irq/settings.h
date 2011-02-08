/*
 * Internal header to deal with irq_desc->status which will be renamed
 * to irq_desc->settings.
 */
enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
};

#undef IRQ_INPROGRESS
#define IRQ_INPROGRESS		GOT_YOU_MORON
#undef IRQ_REPLAY
#define IRQ_REPLAY		GOT_YOU_MORON
#undef IRQ_WAITING
#define IRQ_WAITING		GOT_YOU_MORON
#undef IRQ_DISABLED
#define IRQ_DISABLED		GOT_YOU_MORON
#undef IRQ_PENDING
#define IRQ_PENDING		GOT_YOU_MORON
#undef IRQ_MASKED
#define IRQ_MASKED		GOT_YOU_MORON
