// SPDX-License-Identifier: GPL-2.0-only
/*
 * BPF-backed binary type handlers for binfmt_misc.
 *
 * A handler is a struct binfmt_misc_ops struct_ops map. Loading and
 * registering it makes the handler available under its name in the user
 * namespace it was registered in. A binfmt_misc 'B' entry activates it:
 *
 *   echo ':entry:B::::<handler-name>:' > <binfmt_misc>/register
 *
 * The entry can bind the interpreters the handler may run its binaries
 * with, each opened by the write that binds it and selected by name per
 * exec. An entry registered with 'D' is not matchable yet, which is what
 * leaves it open to being given them:
 *
 *   echo ':entry:B::::<handler-name>:D' > <binfmt_misc>/register
 *   echo '+<name> <path>' > <binfmt_misc>/entry
 *   echo 1 > <binfmt_misc>/entry
 */

#include <linux/binfmt_misc.h>
#include <linux/binfmts.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/user_namespace.h>

struct bm_bpf_ops_reg {
	struct list_head list;
	const struct binfmt_misc_ops *ops;
	struct bpf_link *link;
	struct user_namespace *user_ns;
};

static DEFINE_SPINLOCK(bm_bpf_ops_lock);
static LIST_HEAD(bm_bpf_ops_list);

static struct bpf_struct_ops bpf_binfmt_misc_ops;

static struct bm_bpf_ops_reg *bm_bpf_ops_find(const struct user_namespace *user_ns,
					      const char *name)
{
	struct bm_bpf_ops_reg *reg;

	lockdep_assert_held(&bm_bpf_ops_lock);

	list_for_each_entry(reg, &bm_bpf_ops_list, list) {
		if (reg->user_ns == user_ns && !strcmp(reg->ops->name, name))
			return reg;
	}
	return NULL;
}

/**
 * binfmt_misc_get_ops - look up a bpf binary type handler by name
 * @user_ns: user namespace of the binfmt_misc instance
 * @name: name the handler was registered under
 *
 * Look for a handler named @name registered in @user_ns. A handler is not
 * inherited from ancestor user namespaces: an entry can only name a handler
 * registered in the same user namespace as its instance. The returned handler
 * stays callable until binfmt_misc_put_ops() even if the backing struct_ops
 * map is detached or deleted in the meantime.
 *
 * Return: the handler on success, NULL on failure
 */
const struct binfmt_misc_ops *binfmt_misc_get_ops(struct user_namespace *user_ns,
						  const char *name)
{
	struct bm_bpf_ops_reg *reg;

	guard(spinlock)(&bm_bpf_ops_lock);

	reg = bm_bpf_ops_find(user_ns, name);
	if (!reg)
		return NULL;
	if (!bpf_struct_ops_get(reg->ops))
		return NULL;
	return reg->ops;
}

void binfmt_misc_put_ops(const struct binfmt_misc_ops *ops)
{
	bpf_struct_ops_put(ops);
}

bool bpf_prog_is_binfmt_misc_ops(const struct bpf_prog *prog)
{
	return prog->type == BPF_PROG_TYPE_STRUCT_OPS &&
	       prog->aux->st_ops == &bpf_binfmt_misc_ops;
}

/*
 * Replace the staged interpreter selection: naming a path drops a bound
 * file, selecting a bound interpreter carries its file along.
 */
static void bm_bpf_stage_selection(struct linux_binprm *bprm, char *path,
				   struct file *f)
{
	if (bprm->bpf_interp_file)
		fput(bprm->bpf_interp_file);
	kfree(bprm->bpf_interp);
	bprm->bpf_interp = path;
	bprm->bpf_interp_file = f;
}

__bpf_kfunc_start_defs();

/**
 * bpf_binprm_set_interp - select the interpreter for the current exec
 * @bprm: binary that is being executed
 * @path: absolute path to the interpreter
 * @path__sz: size of the @path buffer, including the terminating NUL
 *
 * To be called from the load program of a struct binfmt_misc_ops handler
 * before returning zero; the verifier rejects the call from any other
 * program, including the handler's own match program. The path is opened
 * with the credentials of the task doing the exec after the program
 * returns. Calling it again replaces the selection, as does selecting an
 * interpreter the entry bound with bpf_binprm_select_interp().
 *
 * Return: 0 on success, a negative errno on failure
 */
__bpf_kfunc int bpf_binprm_set_interp(struct linux_binprm *bprm,
				      const char *path, size_t path__sz)
{
	size_t len;
	char *interp;

	if (!path__sz)
		return -EINVAL;
	len = strnlen(path, path__sz);
	if (len == path__sz)
		return -EINVAL;
	if (path[0] != '/')
		return -EINVAL;
	if (len >= PATH_MAX)
		return -ENAMETOOLONG;

	interp = kmemdup_nul(path, len, GFP_KERNEL);
	if (!interp)
		return -ENOMEM;

	bm_bpf_stage_selection(bprm, interp, NULL);
	return 0;
}

