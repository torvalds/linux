/* SPDX-License-Identifier: GPL-2.0 */
	.section .init.rodata, "a"
	.global bpf_preload_umd_start
bpf_preload_umd_start:
	.incbin "kernel/bpf/preload/bpf_preload_umd"
	.global bpf_preload_umd_end
bpf_preload_umd_end:
