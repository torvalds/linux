/*-
 * Copyright (c) 2014 SRI International
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-11-C-0249)
 * ("MRC2"), as part of the DARPA MRC research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

static inline int
beri_get_core(void)
{
	uint32_t cinfo;

	cinfo = mips_rd_cinfo();
	return (cinfo & 0xffff);
}

static inline int
beri_get_ncores(void)
{
	uint32_t cinfo;

	cinfo = mips_rd_cinfo();
	return ((cinfo >> 16) + 1);
}

static inline int
beri_get_thread(void)
{
	uint32_t tinfo;

	tinfo = mips_rd_tinfo();
	return (tinfo & 0xffff);
}

static inline int
beri_get_nthreads(void)
{
	uint32_t tinfo;

	tinfo = mips_rd_tinfo();
	return ((tinfo >> 16) + 1);
}

static inline int
beri_get_cpu(void)
{

	return ((beri_get_core() * beri_get_nthreads()) + beri_get_thread());
}

static inline int
beri_get_ncpus(void)
{

	return(beri_get_ncores() * beri_get_nthreads());
}

void beripic_setup_ipi(device_t dev, u_int tid, u_int ipi_irq);
void beripic_send_ipi(device_t dev, u_int tid);
void beripic_clear_ipi(device_t dev, u_int tid);
