/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 by Ralf Baechle
 */
#ifndef __ASM_MACH_DEC_PARAM_H
#define __ASM_MACH_DEC_PARAM_H

/*
 * log2(HZ), change this here if you want another HZ value. This is also
 * used in dec_time_init.  Minimum is 1, Maximum is 15.
 */
#define LOG_2_HZ 7
#define HZ (1 << LOG_2_HZ)

#endif /* __ASM_MACH_DEC_PARAM_H */
