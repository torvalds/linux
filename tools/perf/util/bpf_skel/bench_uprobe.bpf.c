// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2023 Red Hat
#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

SEC("uprobe")
int BPF_UPROBE(empty)
{
       return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
