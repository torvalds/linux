#ifndef _PPC64_CURRENT_H
#define _PPC64_CURRENT_H

#include <asm/paca.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define get_current()   (get_paca()->__current)
#define current         get_current()

#endif /* !(_PPC64_CURRENT_H) */
