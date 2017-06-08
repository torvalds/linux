/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SOC_NPS_MTM_H
#define SOC_NPS_MTM_H

#define CTOP_INST_HWSCHD_OFF_R3                 0x3B6F00BF
#define CTOP_INST_HWSCHD_RESTORE_R3             0x3E6F70C3

static inline void hw_schd_save(unsigned int *flags)
{
	__asm__ __volatile__(
	"       .word %1\n"
	"       st r3,[%0]\n"
	:
	: "r"(flags), "i"(CTOP_INST_HWSCHD_OFF_R3)
	: "r3", "memory");
}

static inline void hw_schd_restore(unsigned int flags)
{
	__asm__ __volatile__(
	"       mov r3, %0\n"
	"       .word %1\n"
	:
	: "r"(flags), "i"(CTOP_INST_HWSCHD_RESTORE_R3)
	: "r3");
}

#endif /* SOC_NPS_MTM_H */
