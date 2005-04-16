#ifndef _PPC64_SIGINFO_H
#define _PPC64_SIGINFO_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#define SI_PAD_SIZE32		((SI_MAX_SIZE/sizeof(int)) - 3)

#include <asm-generic/siginfo.h>

#endif /* _PPC64_SIGINFO_H */
