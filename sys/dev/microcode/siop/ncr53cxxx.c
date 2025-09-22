/*	$OpenBSD: ncr53cxxx.c,v 1.8 2023/01/04 10:05:44 jsg Exp $ */
/*	$NetBSD: ncr53cxxx.c,v 1.14 2005/02/11 06:21:22 simonb Exp $	*/

/*
 * Copyright (c) 1995,1999 Michael L. Hitch
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

/*	ncr53cxxx.c	- SCSI SCRIPTS Assembler		*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef AMIGA
#define strcmpi	strcasecmp
#endif

#define	MAXTOKENS	16
#define	MAXINST		1024
#define	MAXSYMBOLS	128

struct {
	int	type;
	char	*name;
} tokens[MAXTOKENS];
int	ntokens;
int	tokenix;

void	f_proc (void);
void	f_pass (void);
void	f_list (void);		/* ENTRY, EXTERNAL label list */
void	f_define (void);	/* ABSOLUTE, RELATIVE label list */
void	f_move (void);
void	f_jump (void);
void	f_call (void);
void	f_return (void);
void	f_int (void);
void	f_intfly (void);
void	f_select (void);
void	f_reselect (void);
void	f_wait (void);
void	f_disconnect (void);
void	f_set (void);
void	f_clear (void);
void	f_load (void);
void	f_store (void);
void	f_nop (void);
void	f_arch (void);

struct {
	char	*name;
	void	(*func)(void);
} directives[] = {
	{"PROC",	f_proc},
	{"PASS",	f_pass},
	{"ENTRY",	f_list},
	{"ABSOLUTE",	f_define},
	{"EXTERN",	f_list},
	{"EXTERNAL",	f_list},
	{"RELATIVE",	f_define},
	{"MOVE",	f_move},
	{"JUMP",	f_jump},
	{"CALL",	f_call},
	{"RETURN",	f_return},
	{"INT",		f_int},
	{"INTFLY",	f_intfly},
	{"SELECT",	f_select},
	{"RESELECT",	f_reselect},
	{"WAIT",	f_wait},
	{"DISCONNECT",	f_disconnect},
	{"SET",		f_set},
	{"CLEAR",	f_clear},
	{"LOAD",	f_load},
	{"STORE",	f_store},
	{"NOP",		f_nop},
	{"ARCH",	f_arch},
	{NULL, NULL}};

u_int32_t script[MAXINST];
int	dsps;
char	*script_name = "SCRIPT";
u_int32_t inst0, inst1, inst2;
unsigned int	ninsts;
unsigned int	npatches;

struct patchlist {
	struct patchlist *next;
	unsigned	offset;
} *patches;

#define	S_LABEL		0x0000
#define	S_ABSOLUTE	0x0001
#define	S_RELATIVE	0x0002
#define	S_EXTERNAL	0x0003
#define	F_DEFINED	0x0001
#define	F_ENTRY		0x0002
struct {
	short	type;
	short	flags;
	u_int32_t value;
	struct patchlist *patchlist;
	char	*name;
} symbols[MAXSYMBOLS];
int nsymbols;

char	*stypes[] = {"Label", "Absolute", "Relative", "External"};

char	*phases[] = {
	"data_out", "data_in", "cmd", "status",
	"res4", "res5", "msg_out", "msg_in"
};

struct ncrregs {
	char *name;
	int addr[5];
};
#define ARCH700 1
#define ARCH710 2
#define ARCH720 3
#define ARCH810 4
#define ARCH825 5

