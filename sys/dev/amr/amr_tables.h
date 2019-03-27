/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
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
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
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
 *
 *	$FreeBSD$
 */

/*
 * Lookup table for code-to-text translations.
 */
struct amr_code_lookup {
    char	*string;
    u_int32_t	code;
};

extern char	*amr_describe_code(struct amr_code_lookup *table, u_int32_t code);

#ifndef AMR_DEFINE_TABLES
extern struct amr_code_lookup amr_table_qinit[];
extern struct amr_code_lookup amr_table_sinit[];
extern struct amr_code_lookup amr_table_drvstate[];

#else /* AMR_DEFINE_TABLES */

/********************************************************************************
 * Look up a text description of a numeric code and return a pointer to same.
 */
char *
amr_describe_code(struct amr_code_lookup *table, u_int32_t code)
{
    int		i;

    for (i = 0; table[i].string != NULL; i++)
	if (table[i].code == code)
	    return(table[i].string);
    return(table[i+1].string);
}

struct amr_code_lookup amr_table_qinit[] = {
    {"init scanning drives",		AMR_QINIT_SCAN},
    {"init scanning initialising",	AMR_QINIT_SCANINIT},
    {"init firmware initing",		AMR_QINIT_FIRMWARE},
    {"init in progress",		AMR_QINIT_INPROG},
    {"init spinning drives",		AMR_QINIT_SPINUP},
    {"insufficient memory",		AMR_QINIT_NOMEM},
    {"init flushing cache",		AMR_QINIT_CACHEFLUSH},
    {"init successfully done",		AMR_QINIT_DONE},
    {NULL, 0},
    {"unknown init code", 0}
};

struct amr_code_lookup amr_table_sinit[] = {
    {"init abnormal terminated",	AMR_SINIT_ABEND},
    {"insufficient memory",		AMR_SINIT_NOMEM},
    {"firmware flushing cache",		AMR_SINIT_CACHEFLUSH},
    {"init in progress",		AMR_SINIT_INPROG},
    {"firmware spinning drives",	AMR_SINIT_SPINUP},
    {"init successfully done",		AMR_SINIT_DONE},
    {NULL, 0},
    {"unknown init code", 0}
};

struct amr_code_lookup amr_table_drvstate[] = {
    {"offline",			AMR_DRV_OFFLINE},
    {"degraded",		AMR_DRV_DEGRADED},
    {"optimal",			AMR_DRV_OPTIMAL},
    {"online",			AMR_DRV_ONLINE},
    {"failed",			AMR_DRV_FAILED},
    {"rebuild",			AMR_DRV_REBUILD},
    {"hot spare",		AMR_DRV_HOTSPARE},
    {NULL, 0},
    {"unknown",	0}
};

struct amr_code_lookup amr_table_adaptertype[] = {
    {"Series 431",			AMR_SIG_431},
    {"Series 438",			AMR_SIG_438},
    {"Series 762",			AMR_SIG_762},
    {"Integrated HP NetRAID (T5)",	AMR_SIG_T5},
    {"Series 466",			AMR_SIG_466},
    {"Series 467",			AMR_SIG_467},
    {"Integrated HP NetRAID (T7)",	AMR_SIG_T7},
    {"Series 490",			AMR_SIG_490},
    {NULL, 0},
    {"unknown adapter",	0}
};

#endif
