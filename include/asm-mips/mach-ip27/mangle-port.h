/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 */
#ifndef __ASM_MACH_IP27_MANGLE_PORT_H
#define __ASM_MACH_IP27_MANGLE_PORT_H

#define __swizzle_addr_b(port)	(port)
#define __swizzle_addr_w(port)	((port) ^ 2)
#define __swizzle_addr_l(port)	(port)
#define __swizzle_addr_q(port)	(port)

#endif /* __ASM_MACH_IP27_MANGLE_PORT_H */