struct ncrregs 	regs[] = {
	{"scntl0",	{0x00, 0x00, 0x00, 0x00, 0x00}},
	{"scntl1",	{0x01, 0x01, 0x01, 0x01, 0x01}},
	{"sdid",	{0x02, 0x02,   -1,   -1,   -1}},
	{"sien",	{0x03, 0x03,   -1,   -1,   -1}},
	{"scid",	{0x04, 0x04,   -1,   -1,   -1}},
	{"scntl2",	{  -1,   -1, 0x02, 0x02, 0x02}},
	{"scntl3",	{  -1,   -1, 0x03, 0x03, 0x03}},
	{"scid", 	{  -1,   -1, 0x04, 0x04, 0x04}},
	{"sxfer",	{0x05, 0x05, 0x05, 0x05, 0x05}},
	{"sodl",	{0x06, 0x06,   -1,   -1,   -1}},
	{"socl",	{0x07, 0x07,   -1,   -1,   -1}},
	{"sdid",	{  -1,   -1, 0x06, 0x06, 0x06}},
	{"gpreg",	{  -1,   -1, 0x07, 0x07, 0x07}},
	{"sfbr",	{0x08, 0x08, 0x08, 0x08, 0x08}},
	{"sidl",	{0x09, 0x09,   -1,   -1,   -1}},
	{"sbdl",	{0x0a, 0x0a,   -1,   -1,   -1}},
	{"socl",	{  -1,   -1, 0x09, 0x09, 0x09}},
	{"ssid", 	{  -1,   -1, 0x0a, 0x0a, 0x0a}},
	{"sbcl",	{0x0b, 0x0b, 0x0b, 0x0b, 0x0b}}, 
	{"dstat",	{0x0c, 0x0c, 0x0c, 0x0c, 0x0c}},
	{"sstat0",	{0x0d, 0x0d, 0x0d, 0x0d, 0x0d}},
	{"sstat1",	{0x0e, 0x0e, 0x0e, 0x0e, 0x0e}},
	{"sstat2",	{0x0f, 0x0f, 0x0f, 0x0f, 0x0f}},
	{"dsa0",	{  -1, 0x10, 0x10, 0x10, 0x10}},
	{"dsa1",	{  -1, 0x11, 0x11, 0x11, 0x11}},
	{"dsa2",	{  -1, 0x12, 0x12, 0x12, 0x12}},
	{"dsa3",	{  -1, 0x13, 0x13, 0x13, 0x13}},
	{"ctest0",	{0x14, 0x14, 0x18, 0x18, 0x18}},
	{"ctest1",	{0x15, 0x15, 0x19, 0x19, 0x19}},
	{"ctest2",	{0x16, 0x16, 0x1a, 0x1a, 0x1a}},
	{"ctest3",	{0x17, 0x17, 0x1b, 0x1b, 0x1b}},
	{"ctest4",	{0x18, 0x18, 0x21, 0x21, 0x21}},
	{"ctest5",	{0x19, 0x19, 0x22, 0x22, 0x22}},
	{"ctest6",	{0x1a, 0x1a, 0x23, 0x23, 0x23}},
	{"ctest7",	{0x1b, 0x1b,   -1,   -1,   -1}},
	{"temp0",	{0x1c, 0x1c, 0x1c, 0x1c, 0x1c}},
	{"temp1",	{0x1d, 0x1d, 0x1d, 0x1d, 0x1d}},
	{"temp2", 	{0x1e, 0x1e, 0x1e, 0x1e, 0x1e}},
	{"temp3",	{0x1f, 0x1f, 0x1f, 0x1f, 0x1f}},
	{"dfifo",	{0x20, 0x20, 0x20, 0x20, 0x20}},
	{"istat", 	{0x21, 0x21, 0x14, 0x14, 0x14}},
	{"ctest8",	{0x22, 0x22,   -1,   -1,   -1}},
	{"lcrc",	{  -1, 0x23,   -1,   -1,   -1}},
	{"ctest9",	{0x23,   -1,   -1,   -1,   -1}},
	{"dbc0",	{0x24, 0x24, 0x24, 0x24, 0x24}},
	{"dbc1",	{0x25, 0x25, 0x25, 0x25, 0x25}},
	{"dbc2",	{0x26, 0x26, 0x26, 0x26, 0x26}},
	{"dcmd",	{0x27, 0x27, 0x27, 0x27, 0x27}},
	{"dnad0",	{0x28, 0x28, 0x28, 0x28, 0x28}},
	{"dnad1",	{0x29, 0x29, 0x29, 0x29, 0x29}},
	{"dnad2",	{0x2a, 0x2a, 0x2a, 0x2a, 0x2a}},
	{"dnad3",	{0x2b, 0x2b, 0x2b, 0x2b, 0x2b}},
	{"dsp0",	{0x2c, 0x2c, 0x2c, 0x2c, 0x2c}},
	{"dsp1",	{0x2d, 0x2d, 0x2d, 0x2d, 0x2d}},
	{"dsp2",	{0x2e, 0x2e, 0x2e, 0x2e, 0x2e}},
	{"dsp3",	{0x2f, 0x2f, 0x2f, 0x2f, 0x2f}},
	{"dsps0",	{0x30, 0x30, 0x30, 0x30, 0x30}},
	{"dsps1",	{0x31, 0x31, 0x31, 0x31, 0x31}},
	{"dsps2",	{0x32, 0x32, 0x32, 0x32, 0x32}},
	{"dsps3",	{0x33, 0x33, 0x33, 0x33, 0x33}},
	{"scratch0",	{  -1, 0x34,   -1,   -1,   -1}},
	{"scratch1",	{  -1, 0x35,   -1,   -1,   -1}},
	{"scratch2",	{  -1, 0x36,   -1,   -1,   -1}},
	{"scratch3",	{  -1, 0x37,   -1,   -1,   -1}},
	{"scratcha0",	{0x10,   -1, 0x34, 0x34, 0x34}},
	{"scratcha1",	{0x11,   -1, 0x35, 0x35, 0x35}},
	{"scratcha2",	{0x12,   -1, 0x36, 0x36, 0x36}},
	{"scratcha3",	{0x13,   -1, 0x37, 0x37, 0x37}},
	{"dmode",	{0x34, 0x38, 0x38, 0x38, 0x38}},
	{"dien",	{0x39, 0x39, 0x39, 0x39, 0x39}},
	{"dwt",		{0x3a, 0x3a, 0x3a,   -1,   -1}},
	{"sbr",		{  -1,   -1,   -1, 0x3a, 0x3a}},
	{"dcntl",	{0x3b, 0x3b, 0x3b, 0x3b, 0x3b}},
	{"addr0",	{  -1, 0x3c, 0x3c, 0x3c, 0x3c}},
	{"addr1",	{  -1, 0x3d, 0x3d, 0x3d, 0x3d}},
	{"addr2",	{  -1, 0x3e, 0x3e, 0x3e, 0x3e}},
	{"addr3",	{  -1, 0x3f, 0x3f, 0x3f, 0x3f}},
	{"sien0",	{  -1,   -1, 0x40, 0x40, 0x40}},
	{"sien1",	{  -1,   -1, 0x41, 0x41, 0x41}},
	{"sist0",	{  -1,   -1, 0x42, 0x42, 0x42}},
	{"sist1",	{  -1,   -1, 0x43, 0x43, 0x43}},
	{"slpar",	{  -1,   -1, 0x44, 0x44, 0x44}},
	{"swide",	{  -1,   -1, 0x45,   -1, 0x45}},
	{"macntl",	{  -1,   -1, 0x46, 0x46, 0x46}},
	{"gpcntl",	{  -1,   -1, 0x47, 0x47, 0x47}},
	{"stime0",	{  -1,   -1, 0x48, 0x48, 0x48}},
	{"stime1",	{  -1,   -1, 0x49, 0x49, 0x49}},
	{"respid0",	{  -1,   -1, 0x4a, 0x4a, 0x4a}},
	{"respid1",	{  -1,   -1, 0x4b,   -1, 0x4b}},
	{"stest0",	{  -1,   -1, 0x4c, 0x4c, 0x4c}},
	{"stest1",	{  -1,   -1, 0x4d, 0x4d, 0x4d}},
	{"stest2",	{  -1,   -1, 0x4e, 0x4e, 0x4e}},
	{"stest3",	{  -1,   -1, 0x4f, 0x4f, 0x4f}},
	{"sidl0",	{  -1,   -1, 0x50, 0x50, 0x50}},
	{"sidl1",	{  -1,   -1, 0x51,   -1, 0x51}},
	{"sodl0",	{  -1,   -1, 0x54, 0x54, 0x54}},
	{"sodl1",	{  -1,   -1, 0x55,   -1, 0x55}},
	{"sbdl0",	{  -1,   -1, 0x58, 0x58, 0x58}},
	{"sbdl1",	{  -1,   -1, 0x59,   -1, 0x59}},
	{"scratchb0",	{0x3c,   -1, 0x5c, 0x5c, 0x5c}},
	{"scratchb1",	{0x3d,   -1, 0x5d, 0x5d, 0x5d}},
	{"scratchb2",	{0x3e,   -1, 0x5e, 0x5e, 0x5e}},
	{"scratchb3",	{0x3f,   -1, 0x5f, 0x5f, 0x5f}},
	{"scratchc0",	{  -1,   -1,   -1,   -1, 0x60}},
	{"scratchc1",	{  -1,   -1,   -1,   -1, 0x61}},
	{"scratchc2",	{  -1,   -1,   -1,   -1, 0x62}},
	{"scratchc3",	{  -1,   -1,   -1,   -1, 0x63}},
	{"scratchd0",	{  -1,   -1,   -1,   -1, 0x64}},
	{"scratchd1",	{  -1,   -1,   -1,   -1, 0x65}},
	{"scratchd2",	{  -1,   -1,   -1,   -1, 0x66}},
	{"scratchd3",	{  -1,   -1,   -1,   -1, 0x67}},
	{"scratche0",	{  -1,   -1,   -1,   -1, 0x68}},
	{"scratche1",	{  -1,   -1,   -1,   -1, 0x69}},
	{"scratche2",	{  -1,   -1,   -1,   -1, 0x6a}},
	{"scratche3",	{  -1,   -1,   -1,   -1, 0x6b}},
	{"scratchf0",	{  -1,   -1,   -1,   -1, 0x6c}},
	{"scratchf1",	{  -1,   -1,   -1,   -1, 0x6d}},
	{"scratchf2",	{  -1,   -1,   -1,   -1, 0x6e}},
	{"scratchf3",	{  -1,   -1,   -1,   -1, 0x6f}},
	{"scratchg0",	{  -1,   -1,   -1,   -1, 0x70}},
	{"scratchg1",	{  -1,   -1,   -1,   -1, 0x71}},
	{"scratchg2",	{  -1,   -1,   -1,   -1, 0x72}},
	{"scratchg3",	{  -1,   -1,   -1,   -1, 0x73}},
	{"scratchh0",	{  -1,   -1,   -1,   -1, 0x74}},
	{"scratchh1",	{  -1,   -1,   -1,   -1, 0x75}},
	{"scratchh2",	{  -1,   -1,   -1,   -1, 0x7e}},
	{"scratchh3",	{  -1,   -1,   -1,   -1, 0x77}},
	{"scratchi0",	{  -1,   -1,   -1,   -1, 0x78}},
	{"scratchi1",	{  -1,   -1,   -1,   -1, 0x79}},
	{"scratchi2",	{  -1,   -1,   -1,   -1, 0x7a}},
	{"scratchi3",	{  -1,   -1,   -1,   -1, 0x7b}},
	{"scratchj0",	{  -1,   -1,   -1,   -1, 0x7c}},
	{"scratchj1",	{  -1,   -1,   -1,   -1, 0x7d}},
	{"scratchj2",	{  -1,   -1,   -1,   -1, 0x7e}},
	{"scratchj3",	{  -1,   -1,   -1,   -1, 0x7f}},
};

