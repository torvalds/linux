/*	$OpenBSD: pw.c,v 1.2 2002/06/23 03:11:09 deraadt Exp $	*/
/*
 * Copyright (c) 1993, 1994, 1995, 1996 by Chris Provenzano and contributors, 
 * proven@mit.edu All rights reserved.
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
 *	This product includes software developed by Chris Provenzano,
 *	the University of California, Berkeley, and contributors.
 * 4. Neither the name of Chris Provenzano, the University, nor the names of
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO, THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include "test.h"

int
main()
{
	struct passwd *pw;
	struct passwd pwbuf;
	char buf[8192];
	char name[16];

	CHECKn(pw = getpwuid(getuid()));
	printf("getpwuid(%u) => %p\n", getuid(), pw);
	printf(" name: %s\n  uid: %u\n  gid: %u\n"
	    "class: %s\ngecos: %s\n  dir: %s\nshell: %s\n",
	    pw->pw_name, pw->pw_uid, pw->pw_gid,
	    pw->pw_class, pw->pw_gecos, pw->pw_dir, pw->pw_shell);

	strlcpy(name, pw->pw_name, sizeof name);
	CHECKe(getpwnam_r(name, &pwbuf, buf, sizeof buf, &pw));
	ASSERT(pwbuf.pw_uid == getuid());

	SUCCEED;
}
