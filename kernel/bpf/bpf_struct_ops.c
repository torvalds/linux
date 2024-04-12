// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/slab.h>
#include <linux/numa.h>
#include <linux/seq_file.h>
#include <linux/refcount.h>
#include <linux/mutex.h>
#include <linux/btf_ids.h>
#include <linux/rcupdate_wait.h>

struct bpf_struct_ops_value {
	struct bpf_struct_ops_common_value common;
	char data[] ____cacheline_aligned_in_smp;
};

#define MAX_TRAMP_IMAGE_PAGES 8

struct bpf_struct_ops_map {
	struct bpf_map map;
	struct rcu_head rcu;
	const struct bpf_struct_ops_desc *st_ops_desc;
	/* protect map_update */
	struct mutex lock;
	/* link has all the bpf_links that is populated
	 * to the func ptr of the kernel's struct
	 * (in kvalue.data).
	 */
	struct bpf_link **links;
	u32 links_cnt;
	u32 image_pages_cnt;
	/* image_pages is an array of pages that has all the trampolines
	 * that stores the func args before calling the bpf_prog.
	 */
	void *image_pages[MAX_TRAMP_IMAGE_PAGES];
	/* The owner moduler's btf. */
	struct btf *btf;
	/* uvalue->data stores the kernel struct
	 * (e.g. tcp_congestion_ops) that is more useful
	 * to userspace than the kvalue.  For example,
	 * the bpf_prog's id is stored instead of the kernel
	 * address of a func ptr.
	 */
	struct bpf_struct_ops_value *uvalue;
	/* kvalue.data stores the actual kernel's struct
	 * (e.g. tcp_congestion_ops) that will be
	 * registered to the kernel subsystem.
	 */
	struct bpf_struct_ops_value kvalue;
};

struct bpf_struct_ops_link {
	struct bpf_link link;
	struct bpf_map __rcu *map;
};

static DEFINE_MUTEX(update_mutex);

#define VALUE_PREFIX "bpf_struct_ops_"
#define VALUE_PREFIX_LEN (sizeof(VALUE_PREFIX) - 1)

const struct bpf_verifier_ops bpf_struct_ops_verifier_ops = {
};

const struct bpf_prog_ops bpf_struct_ops_prog_ops = {
#ifdef CONFIG_NET
	.test_run = bpf_struct_ops_test_run,
#endif
};

BTF_ID_LIST(st_ops_ids)
BTF_ID(struct, module)
BTF_ID(struct, bpf_struct_ops_common_value)

enum {
	IDX_MODULE_ID,
	IDX_ST_OPS_COMMON_VALUE_ID,
};

extern struct btf *btf_vmlinux;

static bool is_valid_value_type(struct btf *btf, s32 value_id,
				const struct btf_type *type,
				const char *value_name)
{
	const struct btf_type *common_value_type;
	const struct btf_member *member;
	const struct btf_type *vt, *mt;

	vt = btf_type_by_id(btf, value_id);
	if (btf_vlen(vt) != 2) {
		pr_warn("The number of %s's members should be 2, but we get %d\n",
			value_name, btf_vlen(vt));
		return false;
	}
	member = btf_type_member(vt);
	mt = btf_type_by_id(btf, member->type);
	common_value_type = btf_type_by_id(btf_vmlinux,
					   st_ops_ids[IDX_ST_OPS_COMMON_VALUE_ID]);
	if (mt != common_value_type) {
		pr_warn("The first member of %s should be bpf_struct_ops_common_value\n",
			value_name);
		return false;
	}
	member++;
	mt = btf_type_by_id(btf, member->type);
	if (mt != type) {
		pr_warn("The second member of %s should be %s\n",
			value_name, btf_name_by_offset(btf, type->name_off));
		return false;
	}

	return true;
}

static void *bpf_struct_ops_image_alloc(void)
{
	void *image;
	int err;

	err = bpf_jit_charge_modmem(PAGE_SIZE);
	if (err)
		return ERR_PTR(err);
	image = arch_alloc_bpf_trampoline(PAGE_SIZE);
	if (!image) {
		bpf_jit_uncharge_modmem(PAGE_SIZE);
		return ERR_PTR(-ENOMEM);
	}

	return image;
}

void bpf_struct_ops_image_free(void *image)
{
	if (image) {
		arch_free_bpf_trampoline(image, PAGE_SIZE);
		bpf_jit_uncharge_modmem(PAGE_SIZE);
	}
}

#define MAYBE_NULL_SUFFIX "__nullable"
#define MAX_STUB_NAME 128

/* Return the type info of a stub function, if it exists.
 *
 * The name of a stub function is made up of the name of the struct_ops and
 * the name of the function pointer member, separated by "__". For example,
 * if the struct_ops type is named "foo_ops" and the function pointer
 * member is named "bar", the stub function name would be "foo_ops__bar".
 */