int	lineno;
int	err_listed;
int	arch;
int	partial_flag;

char	inbuf[128];

char	*sourcefile;
char	*outputfile;
char	*listfile;
char	*errorfile;

FILE	*infp;
FILE	*outfp;
FILE	*listfp;
FILE	*errfp;

void	setarch(char *);
void	parse (void);
void	process (void);
void	emit_symbols (void);
void	list_symbols (void);
void	errout (char *);
void	define_symbol (char *, u_int32_t, short, short);
void	patch_label (void);
void	close_script (void);
void	new_script (char *);
void	store_inst (void);
int	expression (int *);
int	evaluate (int);
int	number (char *);
int	lookup (char *);
int	reserved (char *, int);
int	CheckPhase (int);
int	CheckRegister (int);
void	transfer (int, int);
void	select_reselect (int);
void	set_clear (u_int32_t);
void	block_move (void);
void	register_write (void);
void	memory_to_memory (void);
void	loadstore (int);
void	error_line(void);
char	*makefn(char *, char *);
void	usage(void);

int
main (int argc, char *argv[])
{
	int	i;
	struct patchlist *p;

	if (argc < 2 || argv[1][0] == '-')
		usage();
	sourcefile = argv[1];
	infp = fopen (sourcefile, "r");
	if (infp == NULL) {
		perror ("open source");
		fprintf (stderr, "scc: error opening source file %s\n", argv[1]);
		exit (1);
	}
	/*
	 * process options
	 * -l [listfile]
	 * -o [outputfile]
	 * -p [outputfile]
	 * -z [debugfile]
	 * -e [errorfile]
	 * -a arch
	 * -v
	 * -u
	 */
	for (i = 2; i < argc; ++i) {
		if (argv[i][0] != '-')
			usage();
		switch (argv[i][1]) {
		case 'o':
		case 'p':
			partial_flag = argv[i][1] == 'p';
			if (i + 1 >= argc || argv[i + 1][0] == '-')
				outputfile = makefn (sourcefile, "out");
			else {
				outputfile = argv[i + 1];
				++i;
			}
			break;
		case 'l':
			if (i + 1 >= argc || argv[i + 1][0] == '-')
				listfile = makefn (sourcefile, "lis");
			else {
				listfile = argv[i + 1];
				++i;
			}
			break;
		case 'e':
			if (i + 1 >= argc || argv[i + 1][0] == '-')
				errorfile = makefn (sourcefile, "err");
			else {
				errorfile = argv[i + 1];
				++i;
			}
			break;
		case 'a':
			if (i + 1 == argc)
				usage();
			setarch(argv[i +1]);
			if (arch == 0) {
				fprintf(stderr,"%s: bad arch '%s'\n",
					argv[0], argv[i +1]);
				exit(1);
			}
			++i;
			break;
		default:
			fprintf (stderr, "scc: unrecognized option '%c'\n",
			    argv[i][1]);
			usage();
		}
	}
	if (outputfile)
		outfp = fopen (outputfile, "w");
	if (listfile)
		listfp = fopen (listfile, "w");
	if (errorfile)
		errfp = fopen (errorfile, "w");
	else
		errfp = stderr;

	if (outfp) {
		time_t cur_time;

		fprintf(outfp, "/*\t$OpenBSD: ncr53cxxx.c,v 1.8 2023/01/04 10:05:44 jsg Exp $\t*/\n");
		fprintf(outfp, "/*\n");
		fprintf(outfp, " *\tDO NOT EDIT - this file is automatically generated.\n");
		time(&cur_time);
		fprintf(outfp, " *\tcreated from %s on %s", sourcefile, ctime(&cur_time));
		fprintf(outfp, " */\n");
	}

	while (fgets (inbuf, sizeof (inbuf), infp)) {
		++lineno;
		if (listfp)
			fprintf (listfp, "%3d:  %s", lineno, inbuf);
		err_listed = 0;
		parse ();
		if (ntokens) {
#ifdef DUMP_TOKENS
			int	i;

			fprintf (listfp, "      %d tokens\n", ntokens);
			for (i = 0; i < ntokens; ++i) {
				fprintf (listfp, "      %d: ", i);
				if (tokens[i].type)
					fprintf (listfp,"'%c'\n", tokens[i].type);
				else
					fprintf (listfp, "%s\n", tokens[i].name);
			}
#endif
			if (ntokens >= 2 && tokens[0].type == 0 &&
			    tokens[1].type == ':') {
			    	define_symbol (tokens[0].name, dsps, S_LABEL, F_DEFINED);
				tokenix += 2;
			}
			if (tokenix < ntokens)
				process ();
		}

	}
	close_script ();
	emit_symbols ();
	if (outfp && !partial_flag) {
		fprintf (outfp, "\nu_int32_t INSTRUCTIONS = 0x%08x;\n", ninsts);
		fprintf (outfp, "u_int32_t PATCHES = 0x%08x;\n", npatches);
		fprintf (outfp, "u_int32_t LABELPATCHES[] = {\n");
		p = patches;
		while (p) {
			fprintf (outfp, "\t0x%08x,\n", p->offset / 4);
			p = p->next;
		}
		fprintf (outfp, "};\n\n");
	}
	list_symbols ();
	exit(0);
}

