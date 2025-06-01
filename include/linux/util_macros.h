/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HELPER_MACROS_H_
#define _LINUX_HELPER_MACROS_H_

#include <linux/compiler_attributes.h>
#include <linux/math.h>
#include <linux/typecheck.h>
#include <linux/stddef.h>

/**
 * for_each_if - helper for handling conditionals in various for_each macros
 * @condition: The condition to check
 *
 * Typical use::
 *
 *	#define for_each_foo_bar(x, y) \'
 *		list_for_each_entry(x, y->list, head) \'
 *			for_each_if(x->something == SOMETHING)
 *
 * The for_each_if() macro makes the use of for_each_foo_bar() less error
 * prone.
 */
#define for_each_if(condition) if (!(condition)) {} else

/**
 * find_closest - locate the closest element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element. Must be sorted
 *  in ascending order.
 * @as: Size of 'a'.
 *
 * Returns the index of the element closest to 'x'.
 * Note: If using an array of negative numbers (or mixed positive numbers),
 *       then be sure that 'x' is of a signed-type to get good results.
 */
#define find_closest(x, a, as)						\
({									\
	typeof(as) __fc_i, __fc_as = (as) - 1;				\
	long __fc_mid_x, __fc_x = (x);					\
	long __fc_left, __fc_right;					\
	typeof(*a) const *__fc_a = (a);					\
	for (__fc_i = 0; __fc_i < __fc_as; __fc_i++) {			\
		__fc_mid_x = (__fc_a[__fc_i] + __fc_a[__fc_i + 1]) / 2;	\
		if (__fc_x <= __fc_mid_x) {				\
			__fc_left = __fc_x - __fc_a[__fc_i];		\
			__fc_right = __fc_a[__fc_i + 1] - __fc_x;	\
			if (__fc_right < __fc_left)			\
				__fc_i++;				\
			break;						\
		}							\
	}								\
	(__fc_i);							\
})

/**
 * find_closest_descending - locate the closest element in a sorted array
 * @x: The reference value.
 * @a: The array in which to look for the closest element. Must be sorted
 *  in descending order.
 * @as: Size of 'a'.
 *
 * Similar to find_closest() but 'a' is expected to be sorted in descending
 * order. The iteration is done in reverse order, so that the comparison
 * of '__fc_right' & '__fc_left' also works for unsigned numbers.
 */
#define find_closest_descending(x, a, as)				\
({									\
	typeof(as) __fc_i, __fc_as = (as) - 1;				\
	long __fc_mid_x, __fc_x = (x);					\
	long __fc_left, __fc_right;					\
	typeof(*a) const *__fc_a = (a);					\
	for (__fc_i = __fc_as; __fc_i >= 1; __fc_i--) {			\
		__fc_mid_x = (__fc_a[__fc_i] + __fc_a[__fc_i - 1]) / 2;	\
		if (__fc_x <= __fc_mid_x) {				\
			__fc_left = __fc_x - __fc_a[__fc_i];		\
			__fc_right = __fc_a[__fc_i - 1] - __fc_x;	\
			if (__fc_right < __fc_left)			\
				__fc_i--;				\
			break;						\
		}							\
	}								\
	(__fc_i);							\
})

/**
 * PTR_IF - evaluate to @ptr if @cond is true, or to NULL otherwise.
 * @cond: A conditional, usually in a form of IS_ENABLED(CONFIG_FOO)
 * @ptr: A pointer to assign if @cond is true.
 *
 * PTR_IF(IS_ENABLED(CONFIG_FOO), ptr) evaluates to @ptr if CONFIG_FOO is set
 * to 'y' or 'm', or to NULL otherwise. The @ptr argument must be a pointer.
 *
 * The macro can be very useful to help compiler dropping dead code.
 *
 * For instance, consider the following::
 *
 *     #ifdef CONFIG_FOO_SUSPEND
 *     static int foo_suspend(struct device *dev)
 *     {
 *        ...
 *     }
 *     #endif
 *
 *     static struct pm_ops foo_ops = {
 *     #ifdef CONFIG_FOO_SUSPEND
 *         .suspend = foo_suspend,
 *     #endif
 *     };
 *
 * While this works, the foo_suspend() macro is compiled conditionally,
 * only when CONFIG_FOO_SUSPEND is set. This is problematic, as there could
 * be a build bug in this function, we wouldn't have a way to know unless
 * the configuration option is set.
 *
 * An alternative is to declare foo_suspend() always, but mark it
 * as __maybe_unused. This works, but the __maybe_unused attribute
 * is required to instruct the compiler that the function may not
 * be referenced anywhere, and is safe to remove without making
 * a fuss about it. This makes the programmer responsible for tagging
 * the functions that can be garbage-collected.
 *
 * With the macro it is possible to write the following:
 *
 *     static int foo_suspend(struct device *dev)
 *     {
 *        ...
 *     }
 *
 *     static struct pm_ops foo_ops = {
 *         .suspend = PTR_IF(IS_ENABLED(CONFIG_FOO_SUSPEND), foo_suspend),
 *     };
 *
 * The foo_suspend() function will now be automatically dropped by the
 * compiler, and it does not require any specific attribute.
 */
#define PTR_IF(cond, ptr)	((cond) ? (ptr) : NULL)

/**
 * to_user_ptr - cast a pointer passed as u64 from user space to void __user *
 * @x: The u64 value from user space, usually via IOCTL
 *
 * to_user_ptr() simply casts a pointer passed as u64 from user space to void
 * __user * correctly. Using this lets us get rid of all the tiresome casts.
 */
#define u64_to_user_ptr(x)		\
({					\
	typecheck(u64, (x));		\
	(void __user *)(uintptr_t)(x);	\
})

/**
 * is_insidevar - check if the @ptr points inside the @var memory range.
 * @ptr:	the pointer to a memory address.
 * @var:	the variable which address and size identify the memory range.
 *
 * Evaluates to true if the address in @ptr lies within the memory
 * range allocated to @var.
 */
#define is_insidevar(ptr, var)						\
	((uintptr_t)(ptr) >= (uintptr_t)(var) &&			\
	 (uintptr_t)(ptr) <  (uintptr_t)(var) + sizeof(var))

#endif
