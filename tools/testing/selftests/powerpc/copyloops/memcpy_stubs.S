/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/ppc_asm.h>

FUNC_START(memcpy)
	b test_memcpy

FUNC_START(backwards_memcpy)
	b test_backwards_memcpy