void setarch(char *val)
{
	switch (atoi(val)) {
	case 700:
		arch = ARCH700;
		break;
	case 710:
		arch = ARCH710;
		break;
	case 720:
		arch = ARCH720;
		break;
	case 810:
		arch = ARCH810;
		break;
	case 825:
		arch = ARCH825;
		break;
	default:
		arch = 0;
	}
}

void emit_symbols ()
{
	int	i;
	struct	patchlist *p;

	if (nsymbols == 0 || outfp == NULL)
		return;

	for (i = 0; i < nsymbols; ++i) {
		char	*code;
		if ((symbols[i].flags & F_DEFINED) == 0 &&
		    symbols[i].type != S_EXTERNAL) {
			fprintf(stderr, "warning: symbol %s undefined\n",
			    symbols[i].name);
		}
		if (symbols[i].type == S_ABSOLUTE)
			code = "A_";
		else if (symbols[i].type == S_RELATIVE)
			code = "R_";
		else if (symbols[i].type == S_EXTERNAL)
			code = "E_";
		else if (symbols[i].flags & F_ENTRY)
			code = "Ent_";
		else
			continue;
		fprintf (outfp, "#define\t%s%s\t0x%08x\n", code, symbols[i].name,
			symbols[i].value);
		if (symbols[i].flags & F_ENTRY || symbols[i].patchlist == NULL)
			continue;
		fprintf (outfp, "u_int32_t %s%s_Used[] = {\n", code, symbols[i].name);
#if 1
		p = symbols[i].patchlist;
		while (p) {
			fprintf (outfp, "\t0x%08x,\n", p->offset / 4);
			p = p->next;
		}
#endif
		fprintf (outfp, "};\n\n");
	}
	/* patches ? */
}

void list_symbols ()
{
	int	i;

	if (nsymbols == 0 || listfp == NULL)
		return;
	fprintf (listfp, "\n\nValue     Type     Symbol\n");
	for (i = 0; i < nsymbols; ++i) {
		fprintf (listfp, "%08x: %-8s %s\n", symbols[i].value,
			stypes[symbols[i].type], symbols[i].name);
	}
}

void errout (char *text)
{
	error_line();
	fprintf (errfp, "*** %s ***\n", text);
}