static const struct btf_type *
find_stub_func_proto(const struct btf *btf, const char *st_op_name,
		     const char *member_name)
{
	char stub_func_name[MAX_STUB_NAME];
	const struct btf_type *func_type;
	s32 btf_id;
	int cp;

	cp = snprintf(stub_func_name, MAX_STUB_NAME, "%s__%s",
		      st_op_name, member_name);
	if (cp >= MAX_STUB_NAME) {
		pr_warn("Stub function name too long\n");
		return NULL;
	}
	btf_id = btf_find_by_name_kind(btf, stub_func_name, BTF_KIND_FUNC);
	if (btf_id < 0)
		return NULL;
	func_type = btf_type_by_id(btf, btf_id);
	if (!func_type)
		return NULL;

	return btf_type_by_id(btf, func_type->type); /* FUNC_PROTO */
}

/* Prepare argument info for every nullable argument of a member of a
 * struct_ops type.
 *
 * Initialize a struct bpf_struct_ops_arg_info according to type info of
 * the arguments of a stub function. (Check kCFI for more information about
 * stub functions.)
 *
 * Each member in the struct_ops type has a struct bpf_struct_ops_arg_info
 * to provide an array of struct bpf_ctx_arg_aux, which in turn provides
 * the information that used by the verifier to check the arguments of the
 * BPF struct_ops program assigned to the member. Here, we only care about
 * the arguments that are marked as __nullable.
 *
 * The array of struct bpf_ctx_arg_aux is eventually assigned to
 * prog->aux->ctx_arg_info of BPF struct_ops programs and passed to the
 * verifier. (See check_struct_ops_btf_id())
 *
 * arg_info->info will be the list of struct bpf_ctx_arg_aux if success. If
 * fails, it will be kept untouched.
 */
static int prepare_arg_info(struct btf *btf,
			    const char *st_ops_name,
			    const char *member_name,
			    const struct btf_type *func_proto,
			    struct bpf_struct_ops_arg_info *arg_info)
{
	const struct btf_type *stub_func_proto, *pointed_type;
	const struct btf_param *stub_args, *args;
	struct bpf_ctx_arg_aux *info, *info_buf;
	u32 nargs, arg_no, info_cnt = 0;
	u32 arg_btf_id;
	int offset;

	stub_func_proto = find_stub_func_proto(btf, st_ops_name, member_name);
	if (!stub_func_proto)
		return 0;

	/* Check if the number of arguments of the stub function is the same
	 * as the number of arguments of the function pointer.
	 */
	nargs = btf_type_vlen(func_proto);
	if (nargs != btf_type_vlen(stub_func_proto)) {
		pr_warn("the number of arguments of the stub function %s__%s does not match the number of arguments of the member %s of struct %s\n",
			st_ops_name, member_name, member_name, st_ops_name);
		return -EINVAL;
	}

	if (!nargs)
		return 0;

	args = btf_params(func_proto);
	stub_args = btf_params(stub_func_proto);

	info_buf = kcalloc(nargs, sizeof(*info_buf), GFP_KERNEL);
	if (!info_buf)
		return -ENOMEM;

	/* Prepare info for every nullable argument */
	info = info_buf;
	for (arg_no = 0; arg_no < nargs; arg_no++) {
		/* Skip arguments that is not suffixed with
		 * "__nullable".
		 */
		if (!btf_param_match_suffix(btf, &stub_args[arg_no],
					    MAYBE_NULL_SUFFIX))
			continue;

		/* Should be a pointer to struct */
		pointed_type = btf_type_resolve_ptr(btf,
						    args[arg_no].type,
						    &arg_btf_id);
		if (!pointed_type ||
		    !btf_type_is_struct(pointed_type)) {
			pr_warn("stub function %s__%s has %s tagging to an unsupported type\n",
				st_ops_name, member_name, MAYBE_NULL_SUFFIX);
			goto err_out;
		}

		offset = btf_ctx_arg_offset(btf, func_proto, arg_no);
		if (offset < 0) {
			pr_warn("stub function %s__%s has an invalid trampoline ctx offset for arg#%u\n",
				st_ops_name, member_name, arg_no);
			goto err_out;
		}

		if (args[arg_no].type != stub_args[arg_no].type) {
			pr_warn("arg#%u type in stub function %s__%s does not match with its original func_proto\n",
				arg_no, st_ops_name, member_name);
			goto err_out;
		}

		/* Fill the information of the new argument */
		info->reg_type =
			PTR_TRUSTED | PTR_TO_BTF_ID | PTR_MAYBE_NULL;
		info->btf_id = arg_btf_id;
		info->btf = btf;
		info->offset = offset;

		info++;
		info_cnt++;
	}

	if (info_cnt) {
		arg_info->info = info_buf;
		arg_info->cnt = info_cnt;
	} else {
		kfree(info_buf);
	}

	return 0;

err_out:
	kfree(info_buf);

	return -EINVAL;
}

/* Clean up the arg_info in a struct bpf_struct_ops_desc. */
void bpf_struct_ops_desc_release(struct bpf_struct_ops_desc *st_ops_desc)
{
	struct bpf_struct_ops_arg_info *arg_info;
	int i;

	arg_info = st_ops_desc->arg_info;
	for (i = 0; i < btf_type_vlen(st_ops_desc->type); i++)
		kfree(arg_info[i].info);

	kfree(arg_info);
}

