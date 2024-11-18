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

SEC("?struct_ops/hid_hw_output_report")
int BPF_PROG(hid_test_filter_output_report, struct hid_bpf_ctx *hctx, unsigned char reportnum,
	     enum hid_report_type rtype, enum hid_class_request reqtype, __u64 source)
{
	return -25;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_filter_output_report = {
	.hid_hw_output_report = (void *)hid_test_filter_output_report,
};

SEC("?struct_ops.s/hid_hw_output_report")
int BPF_PROG(hid_test_hidraw_output_report, struct hid_bpf_ctx *hctx, __u64 source)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 3 /* size */);
	int ret;

	if (!data)
		return 0; /* EPERM check */

	/* check if the incoming request comes from our hidraw operation */
	if (source == (__u64)current_file)
		return hid_bpf_hw_output_report(hctx, data, 2);

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_hidraw_output_report = {
	.hid_hw_output_report = (void *)hid_test_hidraw_output_report,
};

SEC("?struct_ops.s/hid_hw_output_report")
int BPF_PROG(hid_test_infinite_loop_output_report, struct hid_bpf_ctx *hctx, __u64 source)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 3 /* size */);
	int ret;

	if (!data)
		return 0; /* EPERM check */

	/* always forward the request as-is to the device, hid-bpf should prevent
	 * infinite loops.
	 */

	ret = hid_bpf_hw_output_report(hctx, data, 2);
	if (ret == 2)
		return 2;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_infinite_loop_output_report = {
	.hid_hw_output_report = (void *)hid_test_infinite_loop_output_report,
};

struct elem {
	struct bpf_wq work;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} hmap SEC(".maps");

static int wq_cb_sleepable(void *map, int *key, void *work)
{
	__u8 buf[9] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
	struct hid_bpf_ctx *hid_ctx;

	hid_ctx = hid_bpf_allocate_context(*key);
	if (!hid_ctx)
		return 0; /* EPERM check */

	hid_bpf_input_report(hid_ctx, HID_INPUT_REPORT, buf, sizeof(buf));

	hid_bpf_release_context(hid_ctx);

	return 0;
}

static int test_inject_input_report_callback(int *key)
{
	struct elem init = {}, *val;
	struct bpf_wq *wq;

	if (bpf_map_update_elem(&hmap, key, &init, 0))
		return -1;

	val = bpf_map_lookup_elem(&hmap, key);
	if (!val)
		return -2;

	wq = &val->work;
	if (bpf_wq_init(wq, &hmap, 0) != 0)
		return -3;

	if (bpf_wq_set_callback(wq, wq_cb_sleepable, 0))
		return -4;

	if (bpf_wq_start(wq, 0))
		return -5;

	return 0;
}

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_test_multiply_events_wq, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 9 /* size */);
	int hid = hid_ctx->hid->id;
	int ret;

	if (!data)
		return 0; /* EPERM check */

	if (data[0] != 1)
		return 0;

	ret = test_inject_input_report_callback(&hid);
	if (ret)
		return ret;

	data[1] += 5;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_multiply_events_wq = {
	.hid_device_event = (void *)hid_test_multiply_events_wq,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_test_multiply_events, struct hid_bpf_ctx *hid_ctx, enum hid_report_type type)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 9 /* size */);
	__u8 buf[9];
	int ret;

	if (!data)
		return 0; /* EPERM check */

	if (data[0] != 1)
		return 0;

	/*
	 * we have to use an intermediate buffer as hid_bpf_input_report
	 * will memset data to \0
	 */
	__builtin_memcpy(buf, data, sizeof(buf));

	buf[0] = 2;
	buf[1] += 5;
	ret = hid_bpf_try_input_report(hid_ctx, HID_INPUT_REPORT, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	/*
	 * In real world we should reset the original buffer as data might be garbage now,
	 * but it actually now has the content of 'buf'
	 */
	data[1] += 5;

	return 9;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_multiply_events = {
	.hid_device_event = (void *)hid_test_multiply_events,
};

SEC("?struct_ops/hid_device_event")
int BPF_PROG(hid_test_infinite_loop_input_report, struct hid_bpf_ctx *hctx,
	     enum hid_report_type report_type, __u64 source)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 6 /* size */);
	__u8 buf[6];

	if (!data)
		return 0; /* EPERM check */

	/*
	 * we have to use an intermediate buffer as hid_bpf_input_report
	 * will memset data to \0
	 */
	__builtin_memcpy(buf, data, sizeof(buf));

	/* always forward the request as-is to the device, hid-bpf should prevent
	 * infinite loops.
	 * the return value is ignored so the event is passing to userspace.
	 */

	hid_bpf_try_input_report(hctx, report_type, buf, sizeof(buf));

	/* each time we process the event, we increment by one data[1]:
	 * after each successful call to hid_bpf_try_input_report, buf
	 * has been memcopied into data by the kernel.
	 */
	data[1] += 1;

	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_infinite_loop_input_report = {
	.hid_device_event = (void *)hid_test_infinite_loop_input_report,
};

SEC("?struct_ops.s/hid_rdesc_fixup")
int BPF_PROG(hid_test_driver_probe, struct hid_bpf_ctx *hid_ctx)
{
	hid_ctx->hid->quirks |= HID_QUIRK_IGNORE_SPECIAL_DRIVER;
	return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops test_driver_probe = {
	.hid_rdesc_fixup = (void *)hid_test_driver_probe,
};
