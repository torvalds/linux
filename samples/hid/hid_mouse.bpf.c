// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "hid_bpf_helpers.h"

SEC("fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_y_event, struct hid_bpf_ctx *hctx)
{
	s16 y;
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 9 /* size */);

	if (!data)
		return 0; /* EPERM check */

	bpf_printk("event: size: %d", hctx->size);
	bpf_printk("incoming event: %02x %02x %02x",
		   data[0],
		   data[1],
		   data[2]);
	bpf_printk("                %02x %02x %02x",
		   data[3],
		   data[4],
		   data[5]);
	bpf_printk("                %02x %02x %02x",
		   data[6],
		   data[7],
		   data[8]);

	y = data[3] | (data[4] << 8);

	y = -y;

	data[3] = y & 0xFF;
	data[4] = (y >> 8) & 0xFF;

	bpf_printk("modified event: %02x %02x %02x",
		   data[0],
		   data[1],
		   data[2]);
	bpf_printk("                %02x %02x %02x",
		   data[3],
		   data[4],
		   data[5]);
	bpf_printk("                %02x %02x %02x",
		   data[6],
		   data[7],
		   data[8]);

	return 0;
}

SEC("fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_x_event, struct hid_bpf_ctx *hctx)
{
	s16 x;
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 9 /* size */);

	if (!data)
		return 0; /* EPERM check */

	x = data[1] | (data[2] << 8);

	x = -x;

	data[1] = x & 0xFF;
	data[2] = (x >> 8) & 0xFF;
	return 0;
}

SEC("fmod_ret/hid_bpf_rdesc_fixup")
int BPF_PROG(hid_rdesc_fixup, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 4096 /* size */);

	if (!data)
		return 0; /* EPERM check */

	bpf_printk("rdesc: %02x %02x %02x",
		   data[0],
		   data[1],
		   data[2]);
	bpf_printk("       %02x %02x %02x",
		   data[3],
		   data[4],
		   data[5]);
	bpf_printk("       %02x %02x %02x ...",
		   data[6],
		   data[7],
		   data[8]);

	/*
	 * The original report descriptor contains:
	 *
	 * 0x05, 0x01,                    //   Usage Page (Generic Desktop)      30
	 * 0x16, 0x01, 0x80,              //   Logical Minimum (-32767)          32
	 * 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           35
	 * 0x09, 0x30,                    //   Usage (X)                         38
	 * 0x09, 0x31,                    //   Usage (Y)                         40
	 *
	 * So byte 39 contains Usage X and byte 41 Usage Y.
	 *
	 * We simply swap the axes here.
	 */
	data[39] = 0x31;
	data[41] = 0x30;

	return 0;
}

char _license[] SEC("license") = "GPL";
