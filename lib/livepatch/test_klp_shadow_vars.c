// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/livepatch.h>
#include <linux/slab.h>

/*
 * Keep a small list of pointers so that we can print address-agnostic
 * pointer values.  Use a rolling integer count to differentiate the values.
 * Ironically we could have used the shadow variable API to do this, but
 * let's not lean too heavily on the very code we're testing.
 */
static LIST_HEAD(ptr_list);
struct shadow_ptr {
	void *ptr;
	int id;
	struct list_head list;
};

static void free_ptr_list(void)
{
	struct shadow_ptr *sp, *tmp_sp;

	list_for_each_entry_safe(sp, tmp_sp, &ptr_list, list) {
		list_del(&sp->list);
		kfree(sp);
	}
}

static int ptr_id(void *ptr)
{
	struct shadow_ptr *sp;
	static int count;

	list_for_each_entry(sp, &ptr_list, list) {
		if (sp->ptr == ptr)
			return sp->id;
	}

	sp = kmalloc(sizeof(*sp), GFP_ATOMIC);
	if (!sp)
		return -ENOMEM;
	sp->ptr = ptr;
	sp->id = count++;

	list_add(&sp->list, &ptr_list);

	return sp->id;
}

/*
 * Shadow variable wrapper functions that echo the function and arguments
 * to the kernel log for testing verification.  Don't display raw pointers,
 * but use the ptr_id() value instead.
 */
static void *shadow_get(void *obj, unsigned long id)
{
	int **sv;

	sv = klp_shadow_get(obj, id);
	pr_info("klp_%s(obj=PTR%d, id=0x%lx) = PTR%d\n",
		__func__, ptr_id(obj), id, ptr_id(sv));

	return sv;
}

static void *shadow_alloc(void *obj, unsigned long id, size_t size,
			  gfp_t gfp_flags, klp_shadow_ctor_t ctor,
			  void *ctor_data)
{
	int **var = ctor_data;
	int **sv;

	sv = klp_shadow_alloc(obj, id, size, gfp_flags, ctor, var);
	pr_info("klp_%s(obj=PTR%d, id=0x%lx, size=%zx, gfp_flags=%pGg), ctor=PTR%d, ctor_data=PTR%d = PTR%d\n",
		__func__, ptr_id(obj), id, size, &gfp_flags, ptr_id(ctor),
		ptr_id(*var), ptr_id(sv));

	return sv;
}

static void *shadow_get_or_alloc(void *obj, unsigned long id, size_t size,
				 gfp_t gfp_flags, klp_shadow_ctor_t ctor,
				 void *ctor_data)
{
	int **var = ctor_data;
	int **sv;

	sv = klp_shadow_get_or_alloc(obj, id, size, gfp_flags, ctor, var);
	pr_info("klp_%s(obj=PTR%d, id=0x%lx, size=%zx, gfp_flags=%pGg), ctor=PTR%d, ctor_data=PTR%d = PTR%d\n",
		__func__, ptr_id(obj), id, size, &gfp_flags, ptr_id(ctor),
		ptr_id(*var), ptr_id(sv));

	return sv;
}

static void shadow_free(void *obj, unsigned long id, klp_shadow_dtor_t dtor)
{
	klp_shadow_free(obj, id, dtor);
	pr_info("klp_%s(obj=PTR%d, id=0x%lx, dtor=PTR%d)\n",
		__func__, ptr_id(obj), id, ptr_id(dtor));
}

static void shadow_free_all(unsigned long id, klp_shadow_dtor_t dtor)
{
	klp_shadow_free_all(id, dtor);
	pr_info("klp_%s(id=0x%lx, dtor=PTR%d)\n",
		__func__, id, ptr_id(dtor));
}


/* Shadow variable constructor - remember simple pointer data */
static int shadow_ctor(void *obj, void *shadow_data, void *ctor_data)
{
	int **sv = shadow_data;
	int **var = ctor_data;

	if (!var)
		return -EINVAL;

	*sv = *var;
	pr_info("%s: PTR%d -> PTR%d\n",
		__func__, ptr_id(sv), ptr_id(*var));

	return 0;
}

