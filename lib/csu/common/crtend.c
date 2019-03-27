/*-
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Copyright 2018 Andrew Turner
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "crt.h"

#ifdef HAVE_CTORS
typedef void (*crt_func)(void);

/*
 * On some architectures and toolchains we may need to call the .ctors.
 * These are called in the reverse order they are in the ELF file.
 */
static void __do_global_ctors_aux(void) __used;

static crt_func __CTOR_END__[] __section(".ctors") __used = {
	(crt_func)0
};
static crt_func __DTOR_END__[] __section(".dtors") __used = {
	(crt_func)0
};
static crt_func __JCR_LIST__[] __section(".jcr") __used = {
	(crt_func)0
};

static void
__do_global_ctors_aux(void)
{
	crt_func fn;
	int n;

	for (n = 1;; n++) {
		fn = __CTOR_END__[-n];
		if (fn == (crt_func)0 || fn == (crt_func)-1)
			break;
		fn();
	}
}

asm (
    ".pushsection .init		\n"
    "\t" INIT_CALL_SEQ(__do_global_ctors_aux) "\n"
    ".popsection		\n"
);
#endif
