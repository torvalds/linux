/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Simple interface to link xor_vmx.c and xor_vmx_glue.c
 *
 * Separating these file ensures that no altivec instructions are run
 * outside of the enable/disable altivec block.
 */

void xor_gen_altivec_inner(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes);
