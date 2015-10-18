/*
 * L2 layer of Medusa DS9 for Linux
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
 * This is the main file of L2. It used to call the initialization routines,
 * but now it just exports some symbols.
 */

#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l1/process_handlers.h>

EXPORT_SYMBOL(medusa_capable);

