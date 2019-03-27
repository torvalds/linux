/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Fabio Checconi
 * Copyright (c) 2010 Luigi Rizzo, Universita` di Pisa
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

/*
 * $Id$
 * $FreeBSD$
 *
 * This file implements the userspace library used by the 'geom'
 * command to load and manipulate disk schedulers.
 */
  
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>

#include <stdio.h>
#include <stdint.h>
#include <libgeom.h>

#include "core/geom.h"
#include "misc/subr.h"

#define	G_SCHED_VERSION	0

uint32_t lib_version = G_LIB_VERSION;
uint32_t version = G_SCHED_VERSION;

/*
 * storage for parameters used by this geom class.
 * Right now only the scheduler name is used.
 */
#define	GSCHED_ALGO	"rr"	/* default scheduler */

/*
 * Adapt to differences in geom library.
 * in V1 struct g_command misses gc_argname, eld, and G_BOOL is undefined
 */
#if G_LIB_VERSION <= 1
#define G_TYPE_BOOL	G_TYPE_NUMBER
#endif
#if G_LIB_VERSION >= 3 && G_LIB_VERSION <= 4
#define G_ARGNAME	NULL,
#else
#define	G_ARGNAME
#endif

static void
gcmd_createinsert(struct gctl_req *req, unsigned flags __unused)
{
	const char *reqalgo;
	char name[64];

	if (gctl_has_param(req, "algo"))
		reqalgo = gctl_get_ascii(req, "algo");
	else
		reqalgo = GSCHED_ALGO;

	snprintf(name, sizeof(name), "gsched_%s", reqalgo);
	/*
	 * Do not complain about errors here, gctl_issue()
	 * will fail anyway.
	 */
	if (modfind(name) < 0)
		kldload(name);
	gctl_issue(req);
}

struct g_command class_commands[] = {
	{ "create", G_FLAG_VERBOSE | G_FLAG_LOADKLD, gcmd_createinsert,
	    {
		{ 'a', "algo", GSCHED_ALGO, G_TYPE_STRING },
		G_OPT_SENTINEL
	    },
	    G_ARGNAME "[-v] [-a algorithm_name] dev ..."
	},
	{ "insert", G_FLAG_VERBOSE | G_FLAG_LOADKLD, gcmd_createinsert,
	    {
		{ 'a', "algo", GSCHED_ALGO, G_TYPE_STRING },
		G_OPT_SENTINEL
	    },
	    G_ARGNAME "[-v] [-a algorithm_name] dev ..."
	},
	{ "configure", G_FLAG_VERBOSE, NULL,
	    {
		{ 'a', "algo", GSCHED_ALGO, G_TYPE_STRING },
		G_OPT_SENTINEL
	    },
	    G_ARGNAME "[-v] [-a algorithm_name] prov ..."
	},
	{ "destroy", G_FLAG_VERBOSE, NULL,
	    {
		{ 'f', "force", NULL, G_TYPE_BOOL },
		G_OPT_SENTINEL
	    },
	    G_ARGNAME "[-fv] prov ..."
	},
	{ "reset", G_FLAG_VERBOSE, NULL, G_NULL_OPTS,
	    G_ARGNAME "[-v] prov ..."
	},
	G_CMD_SENTINEL
};
