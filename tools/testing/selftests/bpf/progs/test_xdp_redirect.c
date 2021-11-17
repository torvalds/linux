/* Copyright (c) 2017 VMware
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int _version SEC("version") = 1;

SEC("redirect_to_111")
int xdp_redirect_to_111(struct xdp_md *xdp)
{
	return bpf_redirect(111, 0);
}
SEC("redirect_to_222")
int xdp_redirect_to_222(struct xdp_md *xdp)
{
	return bpf_redirect(222, 0);
}

char _license[] SEC("license") = "GPL";
