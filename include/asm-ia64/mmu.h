#ifndef __MMU_H
#define __MMU_H

/*
 * Type for a context number.  We declare it volatile to ensure proper ordering when it's
 * accessed outside of spinlock'd critical sections (e.g., as done in activate_mm() and
 * init_new_context()).
 */
typedef volatile unsigned long mm_context_t;

#endif