void parse ()
{
	char *p = inbuf;
	char c;
	char string[64];
	char *s;
	size_t len;

	ntokens = tokenix = 0;
	while (1) {
		while ((c = *p++) && c != '\n' && (c <= ' ' || c == '\t'))
			;
		if (c == '\n' || c == 0 || c == ';')
			break;
		if (ntokens >= MAXTOKENS) {
			errout ("Token table full");
			break;
		}
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') || c == '$' || c == '_') {
		    	s = string;
		    	*s++ = c;
		    	while (((c = *p) >= '0' && c <= '9') ||
		    	    (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    	    c == '_' || c == '$') {
		    	    	*s++ = *p++;
		    	}
		    	*s = 0;
			len = strlen (string) + 1;
		    	tokens[ntokens].name = malloc (len);
		    	strlcpy (tokens[ntokens].name, string, len);
		    	tokens[ntokens].type = 0;
		}
		else {
			tokens[ntokens].type = c;
		}
		++ntokens;
	}
	return;
}

void	process ()
{
	int	i;

	if (tokens[tokenix].type) {
		error_line();
		fprintf (errfp, "Error: expected directive, found '%c'\n",
			tokens[tokenix].type);
		return;
	}
	for (i = 0; directives[i].name; ++i) {
		if (strcmpi (directives[i].name, tokens[tokenix].name) == 0)
			break;
	}
	if (directives[i].name == NULL) {
		error_line();
		fprintf (errfp, "Error: expected directive, found \"%s\"\n",
			tokens[tokenix].name);
		return;
	}
	if (directives[i].func == NULL) {
		error_line();
		fprintf (errfp, "No function for directive \"%s\"\n", tokens[tokenix].name);
	} else {
#if 0
		fprintf (listfp, "Processing directive \"%s\"\n", directives[i].name);
#endif
		++tokenix;
		(*directives[i].func) ();
	}
}

void define_symbol (char *name, u_int32_t value, short type, short flags)
{
	int	i;
	struct patchlist *p;
	size_t	len;

	for (i = 0; i < nsymbols; ++i) {
		if (symbols[i].type == type && strcmp (symbols[i].name, name) == 0) {
			if (symbols[i].flags & F_DEFINED) {
				error_line();
				fprintf (errfp, "*** Symbol \"%s\" multiply defined\n",
					name);
			} else {
				symbols[i].flags |= flags;
				symbols[i].value = value;
				p = symbols[i].patchlist;
				while (p) {
					if (p->offset > dsps)
						errout ("Whoops\007");
					else
						script[p->offset / 4] += dsps;
					p = p->next;
				}
			}
			return;
		}
	}
	if (nsymbols >= MAXSYMBOLS) {
		errout ("Symbol table full");
		return;
	}
	symbols[nsymbols].type = type;
	symbols[nsymbols].flags = flags;
	symbols[nsymbols].value = value;
	symbols[nsymbols].patchlist = NULL;
	len = strlen (name) + 1;
	symbols[nsymbols].name = malloc (len);
	strlcpy (symbols[nsymbols].name, name, len);
	++nsymbols;
}

void patch_label (void)
{
	struct patchlist *p, **h;

	h = &patches;
	while(*h)
		h = &(*h)->next;
	p = (struct patchlist *) malloc (sizeof (struct patchlist));
	*h = p;
	p->next = NULL;
	p->offset = dsps + 4;
	npatches++;
}

void close_script ()
{
	int	i;

	if (dsps == 0)
		return;
	if (outfp) {
		fprintf (outfp, "const u_int32_t %s[] = {\n", script_name);
		for (i = 0; i < dsps / 4; i += 2) {
			fprintf (outfp, "\t0x%08x, 0x%08x", script[i],
				script[i + 1]);
			/* check for memory move instruction */
			if ((script[i] & 0xe0000000) == 0xc0000000)
				fprintf (outfp, ", 0x%08x,", script[i + 2]);
			else
				if ((i + 2) <= dsps / 4) fprintf (outfp, ",\t\t");
			fprintf (outfp, "\t/* %03x - %3d */\n", i * 4, i * 4);
			if ((script[i] & 0xe0000000) == 0xc0000000)
				++i;
		}
		fprintf (outfp, "};\n\n");
	}
	dsps = 0;
}

void new_script (char *name)
{
	size_t len = strlen (name) + 1;

	close_script ();
	script_name = malloc (len);
	strlcpy (script_name, name, len);
}

int	reserved (char *string, int t)
{
	if (tokens[t].type == 0 && strcmpi (tokens[t].name, string) == 0)
		return (1);
	return (0);
}

int	CheckPhase (int t)
{
	int	i;

	for (i = 0; i < 8; ++i) {
		if (reserved (phases[i], t)) {
			inst0 |= i << 24;
			return (1);
		}
	}
	return (0);
}

int	CheckRegister (int t)
{
	int	i;

	if (arch <= 0) {
		errout("'ARCH' statement missing");
		return -1;
	}
	for (i = 0; i < (sizeof(regs) / sizeof(regs[0])); i++) {
		if (regs[i].addr[arch - 1] >= 0 && reserved(regs[i].name, t))
			return regs[i].addr[arch-1];
	}
	return (-1);
}

int	expression (int *t)
{
	int	value;
	int	i = *t;

	value = evaluate (i++);
	while (i < ntokens) {
		if (tokens[i].type == '+')
			value += evaluate (i + 1);
		else if (tokens[i].type == '-')
			value -= evaluate (i + 1);
		else
			errout ("Unknown identifier");
		i += 2;
	}
	*t = i;
	return (value);
}

int	evaluate (t)
{
	int	value;
	char	*name;

	if (tokens[t].type) {
		errout ("Expected an identifier");
		return (0);
	}
	name = tokens[t].name;
	if (*name >= '0' && *name <= '9')
		value = number (name);
	else
		value = lookup (name);
	return (value);
}

