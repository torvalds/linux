// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for memcat_p() in lib/memcat_p.c
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/module.h>

struct test_struct {
	int		num;
	unsigned int	magic;
};

#define MAGIC		0xf00ff00f
/* Size of each of the NULL-terminated input arrays */
#define INPUT_MAX	128
/* Expected number of non-NULL elements in the output array */
#define EXPECT		(INPUT_MAX * 2 - 2)

static int __init test_memcat_p_init(void)
{
	struct test_struct **in0, **in1, **out, **p;
	int err = -ENOMEM, i, r, total = 0;

	in0 = kcalloc(INPUT_MAX, sizeof(*in0), GFP_KERNEL);
	if (!in0)
		return err;

	in1 = kcalloc(INPUT_MAX, sizeof(*in1), GFP_KERNEL);
	if (!in1)
		goto err_free_in0;

	for (i = 0, r = 1; i < INPUT_MAX - 1; i++) {
		in0[i] = kmalloc(sizeof(**in0), GFP_KERNEL);
		if (!in0[i])
			goto err_free_elements;

		in1[i] = kmalloc(sizeof(**in1), GFP_KERNEL);
		if (!in1[i]) {
			kfree(in0[i]);
			goto err_free_elements;
		}

		/* lifted from test_sort.c */
		r = (r * 725861) % 6599;
		in0[i]->num = r;
		in1[i]->num = -r;
		in0[i]->magic = MAGIC;
		in1[i]->magic = MAGIC;
	}

	in0[i] = in1[i] = NULL;

	out = memcat_p(in0, in1);
	if (!out)
		goto err_free_all_elements;

	err = -EINVAL;
	for (i = 0, p = out; *p && (i < INPUT_MAX * 2 - 1); p++, i++) {
		total += (*p)->num;

		if ((*p)->magic != MAGIC) {
			pr_err("test failed: wrong magic at %d: %u\n", i,
			       (*p)->magic);
			goto err_free_out;
		}
	}

	if (total) {
		pr_err("test failed: expected zero total, got %d\n", total);
		goto err_free_out;
	}

	if (i != EXPECT) {
		pr_err("test failed: expected output size %d, got %d\n",
		       EXPECT, i);
		goto err_free_out;
	}

	for (i = 0; i < INPUT_MAX - 1; i++)
		if (out[i] != in0[i] || out[i + INPUT_MAX - 1] != in1[i]) {
			pr_err("test failed: wrong element order at %d\n", i);
			goto err_free_out;
		}

	err = 0;
	pr_info("test passed\n");

err_free_out:
	kfree(out);
err_free_all_elements:
	i = INPUT_MAX;
err_free_elements:
	for (i--; i >= 0; i--) {
		kfree(in1[i]);
		kfree(in0[i]);
	}

	kfree(in1);
err_free_in0:
	kfree(in0);

	return err;
}

static void __exit test_memcat_p_exit(void)
{
}

module_init(test_memcat_p_init);
module_exit(test_memcat_p_exit);

MODULE_LICENSE("GPL");
