/*
 * Internal header to deal with irq_desc->status which will be renamed
 * to irq_desc->settings.
 */
enum {
	_IRQ_DEFAULT_INIT_FLAGS	= IRQ_DEFAULT_INIT_FLAGS,
};

#undef IRQ_INPROGRESS
#define IRQ_INPROGRESS		GOT_YOU_MORON
