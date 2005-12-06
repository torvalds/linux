/*
 * Miguel de Icaza
 */
#ifndef __ASM_INVENTORY_H
#define __ASM_INVENTORY_H

#include <linux/compiler.h>

typedef struct inventory_s {
	struct inventory_s *inv_next;
	int    inv_class;
	int    inv_type;
	int    inv_controller;
	int    inv_unit;
	int    inv_state;
} inventory_t;

extern int inventory_items;

extern void add_to_inventory (int class, int type, int controller, int unit, int state);
extern int dump_inventory_to_user (void __user *userbuf, int size);
extern int __init init_inventory(void);

#endif /* __ASM_INVENTORY_H */
