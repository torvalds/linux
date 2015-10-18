/*
 * L3 layer of Medusa DS9 for Linux
 * Copyright (C) 2002 Milan Pikula <www@terminus.sk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 *
 * This is the main file of L3. It calls initialization routines of
 * l3 related code, and it also exports some symbols. What a surprise,
 * it gets registered before anything else and cannot be built as a
 * module.
 */

#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l3/registry.h>

// EXPORT_SYMBOL(kclasses);
// EXPORT_SYMBOL(evtypes);
// EXPORT_SYMBOL(authserver);

EXPORT_SYMBOL(med_register_kclass);
EXPORT_SYMBOL(med_unregister_kclass);
EXPORT_SYMBOL(med_get_kclass);
EXPORT_SYMBOL(med_get_kclass_by_name);
EXPORT_SYMBOL(med_get_kclass_by_cinfo);
EXPORT_SYMBOL(med_get_kclass_by_pointer);
EXPORT_SYMBOL(med_put_kclass);
EXPORT_SYMBOL(med_unlink_kclass);
EXPORT_SYMBOL(med_register_evtype);
EXPORT_SYMBOL(med_unregister_evtype);
EXPORT_SYMBOL(med_register_authserver);
EXPORT_SYMBOL(med_get_authserver);
EXPORT_SYMBOL(med_unregister_authserver);
EXPORT_SYMBOL(med_put_authserver);
EXPORT_SYMBOL(med_decide);

void __init medusa_l3_init(void) {
}

void __init medusa_init(void) {
	medusa_l3_init();
}

