// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>
#include <linux/btf.h>

#define verbose(env, fmt, args...) bpf_verifier_log_write(env, fmt, ##args)

static int check_abnormal_return(struct bpf_verifier_env *env)
{
	int i;

	for (i = 1; i < env->subprog_cnt; i++) {
		if (env->subprog_info[i].has_ld_abs) {
			verbose(env, "LD_ABS is not allowed in subprogs without BTF\n");
			return -EINVAL;
		}
		if (env->subprog_info[i].has_tail_call) {
			verbose(env, "tail_call is not allowed in subprogs without BTF\n");
			return -EINVAL;
		}
	}
	return 0;
}

/* The minimum supported BTF func info size */
#define MIN_BPF_FUNCINFO_SIZE	8
#define MAX_FUNCINFO_REC_SIZE	252

static int check_btf_func_early(struct bpf_verifier_env *env,
				const union bpf_attr *attr,
				bpfptr_t uattr)
{
	u32 krec_size = sizeof(struct bpf_func_info);
	const struct btf_type *type, *func_proto;
	u32 i, nfuncs, urec_size, min_size;
	struct bpf_func_info *krecord;
	struct bpf_prog *prog;
	const struct btf *btf;
	u32 prev_offset = 0;
	bpfptr_t urecord;
	int ret = -ENOMEM;

	nfuncs = attr->func_info_cnt;
	if (!nfuncs) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}

	urec_size = attr->func_info_rec_size;
	if (urec_size < MIN_BPF_FUNCINFO_SIZE ||
	    urec_size > MAX_FUNCINFO_REC_SIZE ||
	    urec_size % sizeof(u32)) {
		verbose(env, "invalid func info rec size %u\n", urec_size);
		return -EINVAL;
	}

	prog = env->prog;
	btf = prog->aux->btf;

	urecord = make_bpfptr(attr->func_info, uattr.is_kernel);
	min_size = min_t(u32, krec_size, urec_size);

	krecord = kvcalloc(nfuncs, krec_size, GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (!krecord)
		return -ENOMEM;

	for (i = 0; i < nfuncs; i++) {
		ret = bpf_check_uarg_tail_zero(urecord, krec_size, urec_size);
		if (ret) {
			if (ret == -E2BIG) {
				verbose(env, "nonzero tailing record in func info");
				/* set the size kernel expects so loader can zero
				 * out the rest of the record.
				 */
				if (copy_to_bpfptr_offset(uattr,
							  offsetof(union bpf_attr, func_info_rec_size),
							  &min_size, sizeof(min_size)))
					ret = -EFAULT;
			}
			goto err_free;
		}

		if (copy_from_bpfptr(&krecord[i], urecord, min_size)) {
			ret = -EFAULT;
			goto err_free;
		}

		/* check insn_off */
		ret = -EINVAL;
		if (i == 0) {
			if (krecord[i].insn_off) {
				verbose(env,
					"nonzero insn_off %u for the first func info record",
					krecord[i].insn_off);
				goto err_free;
			}
		} else if (krecord[i].insn_off <= prev_offset) {
			verbose(env,
				"same or smaller insn offset (%u) than previous func info record (%u)",
				krecord[i].insn_off, prev_offset);
			goto err_free;
		}

		/* check type_id */
		type = btf_type_by_id(btf, krecord[i].type_id);
		if (!type || !btf_type_is_func(type)) {
			verbose(env, "invalid type id %d in func info",
				krecord[i].type_id);
			goto err_free;
		}

		func_proto = btf_type_by_id(btf, type->type);
		if (unlikely(!func_proto || !btf_type_is_func_proto(func_proto)))
			/* btf_func_check() already verified it during BTF load */
			goto err_free;

		prev_offset = krecord[i].insn_off;
		bpfptr_add(&urecord, urec_size);
	}

	prog->aux->func_info = krecord;
	prog->aux->func_info_cnt = nfuncs;
	return 0;

err_free:
	kvfree(krecord);
	return ret;
}

static int check_btf_func(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  bpfptr_t uattr)
{
	const struct btf_type *type, *func_proto, *ret_type;
	u32 i, nfuncs, urec_size;
	struct bpf_func_info *krecord;
	struct bpf_func_info_aux *info_aux = NULL;
	struct bpf_prog *prog;
	const struct btf *btf;
	bpfptr_t urecord;
	bool scalar_return;
	int ret = -ENOMEM;

	nfuncs = attr->func_info_cnt;
	if (!nfuncs) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}
	if (nfuncs != env->subprog_cnt) {
		verbose(env, "number of funcs in func_info doesn't match number of subprogs\n");
		return -EINVAL;
	}

	urec_size = attr->func_info_rec_size;

	prog = env->prog;
	btf = prog->aux->btf;

	urecord = make_bpfptr(attr->func_info, uattr.is_kernel);

	krecord = prog->aux->func_info;
	info_aux = kzalloc_objs(*info_aux, nfuncs,
				GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (!info_aux)
		return -ENOMEM;

	for (i = 0; i < nfuncs; i++) {
		/* check insn_off */
		ret = -EINVAL;

		if (env->subprog_info[i].start != krecord[i].insn_off) {
			verbose(env, "func_info BTF section doesn't match subprog layout in BPF program\n");
			goto err_free;
		}

		/* Already checked type_id */
		type = btf_type_by_id(btf, krecord[i].type_id);
		info_aux[i].linkage = BTF_INFO_VLEN(type->info);
		/* Already checked func_proto */
		func_proto = btf_type_by_id(btf, type->type);

		ret_type = btf_type_skip_modifiers(btf, func_proto->type, NULL);
		scalar_return =
			btf_type_is_small_int(ret_type) || btf_is_any_enum(ret_type);
		if (i && !scalar_return && env->subprog_info[i].has_ld_abs) {
			verbose(env, "LD_ABS is only allowed in functions that return 'int'.\n");
			goto err_free;
		}
		if (i && !scalar_return && env->subprog_info[i].has_tail_call) {
			verbose(env, "tail_call is only allowed in functions that return 'int'.\n");
			goto err_free;
		}

		env->subprog_info[i].name = btf_name_by_offset(btf, type->name_off);
		bpfptr_add(&urecord, urec_size);
	}

	prog->aux->func_info_aux = info_aux;
	return 0;

err_free:
	kfree(info_aux);
	return ret;
}

#define MIN_BPF_LINEINFO_SIZE	offsetofend(struct bpf_line_info, line_col)
#define MAX_LINEINFO_REC_SIZE	MAX_FUNCINFO_REC_SIZE

static int check_btf_line(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  bpfptr_t uattr)
{
	u32 i, s, nr_linfo, ncopy, expected_size, rec_size, prev_offset = 0;
	struct bpf_subprog_info *sub;
	struct bpf_line_info *linfo;
	struct bpf_prog *prog;
	const struct btf *btf;
	bpfptr_t ulinfo;
	int err;

	nr_linfo = attr->line_info_cnt;
	if (!nr_linfo)
		return 0;
	if (nr_linfo > INT_MAX / sizeof(struct bpf_line_info))
		return -EINVAL;

	rec_size = attr->line_info_rec_size;
	if (rec_size < MIN_BPF_LINEINFO_SIZE ||
	    rec_size > MAX_LINEINFO_REC_SIZE ||
	    rec_size & (sizeof(u32) - 1))
		return -EINVAL;

	/* Need to zero it in case the userspace may
	 * pass in a smaller bpf_line_info object.
	 */
	linfo = kvzalloc_objs(struct bpf_line_info, nr_linfo,
			      GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (!linfo)
		return -ENOMEM;

	prog = env->prog;
	btf = prog->aux->btf;

	s = 0;
	sub = env->subprog_info;
	ulinfo = make_bpfptr(attr->line_info, uattr.is_kernel);
	expected_size = sizeof(struct bpf_line_info);
	ncopy = min_t(u32, expected_size, rec_size);
	for (i = 0; i < nr_linfo; i++) {
		err = bpf_check_uarg_tail_zero(ulinfo, expected_size, rec_size);
		if (err) {
			if (err == -E2BIG) {
				verbose(env, "nonzero tailing record in line_info");
				if (copy_to_bpfptr_offset(uattr,
							  offsetof(union bpf_attr, line_info_rec_size),
							  &expected_size, sizeof(expected_size)))
					err = -EFAULT;
			}
			goto err_free;
		}

		if (copy_from_bpfptr(&linfo[i], ulinfo, ncopy)) {
			err = -EFAULT;
			goto err_free;
		}

		/*
		 * Check insn_off to ensure
		 * 1) strictly increasing AND
		 * 2) bounded by prog->len
		 *
		 * The linfo[0].insn_off == 0 check logically falls into
		 * the later "missing bpf_line_info for func..." case
		 * because the first linfo[0].insn_off must be the
		 * first sub also and the first sub must have
		 * subprog_info[0].start == 0.
		 */
		if ((i && linfo[i].insn_off <= prev_offset) ||
		    linfo[i].insn_off >= prog->len) {
			verbose(env, "Invalid line_info[%u].insn_off:%u (prev_offset:%u prog->len:%u)\n",
				i, linfo[i].insn_off, prev_offset,
				prog->len);
			err = -EINVAL;
			goto err_free;
		}

		if (!prog->insnsi[linfo[i].insn_off].code) {
			verbose(env,
				"Invalid insn code at line_info[%u].insn_off\n",
				i);
			err = -EINVAL;
			goto err_free;
		}

		if (!btf_name_by_offset(btf, linfo[i].line_off) ||
		    !btf_name_by_offset(btf, linfo[i].file_name_off)) {
			verbose(env, "Invalid line_info[%u].line_off or .file_name_off\n", i);
			err = -EINVAL;
			goto err_free;
		}

		if (s != env->subprog_cnt) {
			if (linfo[i].insn_off == sub[s].start) {
				sub[s].linfo_idx = i;
				s++;
			} else if (sub[s].start < linfo[i].insn_off) {
				verbose(env, "missing bpf_line_info for func#%u\n", s);
				err = -EINVAL;
				goto err_free;
			}
		}

		prev_offset = linfo[i].insn_off;
		bpfptr_add(&ulinfo, rec_size);
	}

	if (s != env->subprog_cnt) {
		verbose(env, "missing bpf_line_info for %u funcs starting from func#%u\n",
			env->subprog_cnt - s, s);
		err = -EINVAL;
		goto err_free;
	}

	prog->aux->linfo = linfo;
	prog->aux->nr_linfo = nr_linfo;

	return 0;

err_free:
	kvfree(linfo);
	return err;
}

#define MIN_CORE_RELO_SIZE	sizeof(struct bpf_core_relo)
#define MAX_CORE_RELO_SIZE	MAX_FUNCINFO_REC_SIZE

static int check_core_relo(struct bpf_verifier_env *env,
			   const union bpf_attr *attr,
			   bpfptr_t uattr)
{
	u32 i, nr_core_relo, ncopy, expected_size, rec_size;
	struct bpf_core_relo core_relo = {};
	struct bpf_prog *prog = env->prog;
	const struct btf *btf = prog->aux->btf;
	struct bpf_core_ctx ctx = {
		.log = &env->log,
		.btf = btf,
	};
	bpfptr_t u_core_relo;
	int err;

	nr_core_relo = attr->core_relo_cnt;
	if (!nr_core_relo)
		return 0;
	if (nr_core_relo > INT_MAX / sizeof(struct bpf_core_relo))
		return -EINVAL;

	rec_size = attr->core_relo_rec_size;
	if (rec_size < MIN_CORE_RELO_SIZE ||
	    rec_size > MAX_CORE_RELO_SIZE ||
	    rec_size % sizeof(u32))
		return -EINVAL;

	u_core_relo = make_bpfptr(attr->core_relos, uattr.is_kernel);
	expected_size = sizeof(struct bpf_core_relo);
	ncopy = min_t(u32, expected_size, rec_size);

	/* Unlike func_info and line_info, copy and apply each CO-RE
	 * relocation record one at a time.
	 */
	for (i = 0; i < nr_core_relo; i++) {
		/* future proofing when sizeof(bpf_core_relo) changes */
		err = bpf_check_uarg_tail_zero(u_core_relo, expected_size, rec_size);
		if (err) {
			if (err == -E2BIG) {
				verbose(env, "nonzero tailing record in core_relo");
				if (copy_to_bpfptr_offset(uattr,
							  offsetof(union bpf_attr, core_relo_rec_size),
							  &expected_size, sizeof(expected_size)))
					err = -EFAULT;
			}
			break;
		}

		if (copy_from_bpfptr(&core_relo, u_core_relo, ncopy)) {
			err = -EFAULT;
			break;
		}

		if (core_relo.insn_off % 8 || core_relo.insn_off / 8 >= prog->len) {
			verbose(env, "Invalid core_relo[%u].insn_off:%u prog->len:%u\n",
				i, core_relo.insn_off, prog->len);
			err = -EINVAL;
			break;
		}

		err = bpf_core_apply(&ctx, &core_relo, i,
				     &prog->insnsi[core_relo.insn_off / 8]);
		if (err)
			break;
		bpfptr_add(&u_core_relo, rec_size);
	}
	return err;
}

int bpf_check_btf_info_early(struct bpf_verifier_env *env,
			     const union bpf_attr *attr,
			     bpfptr_t uattr)
{
	struct btf *btf;
	int err;

	if (!attr->func_info_cnt && !attr->line_info_cnt) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}

	btf = btf_get_by_fd(attr->prog_btf_fd);
	if (IS_ERR(btf))
		return PTR_ERR(btf);
	if (btf_is_kernel(btf)) {
		btf_put(btf);
		return -EACCES;
	}
	env->prog->aux->btf = btf;

	err = check_btf_func_early(env, attr, uattr);
	if (err)
		return err;
	return 0;
}

int bpf_check_btf_info(struct bpf_verifier_env *env,
		       const union bpf_attr *attr,
		       bpfptr_t uattr)
{
	int err;

	if (!attr->func_info_cnt && !attr->line_info_cnt) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}

	err = check_btf_func(env, attr, uattr);
	if (err)
		return err;

	err = check_btf_line(env, attr, uattr);
	if (err)
		return err;

	err = check_core_relo(env, attr, uattr);
	if (err)
		return err;

	return 0;
}