/**
 * bpf_binprm_select_interp - run this exec under an interpreter the entry bound
 * @bprm: binary that is being executed
 * @name: name the interpreter was registered under
 * @name__sz: size of the @name buffer, including the terminating NUL
 *
 * To be called from the load program of a struct binfmt_misc_ops handler
 * instead of bpf_binprm_set_interp(). It selects one of the interpreters
 * the matched entry was registered with, each of which was opened once when
 * the entry was registered. Nothing is resolved at exec time, so no
 * filesystem view can redirect the interpreter.
 *
 * The interpreter runs under the path the entry registered it under.
 * Calling it again replaces the selection.
 *
 * Return: 0 on success, -ENOENT if the matched entry bound no interpreter
 * of that name, a negative errno on failure
 */
__bpf_kfunc int bpf_binprm_select_interp(struct linux_binprm *bprm,
					 const char *name, size_t name__sz)
{
	const struct binfmt_misc_interp *interp;
	size_t len;
	char *path;

	if (!name__sz)
		return -EINVAL;
	len = strnlen(name, name__sz);
	if (len == name__sz || !len)
		return -EINVAL;

	interp = binfmt_misc_find_interp(bprm->bpf_interps, name);
	if (!interp)
		return -ENOENT;

	path = kstrdup(interp->path, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	bm_bpf_stage_selection(bprm, path, get_file(interp->file));
	return 0;
}

/**
 * bpf_binprm_set_interp_arg - set a single argument for the interpreter
 * @bprm: binary that is being executed
 * @arg: argument to pass to the interpreter
 * @arg__sz: size of the @arg buffer, including the terminating NUL
 *
 * To be called from the load program of a struct binfmt_misc_ops handler. The
 * argument is passed to the interpreter ahead of the binary, mirroring the
 * single optional argument of a #! interpreter line. Calling it again
 * replaces the argument.
 *
 * Return: 0 on success, a negative errno on failure
 */
__bpf_kfunc int bpf_binprm_set_interp_arg(struct linux_binprm *bprm,
					  const char *arg, size_t arg__sz)
{
	size_t len;
	char *val;

	if (!arg__sz)
		return -EINVAL;
	len = strnlen(arg, arg__sz);
	if (len == arg__sz)
		return -EINVAL;
	if (!len)
		return -EINVAL;

	val = kmemdup_nul(arg, len, GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	kfree(bprm->bpf_interp_arg);
	bprm->bpf_interp_arg = val;
	return 0;
}

/**
 * bpf_binprm_set_flags - choose the interpreter invocation flags for this exec
 * @bprm: binary that is being executed
 * @flags: an OR of enum bpf_binprm_flags values
 *
 * To be called from the load program of a struct binfmt_misc_ops handler. It
 * decides per exec what a static entry fixes at registration with the P, C,
 * O, T and L flags: BPF_BINPRM_PRESERVE_ARGV0 keeps the caller's argv[0],
 * BPF_BINPRM_CREDENTIALS computes credentials from the binary, and
 * BPF_BINPRM_EXECFD hands the binary to the interpreter through AT_EXECFD.
 * BPF_BINPRM_TRANSPARENT additionally leaves the argument vector untouched,
 * making the exec look like a direct execution of the binary.
 * BPF_BINPRM_LOADER substitutes the interpreter for the binary's PT_INTERP
 * and runs the binary as a native exec; it excludes every other flag.
 * Calling it again replaces the flags, passing zero clears them again.
 *
 * Return: 0 on success, -EINVAL if @flags contains an unknown bit or an
 * invalid combination
 */
__bpf_kfunc int bpf_binprm_set_flags(struct linux_binprm *bprm,
				     enum bpf_binprm_flags flags)
{
	if (flags & ~(BPF_BINPRM_PRESERVE_ARGV0 | BPF_BINPRM_CREDENTIALS |
		      BPF_BINPRM_EXECFD | BPF_BINPRM_TRANSPARENT |
		      BPF_BINPRM_LOADER))
		return -EINVAL;

	/* Loader substitution is a native exec: no splice, execfd or creds work. */
	if ((flags & BPF_BINPRM_LOADER) && (flags & ~BPF_BINPRM_LOADER))
		return -EINVAL;

	/* Transparency preserves the whole argv, argv[0] included. */
	if ((flags & BPF_BINPRM_TRANSPARENT) && (flags & BPF_BINPRM_PRESERVE_ARGV0))
		return -EINVAL;

	bprm->bpf_flags = flags;
	return 0;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(bm_bpf_kfunc_ids)
BTF_ID_FLAGS(func, bpf_binprm_set_interp, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_binprm_select_interp, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_binprm_set_interp_arg, KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_binprm_set_flags, KF_SLEEPABLE)
BTF_KFUNCS_END(bm_bpf_kfunc_ids)

static int bm_bpf_kfunc_filter(const struct bpf_prog *prog, u32 kfunc_id)
{
	if (!btf_id_set8_contains(&bm_bpf_kfunc_ids, kfunc_id))
		return 0;
	if (prog->type != BPF_PROG_TYPE_STRUCT_OPS)
		return -EACCES;
	/* ->st_ops is unset during the cfg pass; enforced once it is set. */
	if (!prog->aux->st_ops)
		return 0;
	/* Only the load program decides how a binary is run. */
	if (bpf_prog_is_binfmt_misc_ops(prog) &&
	    prog->aux->attach_st_ops_member_off == offsetof(struct binfmt_misc_ops, load))
		return 0;
	return -EACCES;
}

static const struct btf_kfunc_id_set bm_bpf_kfunc_set = {
	.owner	= THIS_MODULE,
	.set	= &bm_bpf_kfunc_ids,
	.filter	= bm_bpf_kfunc_filter,
};

static bool bm_bpf_ops__match(struct linux_binprm *bprm)
{
	return false;
}

static int bm_bpf_ops__load(struct linux_binprm *bprm)
{
	return 0;
}

static struct binfmt_misc_ops bm_bpf_ops_stubs = {
	.match = bm_bpf_ops__match,
	.load = bm_bpf_ops__load,
};

static int bm_bpf_init(struct btf *btf)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					 &bm_bpf_kfunc_set);
}

static int bm_bpf_check_member(const struct btf_type *t,
			       const struct btf_member *member,
			       const struct bpf_prog *prog)
{
	u32 moff = __btf_member_bit_offset(t, member) / 8;

	switch (moff) {
	case offsetof(struct binfmt_misc_ops, match):
	case offsetof(struct binfmt_misc_ops, load):
		/* Reliable file reads at exec time require sleeping. */
		if (!prog->sleepable)
			return -EINVAL;
		break;
	}
	return 0;
}

static int bm_bpf_init_member(const struct btf_type *t,
			      const struct btf_member *member,
			      void *kdata, const void *udata)
{
	const struct binfmt_misc_ops *uops = udata;
	struct binfmt_misc_ops *ops = kdata;
	u32 moff = __btf_member_bit_offset(t, member) / 8;

	switch (moff) {
	case offsetof(struct binfmt_misc_ops, name):
		if (bpf_obj_name_cpy(ops->name, uops->name,
				     sizeof(ops->name)) <= 0)
			return -EINVAL;
		return 1;
	}
	return 0;
}

static int bm_bpf_validate(void *kdata)
{
	struct binfmt_misc_ops *ops = kdata;

	if (!ops->match || !ops->load)
		return -EINVAL;
	return 0;
}

static int bm_bpf_reg(void *kdata, struct bpf_link *link)
{
	struct binfmt_misc_ops *ops = kdata;
	struct bm_bpf_ops_reg *reg;

	reg = kzalloc_obj(*reg, GFP_KERNEL_ACCOUNT);
	if (!reg)
		return -ENOMEM;

	reg->ops = ops;
	reg->link = link;
	reg->user_ns = get_user_ns(current_user_ns());

	guard(spinlock)(&bm_bpf_ops_lock);

	if (bm_bpf_ops_find(reg->user_ns, ops->name)) {
		put_user_ns(reg->user_ns);
		kfree(reg);
		return -EEXIST;
	}

	list_add(&reg->list, &bm_bpf_ops_list);
	return 0;
}

static void bm_bpf_unreg(void *kdata, struct bpf_link *link)
{
	struct bm_bpf_ops_reg *reg;

	guard(spinlock)(&bm_bpf_ops_lock);

	list_for_each_entry(reg, &bm_bpf_ops_list, list) {
		if (reg->ops == kdata && reg->link == link) {
			list_del(&reg->list);
			put_user_ns(reg->user_ns);
			kfree(reg);
			return;
		}
	}
}

static const struct bpf_verifier_ops bm_bpf_verifier_ops = {
	.get_func_proto		= bpf_base_func_proto,
	.is_valid_access	= bpf_tracing_btf_ctx_access,
};

static struct bpf_struct_ops bpf_binfmt_misc_ops = {
	.verifier_ops	= &bm_bpf_verifier_ops,
	.init		= bm_bpf_init,
	.check_member	= bm_bpf_check_member,
	.init_member	= bm_bpf_init_member,
	.validate	= bm_bpf_validate,
	.reg		= bm_bpf_reg,
	.unreg		= bm_bpf_unreg,
	.cfi_stubs	= &bm_bpf_ops_stubs,
	.name		= "binfmt_misc_ops",
	.owner		= THIS_MODULE,
};

static int __init bm_bpf_struct_ops_init(void)
{
	return register_bpf_struct_ops(&bpf_binfmt_misc_ops, binfmt_misc_ops);
}
late_initcall(bm_bpf_struct_ops_init);