int bpf_struct_ops_desc_init(struct bpf_struct_ops_desc *st_ops_desc,
			     struct btf *btf,
			     struct bpf_verifier_log *log)
{
	struct bpf_struct_ops *st_ops = st_ops_desc->st_ops;
	struct bpf_struct_ops_arg_info *arg_info;
	const struct btf_member *member;
	const struct btf_type *t;
	s32 type_id, value_id;
	char value_name[128];
	const char *mname;
	int i, err;

	if (strlen(st_ops->name) + VALUE_PREFIX_LEN >=
	    sizeof(value_name)) {
		pr_warn("struct_ops name %s is too long\n",
			st_ops->name);
		return -EINVAL;
	}
	sprintf(value_name, "%s%s", VALUE_PREFIX, st_ops->name);

	if (!st_ops->cfi_stubs) {
		pr_warn("struct_ops for %s has no cfi_stubs\n", st_ops->name);
		return -EINVAL;
	}

	type_id = btf_find_by_name_kind(btf, st_ops->name,
					BTF_KIND_STRUCT);
	if (type_id < 0) {
		pr_warn("Cannot find struct %s in %s\n",
			st_ops->name, btf_get_name(btf));
		return -EINVAL;
	}
	t = btf_type_by_id(btf, type_id);
	if (btf_type_vlen(t) > BPF_STRUCT_OPS_MAX_NR_MEMBERS) {
		pr_warn("Cannot support #%u members in struct %s\n",
			btf_type_vlen(t), st_ops->name);
		return -EINVAL;
	}

	value_id = btf_find_by_name_kind(btf, value_name,
					 BTF_KIND_STRUCT);
	if (value_id < 0) {
		pr_warn("Cannot find struct %s in %s\n",
			value_name, btf_get_name(btf));
		return -EINVAL;
	}
	if (!is_valid_value_type(btf, value_id, t, value_name))
		return -EINVAL;

	arg_info = kcalloc(btf_type_vlen(t), sizeof(*arg_info),
			   GFP_KERNEL);
	if (!arg_info)
		return -ENOMEM;

	st_ops_desc->arg_info = arg_info;
	st_ops_desc->type = t;
	st_ops_desc->type_id = type_id;
	st_ops_desc->value_id = value_id;
	st_ops_desc->value_type = btf_type_by_id(btf, value_id);

	for_each_member(i, t, member) {
		const struct btf_type *func_proto;

		mname = btf_name_by_offset(btf, member->name_off);
		if (!*mname) {
			pr_warn("anon member in struct %s is not supported\n",
				st_ops->name);
			err = -EOPNOTSUPP;
			goto errout;
		}

		if (__btf_member_bitfield_size(t, member)) {
			pr_warn("bit field member %s in struct %s is not supported\n",
				mname, st_ops->name);
			err = -EOPNOTSUPP;
			goto errout;
		}

		func_proto = btf_type_resolve_func_ptr(btf,
						       member->type,
						       NULL);
		if (!func_proto)
			continue;

		if (btf_distill_func_proto(log, btf,
					   func_proto, mname,
					   &st_ops->func_models[i])) {
			pr_warn("Error in parsing func ptr %s in struct %s\n",
				mname, st_ops->name);
			err = -EINVAL;
			goto errout;
		}

		err = prepare_arg_info(btf, st_ops->name, mname,
				       func_proto,
				       arg_info + i);
		if (err)
			goto errout;
	}

	if (st_ops->init(btf)) {
		pr_warn("Error in init bpf_struct_ops %s\n",
			st_ops->name);
		err = -EINVAL;
		goto errout;
	}

	return 0;

errout:
	bpf_struct_ops_desc_release(st_ops_desc);

	return err;
}

static int bpf_struct_ops_map_get_next_key(struct bpf_map *map, void *key,
					   void *next_key)
{
	if (key && *(u32 *)key == 0)
		return -ENOENT;

	*(u32 *)next_key = 0;
	return 0;
}

int bpf_struct_ops_map_sys_lookup_elem(struct bpf_map *map, void *key,
				       void *value)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;
	struct bpf_struct_ops_value *uvalue, *kvalue;
	enum bpf_struct_ops_state state;
	s64 refcnt;

	if (unlikely(*(u32 *)key != 0))
		return -ENOENT;

	kvalue = &st_map->kvalue;
	/* Pair with smp_store_release() during map_update */
	state = smp_load_acquire(&kvalue->common.state);
	if (state == BPF_STRUCT_OPS_STATE_INIT) {
		memset(value, 0, map->value_size);
		return 0;
	}

	/* No lock is needed.  state and refcnt do not need
	 * to be updated together under atomic context.
	 */
	uvalue = value;
	memcpy(uvalue, st_map->uvalue, map->value_size);
	uvalue->common.state = state;

	/* This value offers the user space a general estimate of how
	 * many sockets are still utilizing this struct_ops for TCP
	 * congestion control. The number might not be exact, but it
	 * should sufficiently meet our present goals.
	 */
	refcnt = atomic64_read(&map->refcnt) - atomic64_read(&map->usercnt);
	refcount_set(&uvalue->common.refcnt, max_t(s64, refcnt, 0));

	return 0;
}

