// SPDX-License-Identifier: GPL-2.0
// test ir decoder
//
// Copyright (C) 2018 Sean Young <sean@mess.org>

#include <linux/bpf.h>
#include <linux/lirc.h>
#include <bpf/bpf_helpers.h>

SEC("lirc_mode2")
int bpf_decoder(unsigned int *sample)
{
	if (LIRC_IS_PULSE(*sample)) {
		unsigned int duration = LIRC_VALUE(*sample);

		/*
		 * Flag bits picked deliberately low: rc-loopback simulates
		 * a receiver overflow for any pulse over MS_TO_US(50) (see
		 * loop_tx_ir() in rc-loopback.c), which would silently
		 * swallow the sample before it ever reaches this decoder.
		 */
		if (duration & 0x8000)
			bpf_rc_keydown(sample, 0x40, duration & 0x3fff, 0);
		if (duration & 0x4000)
			bpf_rc_pointer_rel(sample, (duration >> 7) & 0x7f,
					   duration & 0x7f);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
