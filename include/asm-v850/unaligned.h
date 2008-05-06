/*
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Note that some v850 chips support unaligned access, but it seems too
 * annoying to use.
 */
#ifndef _ASM_V850_UNALIGNED_H
#define _ASM_V850_UNALIGNED_H

#include <linux/unaligned/be_byteshift.h>
#include <linux/unaligned/le_byteshift.h>
#include <linux/unaligned/generic.h>

#define get_unaligned	__get_unaligned_le
#define put_unaligned	__put_unaligned_le

#endif /* _ASM_V850_UNALIGNED_H */
