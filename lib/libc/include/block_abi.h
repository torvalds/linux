/*-
 * Copyright (c) 2014 David T Chisnall
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
 *
 * $FreeBSD$
 */

#ifdef __BLOCKS__
/**
 * Declares a block variable.  This macro is defined in the trivial way for
 * compilers that support blocks and exposing the ABI in the source for other
 * compilers.
 */
#define	DECLARE_BLOCK(retTy, name, argTys, ...)\
	retTy(^name)(argTys, ## __VA_ARGS__)
/**
 * Calls a block variable.  This macro is defined in the trivial way for
 * compilers that support blocks and exposing the ABI in the source for other
 * compilers.
 */
#define CALL_BLOCK(name, ...) name(__VA_ARGS__)
#else // !__BLOCKS__
#define	DECLARE_BLOCK(retTy, name, argTys, ...)\
	struct {\
		void *isa;\
		int flags;\
		int reserved;\
		retTy (*invoke)(void *, ...);\
	} *name
#define CALL_BLOCK(name, ...) (name)->invoke(name, __VA_ARGS__)
#endif // __BLOCKS__
/**
 * Returns the pointer to the block-invoke function.  This is used for passing
 * blocks to functions that want a function pointer and a data pointer.
 */
#define GET_BLOCK_FUNCTION(x) \
	(((struct {\
		void *isa;\
		int flags;\
		int reserved;\
		void (*invoke)(void *, ...);\
	}*)(void*)x)->invoke)