int	number (char *s)
{
	int	value;
	int	n;
	int	radix;

	radix = 10;
	if (*s == '0') {
		++s;
		radix = 8;
		switch (*s) {
		case 'x':
		case 'X':
			radix = 16;
			break;
		case 'b':
		case 'B':
			radix = 2;
		}
		if (radix != 8)
			++s;
	}
	value = 0;
	while (*s) {
		n = *s++;
		if (n >= '0' && n <= '9')
			n -= '0';
		else if (n >= 'a' && n <= 'f')
			n -= 'a' - 10;
		else if (n >= 'A' && n <= 'F')
			n -= 'A' - 10;
		else {
			error_line();
			fprintf (errfp, "*** Expected digit\n");
			n = 0;
		}
		if (n >= radix)
			errout ("Expected digit");
		else
			value = value * radix + n;
	}
	return (value);
}

int	lookup (char *name)
{
	int	i;
	struct patchlist *p;
	size_t	len;

	for (i = 0; i < nsymbols; ++i) {
		if (strcmp (name, symbols[i].name) == 0) {
			if ((symbols[i].flags & F_DEFINED) == 0) {
				p = (struct patchlist *) &symbols[i].patchlist;
				while (p->next)
					p = p->next;
				p->next = (struct patchlist *) malloc (sizeof (struct patchlist));
				p = p->next;
				p->next = NULL;
				p->offset = dsps + 4;
			}
			return ((int) symbols[i].value);
		}
	}
	if (nsymbols >= MAXSYMBOLS) {
		errout ("Symbol table full");
		return (0);
	}
	symbols[nsymbols].type = S_LABEL;	/* assume forward reference */
	symbols[nsymbols].flags = 0;
	symbols[nsymbols].value = 0;
	p = (struct patchlist *) malloc (sizeof (struct patchlist));
	symbols[nsymbols].patchlist = p;
	p->next = NULL;
	p->offset = dsps + 4;
	len = strlen (name) + 1;
	symbols[nsymbols].name = malloc (len);
	strlcpy (symbols[nsymbols].name, name, len);
	++nsymbols;
	return (0);
}

void	f_arch (void)
{
	int i, archsave;

	i = tokenix;

	archsave = arch;
	setarch(tokens[i].name);
	if( arch == 0) {
		errout("Unrecognized ARCH");
		arch = archsave;
	}
}

void	f_proc (void)
{
	if (tokens[tokenix].type != 0 || tokens[tokenix + 1].type != ':')
		errout ("Invalid PROC statement");
	else
		new_script (tokens[tokenix].name);
}

void	f_pass (void)
{
	errout ("PASS option not implemented");
}

/*
 *	f_list:  process list of symbols for the ENTRY and EXTERNAL directive
 */

void	f_list (void)
{
	int	i;
	short	type;
	short	flags;

	type = strcmpi (tokens[tokenix-1].name, "ENTRY") ? S_EXTERNAL : S_LABEL;
	flags = type == S_LABEL ? F_ENTRY : 0;
	for (i = tokenix; i < ntokens; ++i) {
		if (tokens[i].type != 0) {
			errout ("Expected an identifier");
			return;
		}
		define_symbol (tokens[i].name, 0, type, flags);
		if (i + 1 < ntokens) {
			if (tokens[++i].type == ',')
				continue;
			errout ("Expected a separator");
			return;
		}
	}
}

/*
 *	f_define:	process list of definitions for ABSOLUTE and RELATIVE directive
 */

void	f_define (void)
{
	int	i;
	char	*name;
	u_int32_t value;
	int	type;

	type = strcmpi (tokens[tokenix-1].name, "ABSOLUTE") ? S_RELATIVE : S_ABSOLUTE;
	i = tokenix;
	while (i < ntokens) {
		if (tokens[i].type) {
			errout ("Expected an identifier");
			return;
		}
		if (tokens[i + 1].type != '=') {
			errout ("Expected a separator");
			return;
		}
		name = tokens[i].name;
		i += 2;
		value = expression (&i);
		define_symbol (name, value, type, F_DEFINED);
	}
}

void	store_inst ()
{
	int	i = dsps / 4;
	int	l = 8;

	if ((inst0 & 0xe0000000) == 0xc0000000)
		l = 12;			/* Memory to memory move is 12 bytes */
	if ((dsps + l) / 4 > MAXINST) {
		errout ("Instruction table overflow");
		return;
	}
	script[i++] = inst0;
	script[i++] = inst1;
	if (l == 12)
		script[i++] = inst2;
	if (listfp) {
		fprintf (listfp, "\t%04x: %08x %08x", dsps, inst0, inst1);
		if (l == 12)
			fprintf (listfp, " %08x", inst2);
		fprintf (listfp, "\n");
	}
	dsps += l;
	inst0 = inst1 = inst2 = 0;
	++ninsts;
}

void	f_move (void)
{
	if (reserved ("memory", tokenix))
		memory_to_memory ();
	else if (reserved ("from", tokenix) || tokens[tokenix+1].type == ',')
		block_move ();
	else
		register_write ();
	store_inst ();
}

void	f_jump (void)
{
	transfer (0x80000000, 0);
}

void	f_call (void)
{
	transfer (0x88000000, 0);
}

void	f_return (void)
{
	transfer (0x90000000, 1);
}

void	f_int (void)
{
	transfer (0x98000000, 2);
}

void	f_intfly (void)
{
	transfer (0x98100000, 2);
}

void	f_select (void)
{
	int	t = tokenix;

	if (reserved ("atn", t)) {
		inst0 = 0x01000000;
		++t;
	}
	select_reselect (t);
}

void	f_reselect (void)
{
	select_reselect (tokenix);
}

void	f_wait (void)
{
	int	i = tokenix;

	inst1 = 0;
	if (reserved ("disconnect", i)) {
		inst0 = 0x48000000;
	}
	else {
		if (reserved ("reselect", i))
			inst0 = 0x50000000;
		else if (reserved ("select", i))
			inst0 = 0x50000000;
		else
			errout ("Expected SELECT or RESELECT");
		++i;
		if (reserved ("rel", i)) {
#if 0 /* driver will fix relative dsps to absolute */
			if (arch < ARCH710) {
				errout ("Wrong arch for relative dsps");
			}
#endif
			i += 2;
			inst1 = evaluate (i) - dsps - 8;
			inst0 |= 0x04000000;
		}
		else {
			inst1 = evaluate (i);
			patch_label();
		}
	}
	store_inst ();
}

