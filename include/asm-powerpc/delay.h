#ifndef _ASM_POWERPC_DELAY_H
#define _ASM_POWERPC_DELAY_H

/*
 * Copyright 1996, Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * PPC64 Support added by Dave Engebretsen, Todd Inglett, Mike Corrigan,
 * Anton Blanchard.
 */

extern void __delay(unsigned long loops);
extern void udelay(unsigned long usecs);

#endif /* _ASM_POWERPC_DELAY_H */
