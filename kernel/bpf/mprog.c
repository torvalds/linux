// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */

#include <linux/bpf.h>
#include <linux/bpf_mprog.h>

static int bpf_mprog_link(struct bpf_tuple *tuple,
			  u32 id_or_fd, u32 flags,
			  enum bpf_prog_type type)
{
	struct bpf_link *link = ERR_PTR(-EINVAL);
	bool id = flags & BPF_F_ID;

	if (id)
		link = bpf_link_by_id(id_or_fd);
	else if (id_or_fd)
		link = bpf_link_get_from_fd(id_or_fd);
	if (IS_ERR(link))
		return PTR_ERR(link);
	if (type && link->prog->type != type) {
		bpf_link_put(link);
		return -EINVAL;
	}

	tuple->link = link;
	tuple->prog = link->prog;
	return 0;
}

static int bpf_mprog_prog(struct bpf_tuple *tuple,
			  u32 id_or_fd, u32 flags,
			  enum bpf_prog_type type)
{
	struct bpf_prog *prog = ERR_PTR(-EINVAL);
	bool id = flags & BPF_F_ID;

	if (id)
		prog = bpf_prog_by_id(id_or_fd);
	else if (id_or_fd)
		prog = bpf_prog_get(id_or_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);
	if (type && prog->type != type) {
		bpf_prog_put(prog);
		return -EINVAL;
	}

	tuple->link = NULL;
	tuple->prog = prog;
	return 0;
}

static int bpf_mprog_tuple_relative(struct bpf_tuple *tuple,
				    u32 id_or_fd, u32 flags,
				    enum bpf_prog_type type)
{
	bool link = flags & BPF_F_LINK;
	bool id = flags & BPF_F_ID;

	memset(tuple, 0, sizeof(*tuple));
	if (link)
		return bpf_mprog_link(tuple, id_or_fd, flags, type);
	/* If no relevant flag is set and no id_or_fd was passed, then
	 * tuple link/prog is just NULLed. This is the case when before/
	 * after selects first/last position without passing fd.
	 */
	if (!id && !id_or_fd)
		return 0;
	return bpf_mprog_prog(tuple, id_or_fd, flags, type);
}

static void bpf_mprog_tuple_put(struct bpf_tuple *tuple)
{
	if (tuple->link)
		bpf_link_put(tuple->link);
	else if (tuple->prog)
		bpf_prog_put(tuple->prog);
}

/* The bpf_mprog_{replace,delete}() operate on exact idx position with the
 * one exception that for deletion we support delete from front/back. In
 * case of front idx is -1, in case of back idx is bpf_mprog_total(entry).
 * Adjustment to first and last entry is trivial. The bpf_mprog_insert()
 * we have to deal with the following cases:
 *
 * idx + before:
 *
 * Insert P4 before P3: idx for old array is 1, idx for new array is 2,
 * hence we adjust target idx for the new array, so that memmove copies
 * P1 and P2 to the new entry, and we insert P4 into idx 2. Inserting
 * before P1 would have old idx -1 and new idx 0.
 *
 * +--+--+--+     +--+--+--+--+     +--+--+--+--+
 * |P1|P2|P3| ==> |P1|P2|  |P3| ==> |P1|P2|P4|P3|
 * +--+--+--+     +--+--+--+--+     +--+--+--+--+
 *
 * idx + after:
 *
 * Insert P4 after P2: idx for old array is 2, idx for new array is 2.
 * Again, memmove copies P1 and P2 to the new entry, and we insert P4
 * into idx 2. Inserting after P3 would have both old/new idx at 4 aka
 * bpf_mprog_total(entry).
 *
 * +--+--+--+     +--+--+--+--+     +--+--+--+--+
 * |P1|P2|P3| ==> |P1|P2|  |P3| ==> |P1|P2|P4|P3|
 * +--+--+--+     +--+--+--+--+     +--+--+--+--+
 */
static int bpf_mprog_replace(struct bpf_mprog_entry *entry,
			     struct bpf_mprog_entry **entry_new,
			     struct bpf_tuple *ntuple, int idx)
{
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;
	struct bpf_prog *oprog;

	bpf_mprog_read(entry, idx, &fp, &cp);
	oprog = READ_ONCE(fp->prog);
	bpf_mprog_write(fp, cp, ntuple);
	if (!ntuple->link) {
		WARN_ON_ONCE(cp->link);
		bpf_prog_put(oprog);
	}
	*entry_new = entry;
	return 0;
}

static int bpf_mprog_insert(struct bpf_mprog_entry *entry,
			    struct bpf_mprog_entry **entry_new,
			    struct bpf_tuple *ntuple, int idx, u32 flags)
{
	int total = bpf_mprog_total(entry);
	struct bpf_mprog_entry *peer;
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;

	peer = bpf_mprog_peer(entry);
	bpf_mprog_entry_copy(peer, entry);
	if (idx == total)
		goto insert;
	else if (flags & BPF_F_BEFORE)
		idx += 1;
	bpf_mprog_entry_grow(peer, idx);
insert:
	bpf_mprog_read(peer, idx, &fp, &cp);
	bpf_mprog_write(fp, cp, ntuple);
	bpf_mprog_inc(peer);
	*entry_new = peer;
	return 0;
}

static int bpf_mprog_delete(struct bpf_mprog_entry *entry,
			    struct bpf_mprog_entry **entry_new,
			    struct bpf_tuple *dtuple, int idx)
{
	int total = bpf_mprog_total(entry);
	struct bpf_mprog_entry *peer;

	peer = bpf_mprog_peer(entry);
	bpf_mprog_entry_copy(peer, entry);
	if (idx == -1)
		idx = 0;
	else if (idx == total)
		idx = total - 1;
	bpf_mprog_entry_shrink(peer, idx);
	bpf_mprog_dec(peer);
	bpf_mprog_mark_for_release(peer, dtuple);
	*entry_new = peer;
	return 0;
}

/* In bpf_mprog_pos_*() we evaluate the target position for the BPF
 * program/link that needs to be replaced, inserted or deleted for
 * each "rule" independently. If all rules agree on that position
 * or existing element, then enact replacement, addition or deletion.
 * If this is not the case, then the request cannot be satisfied and
 * we bail out with an error.
 */
static int bpf_mprog_pos_exact(struct bpf_mprog_entry *entry,
			       struct bpf_tuple *tuple)
{
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;
	int i;

	for (i = 0; i < bpf_mprog_total(entry); i++) {
		bpf_mprog_read(entry, i, &fp, &cp);
		if (tuple->prog == READ_ONCE(fp->prog))
			return tuple->link == cp->link ? i : -EBUSY;
	}
	return -ENOENT;
}

static int bpf_mprog_pos_before(struct bpf_mprog_entry *entry,
				struct bpf_tuple *tuple)
{
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;
	int i;

	for (i = 0; i < bpf_mprog_total(entry); i++) {
		bpf_mprog_read(entry, i, &fp, &cp);
		if (tuple->prog == READ_ONCE(fp->prog) &&
		    (!tuple->link || tuple->link == cp->link))
			return i - 1;
	}
	return tuple->prog ? -ENOENT : -1;
}

static int bpf_mprog_pos_after(struct bpf_mprog_entry *entry,
			       struct bpf_tuple *tuple)
{
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;
	int i;

	for (i = 0; i < bpf_mprog_total(entry); i++) {
		bpf_mprog_read(entry, i, &fp, &cp);
		if (tuple->prog == READ_ONCE(fp->prog) &&
		    (!tuple->link || tuple->link == cp->link))
			return i + 1;
	}
	return tuple->prog ? -ENOENT : bpf_mprog_total(entry);
}

int bpf_mprog_attach(struct bpf_mprog_entry *entry,
		     struct bpf_mprog_entry **entry_new,
		     struct bpf_prog *prog_new, struct bpf_link *link,
		     struct bpf_prog *prog_old,
		     u32 flags, u32 id_or_fd, u64 revision)
{
	struct bpf_tuple rtuple, ntuple = {
		.prog = prog_new,
		.link = link,
	}, otuple = {
		.prog = prog_old,
		.link = link,
	};
	int ret, idx = -ERANGE, tidx;

	if (revision && revision != bpf_mprog_revision(entry))
		return -ESTALE;
	if (bpf_mprog_exists(entry, prog_new))
		return -EEXIST;
	ret = bpf_mprog_tuple_relative(&rtuple, id_or_fd,
				       flags & ~BPF_F_REPLACE,
				       prog_new->type);
	if (ret)
		return ret;
	if (flags & BPF_F_REPLACE) {
		tidx = bpf_mprog_pos_exact(entry, &otuple);
		if (tidx < 0) {
			ret = tidx;
			goto out;
		}
		idx = tidx;
	}
	if (flags & BPF_F_BEFORE) {
		tidx = bpf_mprog_pos_before(entry, &rtuple);
		if (tidx < -1 || (idx >= -1 && tidx != idx)) {
			ret = tidx < -1 ? tidx : -ERANGE;
			goto out;
		}
		idx = tidx;
	}
	if (flags & BPF_F_AFTER) {
		tidx = bpf_mprog_pos_after(entry, &rtuple);
		if (tidx < -1 || (idx >= -1 && tidx != idx)) {
			ret = tidx < 0 ? tidx : -ERANGE;
			goto out;
		}
		idx = tidx;
	}
	if (idx < -1) {
		if (rtuple.prog || flags) {
			ret = -EINVAL;
			goto out;
		}
		idx = bpf_mprog_total(entry);
		flags = BPF_F_AFTER;
	}
	if (idx >= bpf_mprog_max()) {
		ret = -ERANGE;
		goto out;
	}
	if (flags & BPF_F_REPLACE)
		ret = bpf_mprog_replace(entry, entry_new, &ntuple, idx);
	else
		ret = bpf_mprog_insert(entry, entry_new, &ntuple, idx, flags);
out:
	bpf_mprog_tuple_put(&rtuple);
	return ret;
}

static int bpf_mprog_fetch(struct bpf_mprog_entry *entry,
			   struct bpf_tuple *tuple, int idx)
{
	int total = bpf_mprog_total(entry);
	struct bpf_mprog_cp *cp;
	struct bpf_mprog_fp *fp;
	struct bpf_prog *prog;
	struct bpf_link *link;

	if (idx == -1)
		idx = 0;
	else if (idx == total)
		idx = total - 1;
	bpf_mprog_read(entry, idx, &fp, &cp);
	prog = READ_ONCE(fp->prog);
	link = cp->link;
	/* The deletion request can either be without filled tuple in which
	 * case it gets populated here based on idx, or with filled tuple
	 * where the only thing we end up doing is the WARN_ON_ONCE() assert.
	 * If we hit a BPF link at the given index, it must not be removed
	 * from opts path.
	 */
	if (link && !tuple->link)
		return -EBUSY;
	WARN_ON_ONCE(tuple->prog && tuple->prog != prog);
	WARN_ON_ONCE(tuple->link && tuple->link != link);
	tuple->prog = prog;
	tuple->link = link;
	return 0;
}

int bpf_mprog_detach(struct bpf_mprog_entry *entry,
		     struct bpf_mprog_entry **entry_new,
		     struct bpf_prog *prog, struct bpf_link *link,
		     u32 flags, u32 id_or_fd, u64 revision)
{
	struct bpf_tuple rtuple, dtuple = {
		.prog = prog,
		.link = link,
	};
	int ret, idx = -ERANGE, tidx;

	if (flags & BPF_F_REPLACE)
		return -EINVAL;
	if (revision && revision != bpf_mprog_revision(entry))
		return -ESTALE;
	ret = bpf_mprog_tuple_relative(&rtuple, id_or_fd, flags,
				       prog ? prog->type :
				       BPF_PROG_TYPE_UNSPEC);
	if (ret)
		return ret;
	if (dtuple.prog) {
		tidx = bpf_mprog_pos_exact(entry, &dtuple);
		if (tidx < 0) {
			ret = tidx;
			goto out;
		}
		idx = tidx;
	}
	if (flags & BPF_F_BEFORE) {
		tidx = bpf_mprog_pos_before(entry, &rtuple);
		if (tidx < -1 || (idx >= -1 && tidx != idx)) {
			ret = tidx < -1 ? tidx : -ERANGE;
			goto out;
		}
		idx = tidx;
	}
	if (flags & BPF_F_AFTER) {
		tidx = bpf_mprog_pos_after(entry, &rtuple);
		if (tidx < -1 || (idx >= -1 && tidx != idx)) {
			ret = tidx < 0 ? tidx : -ERANGE;
			goto out;
		}
		idx = tidx;
	}
	if (idx < -1) {
		if (rtuple.prog || flags) {
			ret = -EINVAL;
			goto out;
		}
		idx = bpf_mprog_total(entry);
		flags = BPF_F_AFTER;
	}
	if (idx >= bpf_mprog_max()) {
		ret = -ERANGE;
		goto out;
	}
	ret = bpf_mprog_fetch(entry, &dtuple, idx);
	if (ret)
		goto out;
	ret = bpf_mprog_delete(entry, entry_new, &dtuple, idx);
out:
	bpf_mprog_tuple_put(&rtuple);
	return ret;
}

int bpf_mprog_query(const union bpf_attr *attr, union bpf_attr __user *uattr,
		    struct bpf_mprog_entry *entry)
{
	u32 __user *uprog_flags, *ulink_flags;
	u32 __user *uprog_id, *ulink_id;
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;
	struct bpf_prog *prog;
	const u32 flags = 0;
	int i, ret = 0;
	u32 id, count;
	u64 revision;

	if (attr->query.query_flags || attr->query.attach_flags)
		return -EINVAL;
	revision = bpf_mprog_revision(entry);
	count = bpf_mprog_total(entry);
	if (copy_to_user(&uattr->query.attach_flags, &flags, sizeof(flags)))
		return -EFAULT;
	if (copy_to_user(&uattr->query.revision, &revision, sizeof(revision)))
		return -EFAULT;
	if (copy_to_user(&uattr->query.count, &count, sizeof(count)))
		return -EFAULT;
	uprog_id = u64_to_user_ptr(attr->query.prog_ids);
	uprog_flags = u64_to_user_ptr(attr->query.prog_attach_flags);
	ulink_id = u64_to_user_ptr(attr->query.link_ids);
	ulink_flags = u64_to_user_ptr(attr->query.link_attach_flags);
	if (attr->query.count == 0 || !uprog_id || !count)
		return 0;
	if (attr->query.count < count) {
		count = attr->query.count;
		ret = -ENOSPC;
	}
	for (i = 0; i < bpf_mprog_max(); i++) {
		bpf_mprog_read(entry, i, &fp, &cp);
		prog = READ_ONCE(fp->prog);
		if (!prog)
			break;
		id = prog->aux->id;
		if (copy_to_user(uprog_id + i, &id, sizeof(id)))
			return -EFAULT;
		if (uprog_flags &&
		    copy_to_user(uprog_flags + i, &flags, sizeof(flags)))
			return -EFAULT;
		id = cp->link ? cp->link->id : 0;
		if (ulink_id &&
		    copy_to_user(ulink_id + i, &id, sizeof(id)))
			return -EFAULT;
		if (ulink_flags &&
		    copy_to_user(ulink_flags + i, &flags, sizeof(flags)))
			return -EFAULT;
		if (i + 1 == count)
			break;
	}
	return ret;
}