void	f_disconnect (void)
{
	inst0 = 0x48000000;
	store_inst ();
}

void	f_set (void)
{
	set_clear (0x58000000);
}

void	f_clear (void)
{
	set_clear (0x60000000);
}

void	f_load (void)
{
	inst0 = 0xe1000000;
	if (arch < ARCH810) {
		errout ("Wrong arch for load/store");
		return;
	}
	loadstore(tokenix);
}

void	f_store (void)
{
	int i;
	inst0 = 0xe0000000;
	if (arch < ARCH810) {
		errout ("Wrong arch for load/store");
		return;
	}
	i = tokenix;
	if (reserved("noflush", i)) {
		inst0 |= 0x2000000;
		i++;
	}
	loadstore(i);
}

void	f_nop (void)
{
	inst0 = 0x80000000;
	inst1 = 0x00000000;
	store_inst ();
}

void loadstore(int i)
{
	int reg, size;

	reg = CheckRegister(i);
	if (reg < 0)	
		errout ("Expected register");
	else
		inst0 |= reg <<  16;
	if (reg == 8)
		errout ("Register can't be SFBR");
	i++;
	if (tokens[i].type == ',')
		i++;
	else
		errout ("expected ','");
	size = evaluate(i);
	if (i < 1 || i > 4)
		errout("wrong size");
	if ((reg & 0x3) + size > 4)
		errout("size too big for register");
	inst0 |= size;
	i++;
	if (tokens[i].type == ',')
		i++;
	else
		errout ("expected ','");
	if (reserved("from", i) || reserved("dsarel", i)) {
		if (arch < ARCH710) {
			errout ("Wrong arch for table indirect");
			return;
		}
		i++;
		inst0 |= 0x10000000;
	}
	inst1 = evaluate(i);
	store_inst ();
}

void	transfer (int word0, int type)
{
	int	i;

	i = tokenix;
	inst0 = word0;
	if (type == 0 && reserved ("rel", i)) {
#if 0 /* driver will fix relative dsps to absolute */
		if (arch < ARCH710) {
			errout ("Wrong arch for relative dsps");
		}
#endif
		inst1 = evaluate (i + 2) - dsps - 8;
		i += 4;
		inst0 |= 0x00800000;
	}
	else if (type != 1) {
		inst1 = evaluate (i);
		++i;
		if (type == 0)
			patch_label();
	}
	if (i >= ntokens) {
		inst0 |= 0x00080000;
		store_inst ();
		return;
	}
	if (tokens[i].type != ',')
		errout ("Expected a separator, ',' assumed");
	else
		++i;
	if (reserved("when", i))
		inst0 |= 0x00010000;
	else if (reserved ("if", i) == 0) {
		errout ("Expected a reserved word");
		store_inst ();
		return;
	}
	i++;
	if (reserved("false", i)) {
		store_inst ();
		return;
	}
	if (reserved ("not", i))
		++i;
	else
		inst0 |= 0x00080000;
	if (reserved ("atn", i)) {
		inst0 |= 0x00020000;
		++i;
	} else if (CheckPhase (i)) {
		inst0 |= 0x00020000;
		++i;
	}
	if (i < ntokens && tokens[i].type != ',') {
		if (inst0 & 0x00020000) {
			if (inst0 & 0x00080000 && reserved ("and", i)) {
				++i;
			}
			else if ((inst0 & 0x00080000) == 0 && reserved ("or", i)) {
				++i;
			}
			else
				errout ("Expected a reserved word");
		}
		inst0 |= 0x00040000 + (evaluate (i++) & 0xff);
	}
	if (i < ntokens) {
		if (tokens[i].type == ',')
			++i;
		else
			errout ("Expected a separator, ',' assumed");
		if (reserved ("and", i) && reserved ("mask", i + 1))
			inst0 |= ((evaluate (i + 2) & 0xff) << 8);
		else
			errout ("Expected , AND MASK");
	}
	store_inst ();
}

void 	select_reselect (int t)
{
	inst0 |= 0x40000000;		/* ATN may be set from SELECT */
	if (reserved ("from", t)) {
		if (arch < ARCH710) {
			errout ("Wrong arch for table indirect");
			return;
		}
		++t;
		inst0 |= 0x02000000 | evaluate (t++);
	}
	else
		inst0 |= (evaluate (t++) & 0xff) << 16;
	if (tokens[t++].type == ',') {
		if (reserved ("rel", t)) {
#if 0 /* driver will fix relative dsps to absolute */
			if (arch < ARCH710) {
				errout ("Wrong arch for relative dsps");
			}
#endif
			inst0 |= 0x04000000;
			inst1 = evaluate (t + 2) - dsps - 8;
		}
		else {
			inst1 = evaluate (t);
			patch_label();
		}
	}
	else
		errout ("Expected separator");
	store_inst ();
}

void	set_clear (u_int32_t code)
{
	int	i = tokenix;
	short	need_and = 0;

	inst0 = code;
	while (i < ntokens) {
		if (need_and) {
			if (reserved ("and", i))
				++i;
			else
				errout ("Expected AND");
		}
		if (reserved ("atn", i)) {
			inst0 |= 0x0008;
			++i;
		}
		else if (reserved ("ack", i)) {
			inst0 |= 0x0040;
			++i;
		}
		else if (reserved ("target", i)) {
			inst0 |= 0x0200;
			++i;
		}
		else if (reserved ("carry", i)) {
			inst0 |= 0x0400;
			++i;
		}
		else
			errout ("Expected ATN, ACK, TARGET or CARRY");
		need_and = 1;
	}
	store_inst ();
}

