/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018 Davide Caratti, Red Hat inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#include <linux/bpf.h>
#include <linux/pkt_cls.h>

__attribute__((section("action-ok"),used)) int action_ok(struct __sk_buff *s)
{
	return TC_ACT_OK;
}

__attribute__((section("action-ko"),used)) int action_ko(struct __sk_buff *s)
{
	s->data = 0x0;
	return TC_ACT_OK;
}

char _license[] __attribute__((section("license"),used)) = "GPL";
