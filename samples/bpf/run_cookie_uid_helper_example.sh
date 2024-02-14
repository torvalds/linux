#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
local_dir="$(pwd)"
root_dir=$local_dir/../..
mnt_dir=$(mktemp -d --tmp)

on_exit() {
	iptables -D OUTPUT -m bpf --object-pinned ${mnt_dir}/bpf_prog -j ACCEPT
	umount ${mnt_dir}
	rm -r ${mnt_dir}
}

trap on_exit EXIT
mount -t bpf bpf ${mnt_dir}
./per_socket_stats_example ${mnt_dir}/bpf_prog $1
