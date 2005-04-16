#ifndef __PPC64_UNALIGNED_H
#define __PPC64_UNALIGNED_H

/*
 * The PowerPC can do unaligned accesses itself in big endian mode. 
 *
 * The strange macros are there to make sure these can't
 * be misused in a way that makes them not work on other
 * architectures where unaligned accesses aren't as simple.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define get_unaligned(ptr) (*(ptr))

#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif /* __PPC64_UNALIGNED_H */
