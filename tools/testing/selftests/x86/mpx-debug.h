/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MPX_DEBUG_H
#define _MPX_DEBUG_H

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif
#define dprintf_level(level, args...) do { if(level <= DEBUG_LEVEL) printf(args); } while(0)
#define dprintf1(args...) dprintf_level(1, args)
#define dprintf2(args...) dprintf_level(2, args)
#define dprintf3(args...) dprintf_level(3, args)
#define dprintf4(args...) dprintf_level(4, args)
#define dprintf5(args...) dprintf_level(5, args)

#endif /* _MPX_DEBUG_H */
