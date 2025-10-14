/* SPDX-License-Identifier: GPL-2.0 */

MEMCPY_FN(memcpy_orig,
	mem_alloc,
	mem_free,
	"x86-64-unrolled",
	"unrolled memcpy() in arch/x86/lib/memcpy_64.S")

MEMCPY_FN(__memcpy,
	mem_alloc,
	mem_free,
	"x86-64-movsq",
	"movsq-based memcpy() in arch/x86/lib/memcpy_64.S")
