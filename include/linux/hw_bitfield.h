/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2025, Collabora Ltd.
 */

#ifndef _LINUX_HW_BITFIELD_H
#define _LINUX_HW_BITFIELD_H

#include <linux/bitfield.h>
#include <linux/build_bug.h>
#include <linux/limits.h>

/**
 * FIELD_PREP_WM16() - prepare a bitfield element with a mask in the upper half
 * @_mask: shifted mask defining the field's length and position
 * @_val:  value to put in the field
 *
 * FIELD_PREP_WM16() masks and shifts up the value, as well as bitwise ORs the
 * result with the mask shifted up by 16.
 *
 * This is useful for a common design of hardware registers where the upper
 * 16-bit half of a 32-bit register is used as a write-enable mask. In such a
 * register, a bit in the lower half is only updated if the corresponding bit
 * in the upper half is high.
 */
#define FIELD_PREP_WM16(_mask, _val)					     \
	({								     \
		typeof(_val) __val = _val;				     \
		typeof(_mask) __mask = _mask;				     \
		__BF_FIELD_CHECK(__mask, ((u16)0U), __val,		     \
				 "HWORD_UPDATE: ");			     \
		(((typeof(__mask))(__val) << __bf_shf(__mask)) & (__mask)) | \
		((__mask) << 16);					     \
	})

/**
 * FIELD_PREP_WM16_CONST() - prepare a constant bitfield element with a mask in
 *                           the upper half
 * @_mask: shifted mask defining the field's length and position
 * @_val:  value to put in the field
 *
 * FIELD_PREP_WM16_CONST() masks and shifts up the value, as well as bitwise ORs
 * the result with the mask shifted up by 16.
 *
 * This is useful for a common design of hardware registers where the upper
 * 16-bit half of a 32-bit register is used as a write-enable mask. In such a
 * register, a bit in the lower half is only updated if the corresponding bit
 * in the upper half is high.
 *
 * Unlike FIELD_PREP_WM16(), this is a constant expression and can therefore
 * be used in initializers. Error checking is less comfortable for this
 * version.
 */
#define FIELD_PREP_WM16_CONST(_mask, _val)				 \
	(								 \
		FIELD_PREP_CONST(_mask, _val) |				 \
		(BUILD_BUG_ON_ZERO(const_true((u64)(_mask) > U16_MAX)) + \
		 ((_mask) << 16))					 \
	)


#endif /* _LINUX_HW_BITFIELD_H */
