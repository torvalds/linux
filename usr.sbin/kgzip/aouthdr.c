/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stddef.h>
#include "aouthdr.h"

#define KGZ_FIX_NSIZE	0	/* Run-time fixup */

const struct kgz_aouthdr0 aouthdr0 = {
    /* a.out header */
    {
	MID_I386 << 020 | OMAGIC,			/* a_midmag */
	0,						/* a_text */
	sizeof(struct kgz_hdr) + KGZ_FIX_NSIZE, 	/* a_data */
	0,						/* a_bss */
	sizeof(struct nlist) * KGZ__STNUM,		/* a_syms */
	0,						/* a_entry */
	0,						/* a_trsize */
	0						/* a_drsize */
    }
};

const struct kgz_aouthdr1 aouthdr1 = {
    /* Symbol table */
    {
	{
	    {
		(char *)offsetof(struct kgz__strtab,
				 kgz)			/* n_un */
	    },
	    N_DATA | N_EXT,				/* n_type */
	    AUX_OBJECT, 				/* n_other */
	    0,						/* n_desc */
	    0						/* n_value */
	},
	{
	    {
		(char *)offsetof(struct kgz__strtab,
				 kgz_ndata)		/* n_un */
	    },
	    N_DATA | N_EXT,				/* n_type */
	    AUX_OBJECT, 				/* n_other */
	    0,						/* n_desc */
	    sizeof(struct kgz_hdr)			/* n_value */
	}
    },
    /* String table */
    {
	sizeof(struct kgz__strtab),			/* length */
	KGZ__STR_KGZ,					/* kgz */
	KGZ__STR_KGZ_NDATA				/* kgz_ndata */
    }
};
