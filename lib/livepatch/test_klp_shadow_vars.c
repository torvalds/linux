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
	pr_info("klp_%s(id=0x%lx, dtor=PTR%d)\n", __func__, id, ptr_id(dtor));
}


/* Shadow variable constructor - remember simple pointer data */
static int shadow_ctor(void *obj, void *shadow_data, void *ctor_data)
{
	int **sv = shadow_data;
	int **var = ctor_data;

	if (!var)
		return -EINVAL;

	*sv = *var;
	pr_info("%s: PTR%d -> PTR%d\n", __func__, ptr_id(sv), ptr_id(*var));

	return 0;
}

static void shadow_dtor(void *obj, void *shadow_data)
{
	int **sv = shadow_data;

	pr_info("%s(obj=PTR%d, shadow_data=PTR%d)\n",
		__func__, ptr_id(obj), ptr_id(sv));
}

/* dynamically created obj fields have the following shadow var id values */
#define SV_ID1 0x1234
#define SV_ID2 0x1235

/*
 * The main test case adds/removes new fields (shadow var) to each of these
 * test structure instances. The last group of fields in the struct represent
 * the idea that shadow variables may be added and removed to and from the
 * struct during execution.
 */
struct test_object {
	 /* add anything here below and avoid to define an empty struct */
	struct shadow_ptr sp;

	/* these represent shadow vars added and removed with SV_ID{1,2} */
	/* char nfield1; */
	/* int  nfield2; */
};

static int test_klp_shadow_vars_init(void)
{
	struct test_object obj1, obj2, obj3;
	char nfield1, nfield2, *pnfield1, *pnfield2, **sv1, **sv2;
	int  nfield3, nfield4, *pnfield3, *pnfield4, **sv3, **sv4;
	void **sv;

	pnfield1 = &nfield1;
	pnfield2 = &nfield2;
	pnfield3 = &nfield3;
	pnfield4 = &nfield4;

	ptr_id(NULL);
	ptr_id(pnfield1);
	ptr_id(pnfield2);
	ptr_id(pnfield3);
	ptr_id(pnfield4);

	/*
	 * With an empty shadow variable hash table, expect not to find
	 * any matches.
	 */
	sv = shadow_get(&obj1, SV_ID1);
	if (!sv)
		pr_info("  got expected NULL result\n");

	/*
	 * Allocate a few shadow variables with different <obj> and <id>.
	 */
	sv1 = shadow_alloc(&obj1, SV_ID1, sizeof(pnfield1), GFP_KERNEL, shadow_ctor, &pnfield1);
	if (!sv1)
		return -ENOMEM;

	sv2 = shadow_alloc(&obj2, SV_ID1, sizeof(pnfield2), GFP_KERNEL, shadow_ctor, &pnfield2);
	if (!sv2)
		return -ENOMEM;

	sv3 = shadow_alloc(&obj1, SV_ID2, sizeof(pnfield3), GFP_KERNEL, shadow_ctor, &pnfield3);
	if (!sv3)
		return -ENOMEM;

	/*
	 * Verify we can find our new shadow variables and that they point
	 * to expected data.
	 */
	sv = shadow_get(&obj1, SV_ID1);
	if (!sv)
		return -EINVAL;
	if ((char **)sv == sv1 && *sv1 == pnfield1)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv1), ptr_id(*sv1));

	sv = shadow_get(&obj2, SV_ID1);
	if (!sv)
		return -EINVAL;
	if ((char **)sv == sv2 && *sv2 == pnfield2)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv2), ptr_id(*sv2));

	sv = shadow_get(&obj1, SV_ID2);
	if (!sv)
		return -EINVAL;
	if ((int **)sv == sv3 && *sv3 == pnfield3)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv3), ptr_id(*sv3));

	/*
	 * Allocate or get a few more, this time with the same <obj>, <id>.
	 * The second invocation should return the same shadow var.
	 */
	sv4 = shadow_get_or_alloc(&obj3, SV_ID1, sizeof(pnfield4), GFP_KERNEL, shadow_ctor, &pnfield4);
	if (!sv4)
		return -ENOMEM;

	sv = shadow_get_or_alloc(&obj3, SV_ID1, sizeof(pnfield4), GFP_KERNEL, shadow_ctor, &pnfield4);
	if (!sv)
		return -EINVAL;
	if ((int **)sv == sv4 && *sv4 == pnfield4)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv4), ptr_id(*sv4));

	/*
	 * Free the <obj=*, id> shadow variables and check that we can no
	 * longer find them.
	 */
	shadow_free(&obj1, SV_ID1, shadow_dtor);		/* sv1 */
	sv = shadow_get(&obj1, SV_ID1);
	if (!sv)
		pr_info("  got expected NULL result\n");

	shadow_free(&obj2, SV_ID1, shadow_dtor);		/* sv2 */
	sv = shadow_get(&obj2, SV_ID1);
	if (!sv)
		pr_info("  got expected NULL result\n");

	shadow_free(&obj3, SV_ID1, shadow_dtor);		/* sv4 */
	sv = shadow_get(&obj3, SV_ID1);
	if (!sv)
		pr_info("  got expected NULL result\n");

	/*
	 * We should still find an <id+1> variable.
	 */
	sv = shadow_get(&obj1, SV_ID2);
	if (!sv)
		return -EINVAL;
	if ((int **)sv == sv3 && *sv3 == pnfield3)
		pr_info("  got expected PTR%d -> PTR%d result\n",
			ptr_id(sv3), ptr_id(*sv3));

	/*
	 * Free all the <id+1> variables, too.
	 */
	shadow_free_all(SV_ID2, shadow_dtor);			/* sv3 */
	sv = shadow_get(&obj1, SV_ID1);
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
