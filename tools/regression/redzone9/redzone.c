/*-
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>


SYSCTL_NODE(_debug, OID_AUTO, redzone, CTLFLAG_RW, 0, "");

#define	REDZONE_MALLOC_UNDERFLOW		0
#define	REDZONE_MALLOC_OVERFLOW			1
#define	REDZONE_REALLOC_SMALLER_UNDERFLOW	2
#define	REDZONE_REALLOC_SMALLER_OVERFLOW	3
#define	REDZONE_REALLOC_BIGGER_UNDERFLOW	4
#define	REDZONE_REALLOC_BIGGER_OVERFLOW		5

static int
redzone_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_char *p = NULL;
	int error, val = 0;

	error = sysctl_handle_int(oidp, &val, sizeof(val), req);
	if (error != 0 || req->newptr == NULL)
		return (0);
	switch (arg2) {
	case REDZONE_MALLOC_UNDERFLOW:
		p = malloc(10, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		p[-1] = '\0';
		break;
	case REDZONE_MALLOC_OVERFLOW:
		p = malloc(10, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		p[10] = '\0';
		break;
	case REDZONE_REALLOC_SMALLER_UNDERFLOW:
		p = malloc(8192, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		p[-1] = '\0';
		p = realloc(p, 10, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		break;
	case REDZONE_REALLOC_SMALLER_OVERFLOW:
		p = malloc(8192, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		p[8192] = '\0';
		p = realloc(p, 10, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		break;
	case REDZONE_REALLOC_BIGGER_UNDERFLOW:
		p = malloc(10, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		p[-1] = '\0';
		p = realloc(p, 8192, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		break;
	case REDZONE_REALLOC_BIGGER_OVERFLOW:
		p = malloc(10, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		p[10] = '\0';
		p = realloc(p, 8192, M_TEMP, M_NOWAIT);
		if (p == NULL)
			return (ENOMEM);
		break;
	}
	free(p, M_TEMP);
        return (0);
}
SYSCTL_PROC(_debug_redzone, OID_AUTO, malloc_underflow, CTLTYPE_INT | CTLFLAG_RW,
    NULL, REDZONE_MALLOC_UNDERFLOW, redzone_sysctl, "I", "");
SYSCTL_PROC(_debug_redzone, OID_AUTO, malloc_overflow, CTLTYPE_INT | CTLFLAG_RW,
    NULL, REDZONE_MALLOC_OVERFLOW, redzone_sysctl, "I", "");
SYSCTL_PROC(_debug_redzone, OID_AUTO, realloc_smaller_underflow, CTLTYPE_INT | CTLFLAG_RW,
    NULL, REDZONE_REALLOC_SMALLER_UNDERFLOW, redzone_sysctl, "I", "");
SYSCTL_PROC(_debug_redzone, OID_AUTO, realloc_smaller_overflow, CTLTYPE_INT | CTLFLAG_RW,
    NULL, REDZONE_REALLOC_SMALLER_OVERFLOW, redzone_sysctl, "I", "");
SYSCTL_PROC(_debug_redzone, OID_AUTO, realloc_bigger_underflow, CTLTYPE_INT | CTLFLAG_RW,
    NULL, REDZONE_REALLOC_BIGGER_UNDERFLOW, redzone_sysctl, "I", "");
SYSCTL_PROC(_debug_redzone, OID_AUTO, realloc_bigger_overflow, CTLTYPE_INT | CTLFLAG_RW,
    NULL, REDZONE_REALLOC_BIGGER_OVERFLOW, redzone_sysctl, "I", "");

static int
redzone_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}
static moduledata_t redzone_module = {
	"redzone",
	redzone_modevent,
	NULL
};
DECLARE_MODULE(redzone, redzone_module, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