static void *bpf_struct_ops_map_lookup_elem(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EINVAL);
}

static void bpf_struct_ops_map_put_progs(struct bpf_struct_ops_map *st_map)
{
	u32 i;

	for (i = 0; i < st_map->links_cnt; i++) {
		if (st_map->links[i]) {
			bpf_link_put(st_map->links[i]);
			st_map->links[i] = NULL;
		}
	}
}

static void bpf_struct_ops_map_free_image(struct bpf_struct_ops_map *st_map)
{
	int i;

	for (i = 0; i < st_map->image_pages_cnt; i++)
		bpf_struct_ops_image_free(st_map->image_pages[i]);
	st_map->image_pages_cnt = 0;
}

static int check_zero_holes(const struct btf *btf, const struct btf_type *t, void *data)
{
	const struct btf_member *member;
	u32 i, moff, msize, prev_mend = 0;
	const struct btf_type *mtype;

	for_each_member(i, t, member) {
		moff = __btf_member_bit_offset(t, member) / 8;
		if (moff > prev_mend &&
		    memchr_inv(data + prev_mend, 0, moff - prev_mend))
			return -EINVAL;

		mtype = btf_type_by_id(btf, member->type);
		mtype = btf_resolve_size(btf, mtype, &msize);
		if (IS_ERR(mtype))
			return PTR_ERR(mtype);
		prev_mend = moff + msize;
	}

	if (t->size > prev_mend &&
	    memchr_inv(data + prev_mend, 0, t->size - prev_mend))
		return -EINVAL;

	return 0;
}

static void bpf_struct_ops_link_release(struct bpf_link *link)
{
}

static void bpf_struct_ops_link_dealloc(struct bpf_link *link)
{
	struct bpf_tramp_link *tlink = container_of(link, struct bpf_tramp_link, link);

	kfree(tlink);
}

const struct bpf_link_ops bpf_struct_ops_link_lops = {
	.release = bpf_struct_ops_link_release,
	.dealloc = bpf_struct_ops_link_dealloc,
};

int bpf_struct_ops_prepare_trampoline(struct bpf_tramp_links *tlinks,
				      struct bpf_tramp_link *link,
				      const struct btf_func_model *model,
				      void *stub_func,
				      void **_image, u32 *_image_off,
				      bool allow_alloc)
{
	u32 image_off = *_image_off, flags = BPF_TRAMP_F_INDIRECT;
	void *image = *_image;
	int size;

	tlinks[BPF_TRAMP_FENTRY].links[0] = link;
	tlinks[BPF_TRAMP_FENTRY].nr_links = 1;

	if (model->ret_size > 0)
		flags |= BPF_TRAMP_F_RET_FENTRY_RET;

	size = arch_bpf_trampoline_size(model, flags, tlinks, NULL);
	if (size <= 0)
		return size ? : -EFAULT;

	/* Allocate image buffer if necessary */
	if (!image || size > PAGE_SIZE - image_off) {
		if (!allow_alloc)
			return -E2BIG;

		image = bpf_struct_ops_image_alloc();
		if (IS_ERR(image))
			return PTR_ERR(image);
		image_off = 0;
	}

	size = arch_prepare_bpf_trampoline(NULL, image + image_off,
					   image + PAGE_SIZE,
					   model, flags, tlinks, stub_func);
	if (size <= 0) {
		if (image != *_image)
			bpf_struct_ops_image_free(image);
		return size ? : -EFAULT;
	}

	*_image = image;
	*_image_off = image_off + size;
	return 0;
}