void	block_move ()
{
	if (reserved ("from", tokenix)) {
		if (arch < ARCH710) {
			errout ("Wrong arch for table indirect");
			return;
		}
		inst1 = evaluate (tokenix+1);
		inst0 |= 0x10000000 | inst1;	/*** ??? to match Zeus script */
		tokenix += 2;
	}
	else {
		inst0 |= evaluate (tokenix++);	/* count */
		tokenix++;			/* skip ',' */
		if (reserved ("ptr", tokenix)) {
			++tokenix;
			inst0 |= 0x20000000;
		}
		inst1 = evaluate (tokenix++);	/* address */
	}
	if (tokens[tokenix].type != ',')
		errout ("Expected separator");
	if (reserved ("when", tokenix + 1)) {
		inst0 |= 0x08000000;
		CheckPhase (tokenix + 2);
	}
	else if (reserved ("with", tokenix + 1)) {
		CheckPhase (tokenix + 2);
	}
	else
		errout ("Expected WITH or WHEN");
}

void	register_write ()
{
	/*
	 * MOVE reg/data8 TO reg			register write
	 * MOVE reg <op> data8 TO reg			register write
	 * MOVE reg + data8 TO reg WITH CARRY		register write
	 */
	int	op;
	int	reg;
	int	data;

	if (reserved ("to", tokenix+1))
		op = 0;
	else if (reserved ("shl", tokenix+1))
		op = 1;
	else if (reserved ("shr", tokenix+1))
		op = 5;
	else if (tokens[tokenix+1].type == '|')
		op = 2;
	else if (reserved ("xor", tokenix+1))
		op = 3;
	else if (tokens[tokenix+1].type == '&')
		op = 4;
	else if (tokens[tokenix+1].type == '+')
		op = 6;
	else if (tokens[tokenix+1].type == '-')
		op = 8;
	else
		errout ("Unknown register operator");
	switch (op) {
	case 2:
	case 3:
	case 4:
	case 6:
	case 8:
		if (reserved ("to", tokenix+3) == 0)
			errout ("Register command expected TO");
	}
	reg = CheckRegister (tokenix);
	if (reg < 0) {			/* Not register, must be data */
		data = evaluate (tokenix);
		if (op)
			errout ("Register operator not move");
		reg = CheckRegister (tokenix+2);
		if (reg < 0)
			errout ("Expected register");
		inst0 = 0x78000000 | (data << 8) | reg << 16;
#if 0
fprintf (listfp, "Move data to register: %02x %d\n", data, reg);
#endif
	} else if (op) {
		switch (op) {
		case 2:
		case 3:
		case 4:
		case 6:
		case 8:
			inst0 = 0;
			/* A register read/write operator */
			if (reserved("sfbr", tokenix+2)) {
				if (arch < ARCH825)
					errout("wrong arch for add with SFBR");
				if (op == 8)
					errout("can't substract SFBR");
				inst0 |= 0x00800000;
				data = 0;
			} else
				data = evaluate (tokenix+2);
			if (tokenix+5 < ntokens) {
				if (!reserved("with", tokenix+5) ||
				    !reserved("carry", tokenix+6)) {
					errout("Expected 'WITH CARRY'");
				} else if (op != 6) {
					errout("'WITH CARRY' only valide "
					    "with '+'");
				}
				op = 7;
			}
			if (op == 8) {
				data = -data;
				op = 6;
			}
			inst0 |= (data & 0xff) << 8;
			data = CheckRegister (tokenix+4);
			break;
		default:
			data = CheckRegister (tokenix+2);
			break;
		}
		if (data < 0)
			errout ("Expected register");
		if (reg != data && reg != 8 && data != 8)
			errout ("One register MUST be SBFR");
		if (reg == data) {	/* A register read/modify/write */
#if 0
fprintf (listfp, "Read/modify register: %02x %d %d\n", inst0 >> 8, op, reg);
#endif
			inst0 |= 0x78000000 | (op << 24) | (reg << 16);
		}
		else {			/* A move to/from SFBR */
			if (reg == 8) {	/* MOVE SFBR <> TO reg */
#if 0
fprintf (listfp, "Move SFBR to register: %02x %d %d\n", inst0 >> 8, op, data);
#endif
				inst0 |= 0x68000000 | (op << 24) | (data << 16);
			}
			else {
#if 0
fprintf (listfp, "Move register to SFBR: %02x %d %d\n", inst0 >> 8, op, reg);
#endif
				inst0 |= 0x70000000 | (op << 24) | (reg << 16);
			}
		}
	} else {				/* register to register */
		data = CheckRegister (tokenix+2);
		if (data < 0)
			errout ("Expected register");
		if (reg == 8)		/* move SFBR to reg */
			inst0 = 0x6a000000 | (data << 16);
		else if (data == 8)	/* move reg to SFBR */
			inst0 = 0x72000000 | (reg << 16);
		else
			errout ("One register must be SFBR");
	}
}

void	memory_to_memory ()
{
	inst0 = 0xc0000000 + evaluate (tokenix+1);
	inst1 = evaluate (tokenix+3);
	/*
	 * need to hack dsps, otherwise patch offset will be wrong for
	 * second pointer
	 */
	dsps += 4;
	inst2 = evaluate (tokenix+5);
	dsps -= 4;
}

void	error_line()
{
	if (errfp != listfp && errfp && err_listed == 0) {
		fprintf (errfp, "%3d:  %s", lineno, inbuf);
		err_listed = 1;
	}
}

char *	makefn (base, sub)
	char *base;
	char *sub;
{
	char *fn;
	size_t len = strlen (base) + strlen (sub) + 2; 

	fn = malloc (len);
	strlcpy (fn, base, len);
	base = strrchr(fn, '.');
	if (base)
		*base = 0;
	strlcat (fn, ".", len);
	strlcat (fn, sub, len);
	return (fn);
}

void	usage()
{
	fprintf (stderr, "usage: scc sourcfile [options]\n");
	exit(1);
}
