/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KUNIT_OF_H
#define _KUNIT_OF_H

#include <kunit/test.h>

struct device_node;

#ifdef CONFIG_OF

void of_node_put_kunit(struct kunit *test, struct device_node *node);

#else

static inline
void of_node_put_kunit(struct kunit *test, struct device_node *node)
{
	kunit_skip(test, "requires CONFIG_OF");
}

#endif /* !CONFIG_OF */

#if defined(CONFIG_OF) && defined(CONFIG_OF_OVERLAY) && defined(CONFIG_OF_EARLY_FLATTREE)

int of_overlay_fdt_apply_kunit(struct kunit *test, void *overlay_fdt,
			       u32 overlay_fdt_size, int *ovcs_id);
#else

static inline int
of_overlay_fdt_apply_kunit(struct kunit *test, void *overlay_fdt,
			   u32 overlay_fdt_size, int *ovcs_id)
{
	kunit_skip(test, "requires CONFIG_OF and CONFIG_OF_OVERLAY and CONFIG_OF_EARLY_FLATTREE for root node");
	return -EINVAL;
}

#endif

/**
 * __of_overlay_apply_kunit() - Test managed of_overlay_fdt_apply() variant
 * @test: test context
 * @overlay_begin: start address of overlay to apply
 * @overlay_end: end address of overlay to apply
 *
 * This is mostly internal API. See of_overlay_apply_kunit() for the wrapper
 * that makes this easier to use.
 *
 * Similar to of_overlay_fdt_apply(), except the overlay is managed by the test
 * case and is automatically removed with of_overlay_remove() after the test
 * case concludes.
 *
 * Return: 0 on success, negative errno on failure
 */
static inline int __of_overlay_apply_kunit(struct kunit *test,
					   u8 *overlay_begin,
					   const u8 *overlay_end)
{
	int unused;

	return of_overlay_fdt_apply_kunit(test, overlay_begin,
					  overlay_end - overlay_begin,
					  &unused);
}

/**
 * of_overlay_apply_kunit() - Test managed of_overlay_fdt_apply() for built-in overlays
 * @test: test context
 * @overlay_name: name of overlay to apply
 *
 * This macro is used to apply a device tree overlay built with the
 * cmd_dt_S_dtbo rule in scripts/Makefile.lib that has been compiled into the
 * kernel image or KUnit test module. The overlay is automatically removed when
 * the test is finished.
 *
 * Unit tests that need device tree nodes should compile an overlay file with
 * @overlay_name\.dtbo.o in their Makefile along with their unit test and then
 * load the overlay during their test. The @overlay_name matches the filename
 * of the overlay without the dtbo filename extension. If CONFIG_OF_OVERLAY is
 * not enabled, the @test will be skipped.
 *
 * In the Makefile
 *
 * .. code-block:: none
 *
 *	obj-$(CONFIG_OF_OVERLAY_KUNIT_TEST) += overlay_test.o kunit_overlay_test.dtbo.o
 *
 * In the test
 *
 * .. code-block:: c
 *
 *	static void of_overlay_kunit_of_overlay_apply(struct kunit *test)
 *	{
 *		struct device_node *np;
 *
 *		KUNIT_ASSERT_EQ(test, 0,
 *				of_overlay_apply_kunit(test, kunit_overlay_test));
 *
 *		np = of_find_node_by_name(NULL, "test-kunit");
 *		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, np);
 *		of_node_put(np);
 *	}
 *
 * Return: 0 on success, negative errno on failure.
 */
#define of_overlay_apply_kunit(test, overlay_name)		\
({								\
	extern uint8_t __dtbo_##overlay_name##_begin[];		\
	extern uint8_t __dtbo_##overlay_name##_end[];		\
								\
	__of_overlay_apply_kunit((test),			\
			__dtbo_##overlay_name##_begin,		\
			__dtbo_##overlay_name##_end);		\
})

#endif
