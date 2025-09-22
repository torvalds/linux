/*	$OpenBSD: libsa.h,v 1.47 2023/02/23 19:48:22 miod Exp $	*/

/*
 * Copyright (c) 1996-1999 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <lib/libsa/stand.h>
#include <machine/biosvar.h>

#define	EXEC_ELF

struct i386_boot_probes {
	char *name;
	void (**probes)(void);
	int count;
};

extern void (*sa_cleanup)(void);

void gateA20(int);
void gateA20on(void);

void cpuprobe(void);
void smpprobe(void);
void ps2probe(void);
void pciprobe(void);
void memprobe(void);
void diskprobe(void);
void cdprobe(void);
void apmprobe(void);
void apmfixmem(void);
void dump_biosmem(bios_memmap_t *);
int mem_add(long long, long long);
int mem_delete(long long, long long);
int mem_limit(long long);
void mem_pass(void);

int pslid(void);

void devboot(dev_t, char *);
void machdep(void);

void *getSYSCONFaddr(void);
void *getEBDAaddr(void);

extern const char bdevs[][4];
extern const int nbdevs;
extern u_int cnvmem, extmem; /* XXX global pass memprobe()->machdep_start() */
extern int ps2model;

extern struct i386_boot_probes probe_list[];
extern int nibprobes;
extern void (*devboot_p)(dev_t, char *);

/* diskprobe.c */
extern bios_diskinfo_t bios_diskinfo[];
extern u_int32_t bios_cksumlen;

#define MACHINE_CMD	cmd_machine /* we have i386-specific commands */

#define CHECK_SKIP_CONF	check_skip_conf	/* we can skip boot.conf with Ctrl */
