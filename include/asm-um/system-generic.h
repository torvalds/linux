#ifndef __UM_SYSTEM_GENERIC_H
#define __UM_SYSTEM_GENERIC_H

#include "asm/arch/system.h"

#undef switch_to
#undef raw_local_irq_save
#undef raw_local_irq_restore
#undef raw_local_irq_disable
#undef raw_local_irq_enable
#undef raw_local_save_flags
#undef raw_local_irq_restore
#undef raw_local_irq_enable
#undef raw_local_irq_disable
#undef raw_local_irq_save
#undef irqs_disabled

extern void *switch_to(void *prev, void *next, void *last);

extern int get_signals(void);
extern int set_signals(int enable);
extern int get_signals(void);
extern void block_signals(void);
extern void unblock_signals(void);

#define raw_local_save_flags(flags) do { typecheck(unsigned long, flags); \
				     (flags) = get_signals(); } while(0)
#define raw_local_irq_restore(flags) do { typecheck(unsigned long, flags); \
				      set_signals(flags); } while(0)

#define raw_local_irq_save(flags) do { raw_local_save_flags(flags); \
                                   raw_local_irq_disable(); } while(0)

#define raw_local_irq_enable() unblock_signals()
#define raw_local_irq_disable() block_signals()

#define irqs_disabled()                 \
({                                      \
        unsigned long flags;            \
        raw_local_save_flags(flags);        \
        (flags == 0);                   \
})

extern void *_switch_to(void *prev, void *next, void *last);
#define switch_to(prev, next, last) prev = _switch_to(prev, next, last)

#endif
