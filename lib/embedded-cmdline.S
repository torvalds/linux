/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Embed the build-time-rendered bootconfig "kernel" subtree as a flat
 * cmdline string. setup_arch() prepends this to boot_command_line on
 * architectures that select ARCH_SUPPORTS_CMDLINE_FROM_BOOTCONFIG.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 */
	.section .init.rodata.embed_cmdline, "a", %progbits
	.global embedded_kernel_cmdline
embedded_kernel_cmdline:
	.incbin "lib/embedded_cmdline.bin"
	.byte 0
	.global embedded_kernel_cmdline_end
embedded_kernel_cmdline_end:
