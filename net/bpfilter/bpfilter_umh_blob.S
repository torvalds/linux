/* SPDX-License-Identifier: GPL-2.0 */
	.section .bpfilter_umh, "a"
	.global bpfilter_umh_start
bpfilter_umh_start:
	.incbin "net/bpfilter/bpfilter_umh"
	.global bpfilter_umh_end
bpfilter_umh_end:
