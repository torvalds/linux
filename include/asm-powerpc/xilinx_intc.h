/*
 * Xilinx intc external definitions
 *
 * Copyright 2007 Secret Lab Technologies Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_POWERPC_XILINX_INTC_H
#define _ASM_POWERPC_XILINX_INTC_H

#ifdef __KERNEL__

extern void __init xilinx_intc_init_tree(void);
extern unsigned int xilinx_intc_get_irq(void);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_XILINX_INTC_H */
