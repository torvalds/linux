/* SPDX-License-Identifier: GPL-2.0 */

MEMSET_FN(memset_orig,
	"x86-64-unrolled",
	"unrolled memset() in arch/x86/lib/memset_64.S")

MEMSET_FN(__memset,
	"x86-64-stosq",
	"movsq-based memset() in arch/x86/lib/memset_64.S")
