// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Red hat */
#include "hid_bpf_helpers.h"

char _license[] SEC("license") = "GPL";

struct attach_prog_args {
	int prog_fd;
	unsigned int hid;
	int retval;
	int insert_head;
};

__u64 callback_check = 52;
__u64 callback2_check = 52;

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_first_event, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 3 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	callback_check = rw_data[1];

	rw_data[2] = rw_data[1] + 5;

	return hid_ctx->size;
}

SEC(".struct_ops.link")
struct hid_bpf_ops first_event = {
	.hid_device_event = (void *)hid_first_event,
	.hid_id = 2,
};

int __hid_subprog_first_event(struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 3 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	rw_data[2] = rw_data[1] + 5;

	return hid_ctx->size;
}

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_subprog_first_event, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	return __hid_subprog_first_event(hid_ctx, type);
}

SEC(".struct_ops.link")
struct hid_bpf_ops subprog_first_event = {
	.hid_device_event = (void *)hid_subprog_first_event,
	.hid_id = 2,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_second_event, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 4 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	rw_data[3] = rw_data[2] + 5;

	return hid_ctx->size;
}

SEC(".struct_ops.link")
struct hid_bpf_ops second_event = {
	.hid_device_event = (void *)hid_second_event,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_change_report_id, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 3 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	rw_data[0] = 2;

	return 9;
}

SEC(".struct_ops.link")
struct hid_bpf_ops change_report_id = {
	.hid_device_event = (void *)hid_change_report_id,
};

struct hid_hw_request_syscall_args {
	/* data needs to come at offset 0 so we can use it in calls */
	__u8 data[10];
	unsigned int hid;
	int retval;
	size_t size;
	enum hid_report_type type;
	__u8 request_type;
};

SEC("syscall")
int hid_user_raw_request(struct hid_hw_request_syscall_args *args)
{
	struct hid_bpf_ctx *ctx;
	const size_t size = args->size;
	int i, ret = 0;

	if (size > sizeof(args->data))
		return -7; /* -E2BIG */

	ctx = hid_bpf_allocate_context(args->hid);
	if (!ctx)
		return -1; /* EPERM check */

	ret = hid_bpf_hw_request(ctx,
				 args->data,
				 size,
				 args->type,
				 args->request_type);
	args->retval = ret;

	hid_bpf_release_context(ctx);

	return 0;
}

SEC("syscall")
int hid_user_output_report(struct hid_hw_request_syscall_args *args)
{
	struct hid_bpf_ctx *ctx;
	const size_t size = args->size;
	int i, ret = 0;

	if (size > sizeof(args->data))
		return -7; /* -E2BIG */

	ctx = hid_bpf_allocate_context(args->hid);
	if (!ctx)
		return -1; /* EPERM check */

	ret = hid_bpf_hw_output_report(ctx,
				       args->data,
				       size);
	args->retval = ret;

	hid_bpf_release_context(ctx);

	return 0;
}

SEC("syscall")
int hid_user_input_report(struct hid_hw_request_syscall_args *args)
{
	struct hid_bpf_ctx *ctx;
	const size_t size = args->size;
	int i, ret = 0;

	if (size > sizeof(args->data))
		return -7; /* -E2BIG */

	ctx = hid_bpf_allocate_context(args->hid);
	if (!ctx)
		return -1; /* EPERM check */

	ret = hid_bpf_input_report(ctx, HID_INPUT_REPORT, args->data, size);
	args->retval = ret;

	hid_bpf_release_context(ctx);

	return 0;
}

static const __u8 rdesc[] = {
	0x05, 0x01,				/* USAGE_PAGE (Generic Desktop) */
	0x09, 0x32,				/* USAGE (Z) */
	0x95, 0x01,				/* REPORT_COUNT (1) */
	0x81, 0x06,				/* INPUT (Data,Var,Rel) */

	0x06, 0x00, 0xff,			/* Usage Page (Vendor Defined Page 1) */
	0x19, 0x01,				/* USAGE_MINIMUM (1) */
	0x29, 0x03,				/* USAGE_MAXIMUM (3) */
	0x15, 0x00,				/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,				/* LOGICAL_MAXIMUM (1) */
	0x95, 0x03,				/* REPORT_COUNT (3) */
	0x75, 0x01,				/* REPORT_SIZE (1) */
	0x91, 0x02,				/* Output (Data,Var,Abs) */
	0x95, 0x01,				/* REPORT_COUNT (1) */
	0x75, 0x05,				/* REPORT_SIZE (5) */
	0x91, 0x01,				/* Output (Cnst,Var,Abs) */

	0x06, 0x00, 0xff,			/* Usage Page (Vendor Defined Page 1) */
	0x19, 0x06,				/* USAGE_MINIMUM (6) */
	0x29, 0x08,				/* USAGE_MAXIMUM (8) */
	0x15, 0x00,				/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,				/* LOGICAL_MAXIMUM (1) */
	0x95, 0x03,				/* REPORT_COUNT (3) */
	0x75, 0x01,				/* REPORT_SIZE (1) */
	0xb1, 0x02,				/* Feature (Data,Var,Abs) */
	0x95, 0x01,				/* REPORT_COUNT (1) */
	0x75, 0x05,				/* REPORT_SIZE (5) */
	0x91, 0x01,				/* Output (Cnst,Var,Abs) */

	0xc0,				/* END_COLLECTION */
	0xc0,			/* END_COLLECTION */
};

/*
 * the following program is marked as sleepable (struct_ops.s).
 * This is not strictly mandatory but is a nice test for
 * sleepable struct_ops
 */
SEC("?struct_ops.s/hid_rdesc_fixup")
int BPF_PROG(hid_rdesc_fixup, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 4096 /* size */);

	if (!data)
		return 0; /* EPERM check */

	callback2_check = data[4];

	/* insert rdesc at offset 73 */
	__builtin_memcpy(&data[73], rdesc, sizeof(rdesc));

	/* Change Usage Vendor globally */
	data[4] = 0x42;

	return sizeof(rdesc) + 73;
}

SEC(".struct_ops.link")
struct hid_bpf_ops rdesc_fixup = {
	.hid_rdesc_fixup = (void *)hid_rdesc_fixup,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_test_insert1, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 4 /* size */);

	if (!data)
		return 0; /* EPERM check */

	/* we need to be run first */
	if (data[2] || data[3])
		return -1;

	data[1] = 1;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_insert1 = {
	.hid_device_event = (void *)hid_test_insert1,
	.flags = BPF_F_BEFORE,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_test_insert2, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 4 /* size */);

	if (!data)
		return 0; /* EPERM check */

	/* after insert0 and before insert2 */
	if (!data[1] || data[3])
		return -1;

	data[2] = 2;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_insert2 = {
	.hid_device_event = (void *)hid_test_insert2,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_test_insert3, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 4 /* size */);

	if (!data)
		return 0; /* EPERM check */

	/* at the end */
	if (!data[1] || !data[2])
		return -1;

	data[3] = 3;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_insert3 = {
	.hid_device_event = (void *)hid_test_insert3,
};

SEC("?struct_ops/hid_hw_request")
int BPF_PROG(hid_test_filter_raw_request, struct hid_bpf_ctx *hctx, unsigned char reportnum,
	     enum hid_report_type rtype, enum hid_class_request reqtype, __u64 source)
{
	return -20;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_filter_raw_request = {
	.hid_hw_request = (void *)hid_test_filter_raw_request,
};

static struct file *current_file;

SEC("fentry/hidraw_open")
int BPF_PROG(hidraw_open, struct inode *inode, struct file *file)
{
	current_file = file;
	return 0;
}

SEC("?struct_ops.s/hid_hw_request")
int BPF_PROG(hid_test_hidraw_raw_request, struct hid_bpf_ctx *hctx, unsigned char reportnum,
	     enum hid_report_type rtype, enum hid_class_request reqtype, __u64 source)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 3 /* size */);
	int ret;

	if (!data)
		return 0; /* EPERM check */

	/* check if the incoming request comes from our hidraw operation */
	if (source == (__u64)current_file) {
		data[0] = reportnum;

		ret = hid_bpf_hw_request(hctx, data, 2, rtype, reqtype);
		if (ret != 2)
			return -1;
		data[0] = reportnum + 1;
		data[1] = reportnum + 2;
		data[2] = reportnum + 3;
		return 3;
	}

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_hidraw_raw_request = {
	.hid_hw_request = (void *)hid_test_hidraw_raw_request,
};

SEC("?struct_ops.s/hid_hw_request")
int BPF_PROG(hid_test_infinite_loop_raw_request, struct hid_bpf_ctx *hctx, unsigned char reportnum,
	     enum hid_report_type rtype, enum hid_class_request reqtype, __u64 source)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 3 /* size */);
	int ret;

	if (!data)
		return 0; /* EPERM check */

	/* always forward the request as-is to the device, hid-bpf should prevent
	 * infinite loops.
	 */
	data[0] = reportnum;

	ret = hid_bpf_hw_request(hctx, data, 2, rtype, reqtype);
	if (ret == 2)
		return 3;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_infinite_loop_raw_request = {
	.hid_hw_request = (void *)hid_test_infinite_loop_raw_request,
};
