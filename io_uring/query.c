// SPDX-License-Identifier: GPL-2.0

#include "linux/io_uring/query.h"

#include "query.h"
#include "io_uring.h"

#define IO_MAX_QUERY_SIZE		(sizeof(struct io_uring_query_opcode))
#define IO_MAX_QUERY_ENTRIES		1000

static ssize_t io_query_ops(void *data)
{
	struct io_uring_query_opcode *e = data;

	BUILD_BUG_ON(sizeof(*e) > IO_MAX_QUERY_SIZE);

	e->nr_request_opcodes = IORING_OP_LAST;
	e->nr_register_opcodes = IORING_REGISTER_LAST;
	e->feature_flags = IORING_FEAT_FLAGS;
	e->ring_setup_flags = IORING_SETUP_FLAGS;
	e->enter_flags = IORING_ENTER_FLAGS;
	e->sqe_flags = SQE_VALID_FLAGS;
	e->nr_query_opcodes = __IO_URING_QUERY_MAX;
	e->__pad = 0;
	return sizeof(*e);
}

static int io_handle_query_entry(struct io_ring_ctx *ctx,
				 void *data, void __user *uhdr,
				 u64 *next_entry)
{
	struct io_uring_query_hdr hdr;
	size_t usize, res_size = 0;
	ssize_t ret = -EINVAL;
	void __user *udata;

	if (copy_from_user(&hdr, uhdr, sizeof(hdr)))
		return -EFAULT;
	usize = hdr.size;
	hdr.size = min(hdr.size, IO_MAX_QUERY_SIZE);
	udata = u64_to_user_ptr(hdr.query_data);

	if (hdr.query_op >= __IO_URING_QUERY_MAX) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (!mem_is_zero(hdr.__resv, sizeof(hdr.__resv)) || hdr.result || !hdr.size)
		goto out;
	if (copy_from_user(data, udata, hdr.size))
		return -EFAULT;

	switch (hdr.query_op) {
	case IO_URING_QUERY_OPCODES:
		ret = io_query_ops(data);
		break;
	}

	if (ret >= 0) {
		if (WARN_ON_ONCE(ret > IO_MAX_QUERY_SIZE))
			return -EFAULT;
		res_size = ret;
		ret = 0;
	}
out:
	hdr.result = ret;
	hdr.size = min_t(size_t, usize, res_size);

	if (copy_struct_to_user(udata, usize, data, hdr.size, NULL))
		return -EFAULT;
	if (copy_to_user(uhdr, &hdr, sizeof(hdr)))
		return -EFAULT;
	*next_entry = hdr.next_entry;
	return 0;
}

int io_query(struct io_ring_ctx *ctx, void __user *arg, unsigned nr_args)
{
	char entry_buffer[IO_MAX_QUERY_SIZE];
	void __user *uhdr = arg;
	int ret, nr = 0;

	memset(entry_buffer, 0, sizeof(entry_buffer));

	if (nr_args)
		return -EINVAL;

	while (uhdr) {
		u64 next_hdr;

		ret = io_handle_query_entry(ctx, entry_buffer, uhdr, &next_hdr);
		if (ret)
			return ret;
		uhdr = u64_to_user_ptr(next_hdr);

		/* Have some limit to avoid a potential cycle */
		if (++nr >= IO_MAX_QUERY_ENTRIES)
			return -ERANGE;
		if (fatal_signal_pending(current))
			return -EINTR;
		cond_resched();
	}
	return 0;
}
