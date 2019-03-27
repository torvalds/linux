/*-
 * Copyright (c) 2010 Juli Mallett.
 * All rights reserved.
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * This requires makecontext() to setup a valid GP for locating
 * _ctx_done rather than deriving GP from T9 on entry.  Currently this
 * uses the GP inherited from getcontext() assuming that getcontext()
 * is in the same shared object as _ctx_done().  For N32 and N64, GP
 * is caller-save so will be preserved across the call to the callback
 * function.  For O32, GP is callee-save, so save it in a different
 * caller-save register (S1) while invoking the callback.  This is
 * done instead of the usual SETUP_GP/SAVE_GP to avoid disturbing the
 * stack frame setup by makecontext() for the callback function.
 */
ENTRY(_ctx_start)
#ifdef __mips_o32
	move	s1, gp
#endif
	jalr	t9

#ifdef __mips_o32
	move	gp, s1
#endif
	move	a0, s0
	PTR_LA	t9, _ctx_done
	jalr	t9

	break	0
END(_ctx_start)