static long bpf_struct_ops_map_update_elem(struct bpf_map *map, void *key,
					   void *value, u64 flags)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;
	const struct bpf_struct_ops_desc *st_ops_desc = st_map->st_ops_desc;
	const struct bpf_struct_ops *st_ops = st_ops_desc->st_ops;
	struct bpf_struct_ops_value *uvalue, *kvalue;
	const struct btf_type *module_type;
	const struct btf_member *member;
	const struct btf_type *t = st_ops_desc->type;
	struct bpf_tramp_links *tlinks;
	void *udata, *kdata;
	int prog_fd, err;
	u32 i, trampoline_start, image_off = 0;
	void *cur_image = NULL, *image = NULL;

	if (flags)
		return -EINVAL;

	if (*(u32 *)key != 0)
		return -E2BIG;

	err = check_zero_holes(st_map->btf, st_ops_desc->value_type, value);
	if (err)
		return err;

	uvalue = value;
	err = check_zero_holes(st_map->btf, t, uvalue->data);
	if (err)
		return err;

	if (uvalue->common.state || refcount_read(&uvalue->common.refcnt))
		return -EINVAL;

	tlinks = kcalloc(BPF_TRAMP_MAX, sizeof(*tlinks), GFP_KERNEL);
	if (!tlinks)
		return -ENOMEM;

	uvalue = (struct bpf_struct_ops_value *)st_map->uvalue;
	kvalue = (struct bpf_struct_ops_value *)&st_map->kvalue;

	mutex_lock(&st_map->lock);

	if (kvalue->common.state != BPF_STRUCT_OPS_STATE_INIT) {
		err = -EBUSY;
		goto unlock;
	}

	memcpy(uvalue, value, map->value_size);

	udata = &uvalue->data;
	kdata = &kvalue->data;

	module_type = btf_type_by_id(btf_vmlinux, st_ops_ids[IDX_MODULE_ID]);
	for_each_member(i, t, member) {
		const struct btf_type *mtype, *ptype;
		struct bpf_prog *prog;
		struct bpf_tramp_link *link;
		u32 moff;

		moff = __btf_member_bit_offset(t, member) / 8;
		ptype = btf_type_resolve_ptr(st_map->btf, member->type, NULL);
		if (ptype == module_type) {
			if (*(void **)(udata + moff))
				goto reset_unlock;
			*(void **)(kdata + moff) = BPF_MODULE_OWNER;
			continue;
		}

		err = st_ops->init_member(t, member, kdata, udata);
		if (err < 0)
			goto reset_unlock;

		/* The ->init_member() has handled this member */
		if (err > 0)
			continue;

		/* If st_ops->init_member does not handle it,
		 * we will only handle func ptrs and zero-ed members
		 * here.  Reject everything else.
		 */

		/* All non func ptr member must be 0 */
		if (!ptype || !btf_type_is_func_proto(ptype)) {
			u32 msize;

			mtype = btf_type_by_id(st_map->btf, member->type);
			mtype = btf_resolve_size(st_map->btf, mtype, &msize);
			if (IS_ERR(mtype)) {
				err = PTR_ERR(mtype);
				goto reset_unlock;
			}

			if (memchr_inv(udata + moff, 0, msize)) {
				err = -EINVAL;
				goto reset_unlock;
			}

			continue;
		}

		prog_fd = (int)(*(unsigned long *)(udata + moff));
		/* Similar check as the attr->attach_prog_fd */
		if (!prog_fd)
			continue;

		prog = bpf_prog_get(prog_fd);
		if (IS_ERR(prog)) {
			err = PTR_ERR(prog);
			goto reset_unlock;
		}

		if (prog->type != BPF_PROG_TYPE_STRUCT_OPS ||
		    prog->aux->attach_btf_id != st_ops_desc->type_id ||
		    prog->expected_attach_type != i) {
			bpf_prog_put(prog);
			err = -EINVAL;
			goto reset_unlock;
		}

		link = kzalloc(sizeof(*link), GFP_USER);
		if (!link) {
			bpf_prog_put(prog);
			err = -ENOMEM;
			goto reset_unlock;
		}
		bpf_link_init(&link->link, BPF_LINK_TYPE_STRUCT_OPS,
			      &bpf_struct_ops_link_lops, prog);
		st_map->links[i] = &link->link;

		trampoline_start = image_off;
		err = bpf_struct_ops_prepare_trampoline(tlinks, link,
						&st_ops->func_models[i],
						*(void **)(st_ops->cfi_stubs + moff),
						&image, &image_off,
						st_map->image_pages_cnt < MAX_TRAMP_IMAGE_PAGES);
		if (err)
			goto reset_unlock;

		if (cur_image != image) {
			st_map->image_pages[st_map->image_pages_cnt++] = image;
			cur_image = image;
			trampoline_start = 0;
		}
		if (err < 0)
			goto reset_unlock;

		*(void **)(kdata + moff) = image + trampoline_start + cfi_get_offset();

		/* put prog_id to udata */
		*(unsigned long *)(udata + moff) = prog->aux->id;
	}

	if (st_ops->validate) {
		err = st_ops->validate(kdata);
		if (err)
			goto reset_unlock;
	}
	for (i = 0; i < st_map->image_pages_cnt; i++)
		arch_protect_bpf_trampoline(st_map->image_pages[i], PAGE_SIZE);

	if (st_map->map.map_flags & BPF_F_LINK) {
		err = 0;
		/* Let bpf_link handle registration & unregistration.
		 *
		 * Pair with smp_load_acquire() during lookup_elem().
		 */
		smp_store_release(&kvalue->common.state, BPF_STRUCT_OPS_STATE_READY);
		goto unlock;
	}

	err = st_ops->reg(kdata);
	if (likely(!err)) {
		/* This refcnt increment on the map here after
		 * 'st_ops->reg()' is secure since the state of the
		 * map must be set to INIT at this moment, and thus
		 * bpf_struct_ops_map_delete_elem() can't unregister
		 * or transition it to TOBEFREE concurrently.
		 */
		bpf_map_inc(map);
		/* Pair with smp_load_acquire() during lookup_elem().
		 * It ensures the above udata updates (e.g. prog->aux->id)
		 * can be seen once BPF_STRUCT_OPS_STATE_INUSE is set.
		 */
		smp_store_release(&kvalue->common.state, BPF_STRUCT_OPS_STATE_INUSE);
		goto unlock;
	}

	/* Error during st_ops->reg(). Can happen if this struct_ops needs to be
	 * verified as a whole, after all init_member() calls. Can also happen if
	 * there was a race in registering the struct_ops (under the same name) to
	 * a sub-system through different struct_ops's maps.
	 */

reset_unlock:
	bpf_struct_ops_map_free_image(st_map);
	bpf_struct_ops_map_put_progs(st_map);
	memset(uvalue, 0, map->value_size);
	memset(kvalue, 0, map->value_size);
unlock:
	kfree(tlinks);
	mutex_unlock(&st_map->lock);
	return err;
}

static long bpf_struct_ops_map_delete_elem(struct bpf_map *map, void *key)
{
	enum bpf_struct_ops_state prev_state;
	struct bpf_struct_ops_map *st_map;

	st_map = (struct bpf_struct_ops_map *)map;
	if (st_map->map.map_flags & BPF_F_LINK)
		return -EOPNOTSUPP;

	prev_state = cmpxchg(&st_map->kvalue.common.state,
			     BPF_STRUCT_OPS_STATE_INUSE,
			     BPF_STRUCT_OPS_STATE_TOBEFREE);
	switch (prev_state) {
	case BPF_STRUCT_OPS_STATE_INUSE:
		st_map->st_ops_desc->st_ops->unreg(&st_map->kvalue.data);
		bpf_map_put(map);
		return 0;
	case BPF_STRUCT_OPS_STATE_TOBEFREE:
		return -EINPROGRESS;
	case BPF_STRUCT_OPS_STATE_INIT:
		return -ENOENT;
	default:
		WARN_ON_ONCE(1);
		/* Should never happen.  Treat it as not found. */
		return -ENOENT;
	}
}

static void bpf_struct_ops_map_seq_show_elem(struct bpf_map *map, void *key,
					     struct seq_file *m)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;
	void *value;
	int err;

	value = kmalloc(map->value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		return;

	err = bpf_struct_ops_map_sys_lookup_elem(map, key, value);
	if (!err) {
		btf_type_seq_show(st_map->btf,
				  map->btf_vmlinux_value_type_id,
				  value, m);
		seq_puts(m, "\n");
	}

	kfree(value);
}

static void __bpf_struct_ops_map_free(struct bpf_map *map)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;

	if (st_map->links)
		bpf_struct_ops_map_put_progs(st_map);
	bpf_map_area_free(st_map->links);
	bpf_struct_ops_map_free_image(st_map);
	bpf_map_area_free(st_map->uvalue);
	bpf_map_area_free(st_map);
}

static void bpf_struct_ops_map_free(struct bpf_map *map)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;

	/* st_ops->owner was acquired during map_alloc to implicitly holds
	 * the btf's refcnt. The acquire was only done when btf_is_module()
	 * st_map->btf cannot be NULL here.
	 */
	if (btf_is_module(st_map->btf))
		module_put(st_map->st_ops_desc->st_ops->owner);

	/* The struct_ops's function may switch to another struct_ops.
	 *
	 * For example, bpf_tcp_cc_x->init() may switch to
	 * another tcp_cc_y by calling
	 * setsockopt(TCP_CONGESTION, "tcp_cc_y").
	 * During the switch,  bpf_struct_ops_put(tcp_cc_x) is called
	 * and its refcount may reach 0 which then free its
	 * trampoline image while tcp_cc_x is still running.
	 *
	 * A vanilla rcu gp is to wait for all bpf-tcp-cc prog
	 * to finish. bpf-tcp-cc prog is non sleepable.
	 * A rcu_tasks gp is to wait for the last few insn
	 * in the tramopline image to finish before releasing
	 * the trampoline image.
	 */
	synchronize_rcu_mult(call_rcu, call_rcu_tasks);

	__bpf_struct_ops_map_free(map);
}

static int bpf_struct_ops_map_alloc_check(union bpf_attr *attr)
{
	if (attr->key_size != sizeof(unsigned int) || attr->max_entries != 1 ||
	    (attr->map_flags & ~(BPF_F_LINK | BPF_F_VTYPE_BTF_OBJ_FD)) ||
	    !attr->btf_vmlinux_value_type_id)
		return -EINVAL;
	return 0;
}

static struct bpf_map *bpf_struct_ops_map_alloc(union bpf_attr *attr)
{
	const struct bpf_struct_ops_desc *st_ops_desc;
	size_t st_map_size;
	struct bpf_struct_ops_map *st_map;
	const struct btf_type *t, *vt;
	struct module *mod = NULL;
	struct bpf_map *map;
	struct btf *btf;
	int ret;

	if (attr->map_flags & BPF_F_VTYPE_BTF_OBJ_FD) {
		/* The map holds btf for its whole life time. */
		btf = btf_get_by_fd(attr->value_type_btf_obj_fd);
		if (IS_ERR(btf))
			return ERR_CAST(btf);
		if (!btf_is_module(btf)) {
			btf_put(btf);
			return ERR_PTR(-EINVAL);
		}

		mod = btf_try_get_module(btf);
		/* mod holds a refcnt to btf. We don't need an extra refcnt
		 * here.
		 */
		btf_put(btf);
		if (!mod)
			return ERR_PTR(-EINVAL);
	} else {
		btf = bpf_get_btf_vmlinux();
		if (IS_ERR(btf))
			return ERR_CAST(btf);
		if (!btf)
			return ERR_PTR(-ENOTSUPP);
	}

	st_ops_desc = bpf_struct_ops_find_value(btf, attr->btf_vmlinux_value_type_id);
	if (!st_ops_desc) {
		ret = -ENOTSUPP;
		goto errout;
	}

	vt = st_ops_desc->value_type;
	if (attr->value_size != vt->size) {
		ret = -EINVAL;
		goto errout;
	}

	t = st_ops_desc->type;

	st_map_size = sizeof(*st_map) +
		/* kvalue stores the
		 * struct bpf_struct_ops_tcp_congestions_ops
		 */
		(vt->size - sizeof(struct bpf_struct_ops_value));

	st_map = bpf_map_area_alloc(st_map_size, NUMA_NO_NODE);
	if (!st_map) {
		ret = -ENOMEM;
		goto errout;
	}

	st_map->st_ops_desc = st_ops_desc;
	map = &st_map->map;

	st_map->uvalue = bpf_map_area_alloc(vt->size, NUMA_NO_NODE);
	st_map->links_cnt = btf_type_vlen(t);
	st_map->links =
		bpf_map_area_alloc(st_map->links_cnt * sizeof(struct bpf_links *),
				   NUMA_NO_NODE);
	if (!st_map->uvalue || !st_map->links) {
		ret = -ENOMEM;
		goto errout_free;
	}
	st_map->btf = btf;

	mutex_init(&st_map->lock);
	bpf_map_init_from_attr(map, attr);

	return map;

errout_free:
	__bpf_struct_ops_map_free(map);
errout:
	module_put(mod);

	return ERR_PTR(ret);
}

static u64 bpf_struct_ops_map_mem_usage(const struct bpf_map *map)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;
	const struct bpf_struct_ops_desc *st_ops_desc = st_map->st_ops_desc;
	const struct btf_type *vt = st_ops_desc->value_type;
	u64 usage;

	usage = sizeof(*st_map) +
			vt->size - sizeof(struct bpf_struct_ops_value);
	usage += vt->size;
	usage += btf_type_vlen(vt) * sizeof(struct bpf_links *);
	usage += PAGE_SIZE;
	return usage;
}

BTF_ID_LIST_SINGLE(bpf_struct_ops_map_btf_ids, struct, bpf_struct_ops_map)
const struct bpf_map_ops bpf_struct_ops_map_ops = {
	.map_alloc_check = bpf_struct_ops_map_alloc_check,
	.map_alloc = bpf_struct_ops_map_alloc,
	.map_free = bpf_struct_ops_map_free,
	.map_get_next_key = bpf_struct_ops_map_get_next_key,
	.map_lookup_elem = bpf_struct_ops_map_lookup_elem,
	.map_delete_elem = bpf_struct_ops_map_delete_elem,
	.map_update_elem = bpf_struct_ops_map_update_elem,
	.map_seq_show_elem = bpf_struct_ops_map_seq_show_elem,
	.map_mem_usage = bpf_struct_ops_map_mem_usage,
	.map_btf_id = &bpf_struct_ops_map_btf_ids[0],
};

/* "const void *" because some subsystem is
 * passing a const (e.g. const struct tcp_congestion_ops *)
 */
bool bpf_struct_ops_get(const void *kdata)
{
	struct bpf_struct_ops_value *kvalue;
	struct bpf_struct_ops_map *st_map;
	struct bpf_map *map;

	kvalue = container_of(kdata, struct bpf_struct_ops_value, data);
	st_map = container_of(kvalue, struct bpf_struct_ops_map, kvalue);

	map = __bpf_map_inc_not_zero(&st_map->map, false);
	return !IS_ERR(map);
}

void bpf_struct_ops_put(const void *kdata)
{
	struct bpf_struct_ops_value *kvalue;
	struct bpf_struct_ops_map *st_map;

	kvalue = container_of(kdata, struct bpf_struct_ops_value, data);
	st_map = container_of(kvalue, struct bpf_struct_ops_map, kvalue);

	bpf_map_put(&st_map->map);
}

static bool bpf_struct_ops_valid_to_reg(struct bpf_map *map)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;

	return map->map_type == BPF_MAP_TYPE_STRUCT_OPS &&
		map->map_flags & BPF_F_LINK &&
		/* Pair with smp_store_release() during map_update */
		smp_load_acquire(&st_map->kvalue.common.state) == BPF_STRUCT_OPS_STATE_READY;
}

