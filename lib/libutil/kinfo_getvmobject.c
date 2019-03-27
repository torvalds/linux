/*
 * Copyright (c) 2013 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <stdlib.h>
#include <string.h>

#include "libutil.h"

struct kinfo_vmobject *
kinfo_getvmobject(int *cntp)
{
	char *buf, *bp, *ep;
	struct kinfo_vmobject *kvo, *list, *kp;
	size_t len;
	int cnt, i;

	buf = NULL;
	for (i = 0; i < 3; i++) {
		if (sysctlbyname("vm.objects", NULL, &len, NULL, 0) < 0) {
			free(buf);
			return (NULL);
		}
		buf = reallocf(buf, len);
		if (buf == NULL)
			return (NULL);
		if (sysctlbyname("vm.objects", buf, &len, NULL, 0) == 0)
			goto unpack;
		if (errno != ENOMEM) {
			free(buf);
			return (NULL);
		}
	}
	free(buf);
	return (NULL);

unpack:
	/* Count items */
	cnt = 0;
	bp = buf;
	ep = buf + len;
	while (bp < ep) {
		kvo = (struct kinfo_vmobject *)(uintptr_t)bp;
		bp += kvo->kvo_structsize;
		cnt++;
	}

	list = calloc(cnt, sizeof(*list));
	if (list == NULL) {
		free(buf);
		return (NULL);
	}

	/* Unpack */
	bp = buf;
	kp = list;
	while (bp < ep) {
		kvo = (struct kinfo_vmobject *)(uintptr_t)bp;
		memcpy(kp, kvo, kvo->kvo_structsize);
		bp += kvo->kvo_structsize;
		kp->kvo_structsize = sizeof(*kp);
		kp++;
	}
	free(buf);
	*cntp = cnt;
	return (list);
}
