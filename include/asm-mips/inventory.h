/*
 * Miguel de Icaza
 */
#ifndef __ASM_INVENTORY_H
#define __ASM_INVENTORY_H

typedef struct inventory_s {
	struct inventory_s *inv_next;
	int    inv_class;
	int    inv_type;
	int    inv_controller;
	int    inv_unit;
	int    inv_state;
} inventory_t;

extern int inventory_items;
void add_to_inventory (int class, int type, int controller, int unit, int state);
int dump_inventory_to_user (void *userbuf, int size);

#endif /* __ASM_INVENTORY_H */
