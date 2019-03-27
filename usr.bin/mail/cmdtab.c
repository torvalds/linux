/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
#if 0
static char sccsid[] = "@(#)cmdtab.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "def.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Define all of the command names and bindings.
 */

const struct cmd cmdtab[] = {
	/*						msgmask msgflag  */
	/* command	function	argtype		result	& mask   */
	/* -------	--------	-------		------	-------  */
	{ "next",	next,		NDMLIST,	0,	MMNDEL	},
	{ "alias",	group,		M|RAWLIST,	0,	1000 	},
	{ "print",	type,		MSGLIST,	0,	MMNDEL	},
	{ "type",	type,		MSGLIST,	0,	MMNDEL	},
	{ "Type",	Type,		MSGLIST,	0,	MMNDEL	},
	{ "Print",	Type,		MSGLIST,	0,	MMNDEL	},
	{ "visual",	visual,		I|MSGLIST,	0,	MMNORM	},
	{ "top",	top,		MSGLIST,	0,	MMNDEL	},
	{ "touch",	stouch,		W|MSGLIST,	0,	MMNDEL	},
	{ "preserve",	preserve,	W|MSGLIST,	0,	MMNDEL	},
	{ "delete",	deletecmd,	W|P|MSGLIST,	0,	MMNDEL	},
	{ "dp",		deltype,	W|MSGLIST,	0,	MMNDEL	},
	{ "dt",		deltype,	W|MSGLIST,	0,	MMNDEL	},
	{ "undelete",	undeletecmd, 	P|MSGLIST,	MDELETED,MMNDEL },
	{ "unset",	unset,		M|RAWLIST,	1,	1000 	},
	{ "mail",	sendmail,	R|M|I|STRLIST,	0,	0 	},
	{ "mbox",	mboxit,		W|MSGLIST,	0,	0 	},
	{ "more",	more,		MSGLIST,	0,	MMNDEL	},
	{ "page",	more,		MSGLIST,	0,	MMNDEL	},
	{ "More",	More,		MSGLIST,	0,	MMNDEL	},
	{ "Page",	More,		MSGLIST,	0,	MMNDEL	},
	{ "unread",	unread,		MSGLIST,	0,	MMNDEL	},
	{ "!",		shell,		I|STRLIST,	0,	0	},
	{ "copy",	copycmd,	M|STRLIST,	0,	0 	},
	{ "chdir",	schdir,		M|RAWLIST,	0,	1 	},
	{ "cd",		schdir,		M|RAWLIST,	0,	1 	},
	{ "save",	save,		STRLIST,	0,	0	},
	{ "source",	source,		M|RAWLIST,	1,	1	},
	{ "set",	set,		M|RAWLIST,	0,	1000 	},
	{ "shell",	dosh,		I|NOLIST,	0,	0 	},
	{ "version",	pversion,	M|NOLIST,	0,	0 	},
	{ "group",	group,		M|RAWLIST,	0,	1000 	},
	{ "write",	swrite,		STRLIST,	0,	0 	},
	{ "from",	from,		MSGLIST,	0,	MMNORM	},
	{ "file",	file,		T|M|RAWLIST,	0,	1 	},
	{ "folder",	file,		T|M|RAWLIST,	0,	1 	},
	{ "folders",	folders,	T|M|NOLIST,	0,	0 	},
	{ "?",		help,		M|NOLIST,	0,	0 	},
	{ "z",		scroll,		M|STRLIST,	0,	0 	},
	{ "headers",	headers,	MSGLIST,	0,	MMNDEL	},
	{ "help",	help,		M|NOLIST,	0,	0 	},
	{ "=",		pdot,		NOLIST,		0,	0 	},
	{ "Reply",	Respond,	R|I|MSGLIST,	0,	MMNDEL	},
	{ "Respond",	Respond,	R|I|MSGLIST,	0,	MMNDEL	},
	{ "reply",	respond,	R|I|MSGLIST,	0,	MMNDEL	},
	{ "respond",	respond,	R|I|MSGLIST,	0,	MMNDEL	},
	{ "edit",	editor,		I|MSGLIST,	0,	MMNORM	},
	{ "echo",	echo,		M|RAWLIST,	0,	1000 	},
	{ "quit",	quitcmd,	NOLIST,		0,	0 	},
	{ "list",	pcmdlist,	M|NOLIST,	0,	0 	},
	{ "xit",	rexit,		M|NOLIST,	0,	0 	},
	{ "exit",	rexit,		M|NOLIST,	0,	0 	},
	{ "size",	messize,	MSGLIST,	0,	MMNDEL	},
	{ "hold",	preserve,	W|MSGLIST,	0,	MMNDEL	},
	{ "if",		ifcmd,		F|M|RAWLIST,	1,	1 	},
	{ "else",	elsecmd,	F|M|RAWLIST,	0,	0 	},
	{ "endif",	endifcmd,	F|M|RAWLIST,	0,	0 	},
	{ "alternates",	alternates,	M|RAWLIST,	0,	1000 	},
	{ "ignore",	igfield,	M|RAWLIST,	0,	1000 	},
	{ "discard",	igfield,	M|RAWLIST,	0,	1000 	},
	{ "retain",	retfield,	M|RAWLIST,	0,	1000 	},
	{ "saveignore",	saveigfield,	M|RAWLIST,	0,	1000 	},
	{ "savediscard",saveigfield,	M|RAWLIST,	0,	1000 	},
	{ "saveretain",	saveretfield,	M|RAWLIST,	0,	1000 	},
/*	{ "Header",	Header,		STRLIST,	0,	1000	}, */
	{ "core",	core,		M|NOLIST,	0,	0 	},
	{ "#",		null,		M|NOLIST,	0,	0 	},
	{ "clobber",	clobber,	M|RAWLIST,	0,	1 	},
	{ "inc",	inc,		T|NOLIST,	0,	0 	},
	{ 0,		0,		0,		0,	0 	}
};
