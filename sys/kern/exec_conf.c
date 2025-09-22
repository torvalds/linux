/*	$OpenBSD: exec_conf.c,v 1.38 2021/12/07 22:17:02 guenther Exp $	*/
/*	$NetBSD: exec_conf.c,v 1.16 1995/12/09 05:34:47 cgd Exp $	*/

/*
 * Copyright (c) 1993, 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/exec_script.h>
#include <sys/exec_elf.h>

const struct execsw execsw[] = {
	{	/* shell scripts */
		.es_hdrsz = EXEC_SCRIPT_HDRSZ,
		.es_check = exec_script_makecmds,
	},
	{	/* elf binaries */
		.es_hdrsz = sizeof(Elf_Ehdr),
		.es_check = exec_elf_makecmds,
	},
};
int nexecs = (sizeof execsw / sizeof(*execsw));
int exec_maxhdrsz;

void	init_exec(void);

void
init_exec(void)
{
	int i;

	/*
	 * figure out the maximum size of an exec header.
	 */
	for (i = 0; i < nexecs; i++)
		if (execsw[i].es_check != NULL &&
		    execsw[i].es_hdrsz > exec_maxhdrsz)
			exec_maxhdrsz = execsw[i].es_hdrsz;
}