static void bpf_struct_ops_map_link_dealloc(struct bpf_link *link)
{
	struct bpf_struct_ops_link *st_link;
	struct bpf_struct_ops_map *st_map;

	st_link = container_of(link, struct bpf_struct_ops_link, link);
	st_map = (struct bpf_struct_ops_map *)
		rcu_dereference_protected(st_link->map, true);
	if (st_map) {
		/* st_link->map can be NULL if
		 * bpf_struct_ops_link_create() fails to register.
		 */
		st_map->st_ops_desc->st_ops->unreg(&st_map->kvalue.data);
		bpf_map_put(&st_map->map);
	}
	kfree(st_link);
}

static void bpf_struct_ops_map_link_show_fdinfo(const struct bpf_link *link,
					    struct seq_file *seq)
{
	struct bpf_struct_ops_link *st_link;
	struct bpf_map *map;

	st_link = container_of(link, struct bpf_struct_ops_link, link);
	rcu_read_lock();
	map = rcu_dereference(st_link->map);
	seq_printf(seq, "map_id:\t%d\n", map->id);
	rcu_read_unlock();
}

static int bpf_struct_ops_map_link_fill_link_info(const struct bpf_link *link,
					       struct bpf_link_info *info)
{
	struct bpf_struct_ops_link *st_link;
	struct bpf_map *map;

	st_link = container_of(link, struct bpf_struct_ops_link, link);
	rcu_read_lock();
	map = rcu_dereference(st_link->map);
	info->struct_ops.map_id = map->id;
	rcu_read_unlock();
	return 0;
}

static int bpf_struct_ops_map_link_update(struct bpf_link *link, struct bpf_map *new_map,
					  struct bpf_map *expected_old_map)
{
	struct bpf_struct_ops_map *st_map, *old_st_map;
	struct bpf_map *old_map;
	struct bpf_struct_ops_link *st_link;
	int err;

	st_link = container_of(link, struct bpf_struct_ops_link, link);
	st_map = container_of(new_map, struct bpf_struct_ops_map, map);

	if (!bpf_struct_ops_valid_to_reg(new_map))
		return -EINVAL;

	if (!st_map->st_ops_desc->st_ops->update)
		return -EOPNOTSUPP;

	mutex_lock(&update_mutex);

	old_map = rcu_dereference_protected(st_link->map, lockdep_is_held(&update_mutex));
	if (expected_old_map && old_map != expected_old_map) {
		err = -EPERM;
		goto err_out;
	}

	old_st_map = container_of(old_map, struct bpf_struct_ops_map, map);
	/* The new and old struct_ops must be the same type. */
	if (st_map->st_ops_desc != old_st_map->st_ops_desc) {
		err = -EINVAL;
		goto err_out;
	}

	err = st_map->st_ops_desc->st_ops->update(st_map->kvalue.data, old_st_map->kvalue.data);
	if (err)
		goto err_out;

	bpf_map_inc(new_map);
	rcu_assign_pointer(st_link->map, new_map);
	bpf_map_put(old_map);

err_out:
	mutex_unlock(&update_mutex);

	return err;
}

static const struct bpf_link_ops bpf_struct_ops_map_lops = {
	.dealloc = bpf_struct_ops_map_link_dealloc,
	.show_fdinfo = bpf_struct_ops_map_link_show_fdinfo,
	.fill_link_info = bpf_struct_ops_map_link_fill_link_info,
	.update_map = bpf_struct_ops_map_link_update,
};

int bpf_struct_ops_link_create(union bpf_attr *attr)
{
	struct bpf_struct_ops_link *link = NULL;
	struct bpf_link_primer link_primer;
	struct bpf_struct_ops_map *st_map;
	struct bpf_map *map;
	int err;

	map = bpf_map_get(attr->link_create.map_fd);
	if (IS_ERR(map))
		return PTR_ERR(map);

	st_map = (struct bpf_struct_ops_map *)map;

	if (!bpf_struct_ops_valid_to_reg(map)) {
		err = -EINVAL;
		goto err_out;
	}

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto err_out;
	}
	bpf_link_init(&link->link, BPF_LINK_TYPE_STRUCT_OPS, &bpf_struct_ops_map_lops, NULL);

	err = bpf_link_prime(&link->link, &link_primer);
	if (err)
		goto err_out;

	err = st_map->st_ops_desc->st_ops->reg(st_map->kvalue.data);
	if (err) {
		bpf_link_cleanup(&link_primer);
		link = NULL;
		goto err_out;
	}
	RCU_INIT_POINTER(link->map, map);

	return bpf_link_settle(&link_primer);

err_out:
	bpf_map_put(map);
	kfree(link);
	return err;
}

void bpf_map_struct_ops_info_fill(struct bpf_map_info *info, struct bpf_map *map)
{
	struct bpf_struct_ops_map *st_map = (struct bpf_struct_ops_map *)map;

	info->btf_vmlinux_id = btf_obj_id(st_map->btf);
}
