/* $OpenBSD: aicasm.h,v 1.6 2003/12/24 23:27:55 krw Exp $ */
/*	$NetBSD: aicasm.h,v 1.2 2003/04/19 19:26:10 fvdl Exp $	*/

/*
 * Assembler for the sequencer program downloaded to Aic7xxx SCSI host adapters
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 2001, 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/sys/dev/aic7xxx/aicasm/aicasm.h,v 1.16 2002/08/31 06:39:40 gibbs Exp $
 */

#ifdef __linux__
#include "../queue.h"
#else
#include <sys/queue.h>
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef struct path_entry {
	char	*directory;
	int	quoted_includes_only;
	SLIST_ENTRY(path_entry) links;
} *path_entry_t;

typedef enum {  
	QUOTED_INCLUDE,
	BRACKETED_INCLUDE,
	SOURCE_FILE
} include_type;

SLIST_HEAD(path_list, path_entry);

extern struct path_list search_path;
extern struct cs_tailq cs_tailq;
extern struct scope_list scope_stack;
extern struct symlist patch_functions;
extern int includes_search_curdir;		/* False if we've seen -I- */
extern char *appname;
extern char *stock_include_file;
extern int yylineno;
extern char *yyfilename;
extern char *prefix;
extern char *patch_arg_list;
extern char *versions;
extern int   src_mode;
extern int   dst_mode;
struct symbol;

void stop(const char *errstring, int err_code);
void include_file(char *file_name, include_type type);
void expand_macro(struct symbol *macro_symbol);
struct instruction *seq_alloc(void);
struct critical_section *cs_alloc(void);
struct scope *scope_alloc(void);
void process_scope(struct scope *);
