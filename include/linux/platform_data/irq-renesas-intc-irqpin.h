/*
 * Renesas INTC External IRQ Pin Driver
 *
 *  Copyright (C) 2013 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __IRQ_RENESAS_INTC_IRQPIN_H__
#define __IRQ_RENESAS_INTC_IRQPIN_H__

struct renesas_intc_irqpin_config {
	unsigned int sense_bitfield_width;
	unsigned int irq_base;
	bool control_parent;
};

#endif /* __IRQ_RENESAS_INTC_IRQPIN_H__ */
