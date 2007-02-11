/*
 *  include/asm-mips/emma2rh/markeins.h
 *      This file is EMMA2RH board depended header.
 *
 *  Copyright (C) NEC Electronics Corporation 2005-2006
 *
 *  This file based on include/asm-mips/ddb5xxx/ddb5xxx.h
 *          Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef MARKEINS_H
#define MARKEINS_H

#define NUM_EMMA2RH_IRQ_SW	32
#define NUM_EMMA2RH_IRQ_GPIO	32

#define EMMA2RH_SW_CASCADE	(EMMA2RH_IRQ_INT7 - EMMA2RH_IRQ_INT0)
#define EMMA2RH_GPIO_CASCADE	(EMMA2RH_IRQ_INT46 - EMMA2RH_IRQ_INT0)

#define EMMA2RH_SW_IRQ_BASE	(EMMA2RH_IRQ_BASE + NUM_EMMA2RH_IRQ)
#define EMMA2RH_GPIO_IRQ_BASE	(EMMA2RH_SW_IRQ_BASE + NUM_EMMA2RH_IRQ_SW)

#define EMMA2RH_SW_IRQ_INT0	(0+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT1	(1+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT2	(2+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT3	(3+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT4	(4+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT5	(5+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT6	(6+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT7	(7+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT8	(8+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT9	(9+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT10	(10+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT11	(11+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT12	(12+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT13	(13+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT14	(14+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT15	(15+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT16	(16+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT17	(17+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT18	(18+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT19	(19+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT20	(20+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT21	(21+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT22	(22+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT23	(23+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT24	(24+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT25	(25+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT26	(26+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT27	(27+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT28	(28+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT29	(29+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT30	(30+EMMA2RH_SW_IRQ_BASE)
#define EMMA2RH_SW_IRQ_INT31	(31+EMMA2RH_SW_IRQ_BASE)

#define MARKEINS_PCI_IRQ_INTA	EMMA2RH_GPIO_IRQ_BASE+15
#define MARKEINS_PCI_IRQ_INTB	EMMA2RH_GPIO_IRQ_BASE+16
#define MARKEINS_PCI_IRQ_INTC	EMMA2RH_GPIO_IRQ_BASE+17
#define MARKEINS_PCI_IRQ_INTD	EMMA2RH_GPIO_IRQ_BASE+18

#endif /* CONFIG_MARKEINS */
