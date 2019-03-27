/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NANDSIM_CONFPARSER_H_
#define _NANDSIM_CONFPARSER_H_

#define VALUE_UINT	0x08
#define VALUE_INT	0x10
#define VALUE_UINTARRAY	0x18
#define VALUE_INTARRAY	0x20
#define VALUE_STRING	0x28
#define VALUE_CHAR	0x40
#define VALUE_BOOL	0x48

#define SIZE_8	0x01
#define SIZE_16	0x02
#define SIZE_32	0x04

#include "nandsim_rcfile.h"

/*
 * keyname	= name of a key,
 * mandatory	= is key mandatory in section belonging to, 0=false 1=true
 * valuetype	= what kind of value is assigned to that key, e.g.
 *		  VALUE_UINT | SIZE_8 -- unsigned uint size 8 bits;
 *		  VALUE_UINTARRAY | SIZE_8 -- array of uints 8-bit long;
 *		  VALUE_BOOL -- 'on', 'off','true','false','yes' or 'no'
 *		  literals;
 *		  VALUE_STRING -- strings
 * field	= ptr to the field that should hold value for parsed value
 * maxlength	= contains maximum length of an array (used only with either
 *		  VALUE_STRING or VALUE_(U)INTARRAY value types.
 */
struct nandsim_key {
	const char	*keyname;
	uint8_t		mandatory;
	uint8_t		valuetype;
	void		*field;
	uint32_t	maxlength;
};
struct nandsim_section {
	const char		*name;
	struct nandsim_key	*keys;
};

struct nandsim_config {
	struct sim_param	**simparams;
	struct sim_chip		**simchips;
	struct sim_ctrl		**simctrls;
	int			chipcnt;
	int			ctrlcnt;
};

int parse_intarray(char *, int **);
int parse_config(char *, const char *);
int parse_section(struct rcfile *, const char *, int);
int compare_configs(struct nandsim_config *, struct nandsim_config *);
int convert_argint(char *, int *);
int convert_arguint(char *, unsigned int *);

#endif /* _NANDSIM_CONFPARSER_H_ */
