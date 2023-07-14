// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Red hat */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
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

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_first_event, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 3 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	callback_check = rw_data[1];

	rw_data[2] = rw_data[1] + 5;

	return hid_ctx->size;
}

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_second_event, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 4 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	rw_data[3] = rw_data[2] + 5;

	return hid_ctx->size;
}

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_change_report_id, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 3 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	rw_data[0] = 2;

	return 9;
}

SEC("syscall")
int attach_prog(struct attach_prog_args *ctx)
{
	ctx->retval = hid_bpf_attach_prog(ctx->hid,
					  ctx->prog_fd,
					  ctx->insert_head ? HID_BPF_FLAG_INSERT_HEAD :
							     HID_BPF_FLAG_NONE);
	return 0;
}

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

SEC("?fmod_ret/hid_bpf_rdesc_fixup")
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

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_test_insert1, struct hid_bpf_ctx *hid_ctx)
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

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_test_insert2, struct hid_bpf_ctx *hid_ctx)
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

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_test_insert3, struct hid_bpf_ctx *hid_ctx)
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
