/*	$OpenBSD: samachdep.h,v 1.7 2023/02/15 12:43:32 aoyama Exp $	*/
/*	$NetBSD: samachdep.h,v 1.10 2013/03/05 15:34:53 tsutsui Exp $	*/

/*
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)samachdep.h	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <lib/libsa/stand.h>

#define MHZ_25		25
#define MHZ_33		33

struct consdev;
typedef struct label_t {
	long val[19];
} label_t;

/* autoconf.c */
void configure(void);
void find_devs(void);

/* bmc.c */
void bmccnprobe(struct consdev *);
void bmccninit(struct consdev *);
int  bmccngetc(dev_t);
void bmccnputc(dev_t, int);

/* bmd.c */
void bmdinit(void);
int bmdputc(int);
void bmdadjust(short, short);
void bmdclear(void);

/* boot.c */
extern uint32_t bootdev;

/* conf.c */
extern	struct fs_ops file_system_disk[];
extern	int nfsys_disk;
extern	struct fs_ops file_system_nfs[];

/* exec.c */
void run_loadfile(uint64_t *, int);

/* fault.c */
int badaddr(void *, int);

/* font.c */
extern const u_short bmdfont[][20];

/* init_main.c */
extern int cpuspeed;
extern int nplane;
extern int machtype;
extern char default_file[];
extern char fuse_rom_data[];

/* kbd.c */
int kbd_decode(u_char);

/* lance.c */
void *lance_attach(uint, void *, void *, uint8_t *);
void *lance_cookie(uint);
uint8_t *lance_eaddr(void *);
int lance_init(void *);
int lance_get(void *, void *, size_t);
int lance_put(void *, void *, size_t);
int lance_end(void *);

/* locore.S */
extern uint16_t dipswitch;
extern volatile uint32_t tick;
int setjmp(label_t *);
void delay(int);

/* logo.c */
void draw_logo(void);

/* sc.c */
struct scsi_softc;
int scinit(struct scsi_softc *, uint);
struct scsi_generic_cdb;
int scsi_immed_command(struct scsi_softc *, int, int, struct scsi_generic_cdb *,
    u_char *, unsigned int);
int scsi_request_sense(struct scsi_softc *, int, int, u_char *, unsigned int);
int scsi_test_unit_rdy(struct scsi_softc *, int, int);

/* sd.c */
int sdstrategy(void *, int, daddr_t, size_t, void *, size_t *);
int sdopen(struct open_file *, ...);
int sdclose(struct open_file *);

/* sio.c */
int  siointr(int);
void siocnprobe(struct consdev *);
void siocninit(struct consdev *);
int  siocngetc(dev_t);
void siocnputc(dev_t, int);
void sioinit(void);

/* ufs_disklabel.c */
char *readdisklabel(struct scsi_softc *, uint, struct disklabel *);

#define DELAY(n)	delay(n)