static void shadow_dtor(void *obj, void *shadow_data)
{
	int **sv = shadow_data;

	pr_info("%s(obj=PTR%d, shadow_data=PTR%d)\n",
		__func__, ptr_id(obj), ptr_id(sv));
}

static int test_klp_shadow_vars_init(void)
{
	void *obj			= THIS_MODULE;
	int id			= 0x1234;
	gfp_t gfp_flags		= GFP_KERNEL;

	int var1, var2, var3, var4;
	int *pv1, *pv2, *pv3, *pv4;
	int **sv1, **sv2, **sv3, **sv4;

	int **sv;

	pv1 = &var1;
	pv2 = &var2;
	pv3 = &var3;
	pv4 = &var4;

	ptr_id(NULL);
	ptr_id(pv1);
	ptr_id(pv2);
	ptr_id(pv3);
	ptr_id(pv4);

	/*
	 * With an empty shadow variable hash table, expect not to find
	 * any matches.
	 */
	sv = shadow_get(obj, id);
	if (!sv)
		pr_info("  got expected NULL result\n");

	/*
	 * Allocate a few shadow variables with different <obj> and <id>.
	 */
	sv1 = shadow_alloc(obj, id, sizeof(pv1), gfp_flags, shadow_ctor, &pv1);
	if (!sv1)
		return -ENOMEM;

	sv2 = shadow_alloc(obj + 1, id, sizeof(pv2), gfp_flags, shadow_ctor, &pv2);
	if (!sv2)
		return -ENOMEM;

	sv3 = shadow_alloc(obj, id + 1, sizeof(pv3), gfp_flags, shadow_ctor, &pv3);
	if (!sv3)
		return -ENOMEM;

	/*
	 * Verify we can find our new shadow variables and that they point
	 * to expected data.
	 */
	sv = shadow_get(obj, id);
	if (!sv)
		return -EINVAL;
	if (sv == sv1 && *sv1 == pv1)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv1), ptr_id(*sv1));

	sv = shadow_get(obj + 1, id);
	if (!sv)
		return -EINVAL;
	if (sv == sv2 && *sv2 == pv2)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv2), ptr_id(*sv2));
	sv = shadow_get(obj, id + 1);
	if (!sv)
		return -EINVAL;
	if (sv == sv3 && *sv3 == pv3)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv3), ptr_id(*sv3));

	/*
	 * Allocate or get a few more, this time with the same <obj>, <id>.
	 * The second invocation should return the same shadow var.
	 */
	sv4 = shadow_get_or_alloc(obj + 2, id, sizeof(pv4), gfp_flags, shadow_ctor, &pv4);
	if (!sv4)
		return -ENOMEM;

	sv = shadow_get_or_alloc(obj + 2, id, sizeof(pv4), gfp_flags, shadow_ctor, &pv4);
	if (!sv)
		return -EINVAL;
	if (sv == sv4 && *sv4 == pv4)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv4), ptr_id(*sv4));

	/*
	 * Free the <obj=*, id> shadow variables and check that we can no
	 * longer find them.
	 */
	shadow_free(obj, id, shadow_dtor);			/* sv1 */
	sv = shadow_get(obj, id);
	if (!sv)
		pr_info("  got expected NULL result\n");

	shadow_free(obj + 1, id, shadow_dtor);			/* sv2 */
	sv = shadow_get(obj + 1, id);
	if (!sv)
		pr_info("  got expected NULL result\n");

	shadow_free(obj + 2, id, shadow_dtor);			/* sv4 */
	sv = shadow_get(obj + 2, id);
	if (!sv)
		pr_info("  got expected NULL result\n");

	/*
	 * We should still find an <id+1> variable.
	 */
	sv = shadow_get(obj, id + 1);
	if (!sv)
		return -EINVAL;
	if (sv == sv3 && *sv3 == pv3)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv3), ptr_id(*sv3));

	/*
	 * Free all the <id+1> variables, too.
	 */
	shadow_free_all(id + 1, shadow_dtor);			/* sv3 */
	sv = shadow_get(obj, id);
	if (!sv)
		pr_info("  shadow_get() got expected NULL result\n");


	free_ptr_list();

	return 0;
}

static void test_klp_shadow_vars_exit(void)
{
}

module_init(test_klp_shadow_vars_init);
module_exit(test_klp_shadow_vars_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: shadow variables");
