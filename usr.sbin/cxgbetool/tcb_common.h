/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Chelsio Communications, Inc.
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
 * $FreeBSD$
 *
 */

#ifndef	__tcb_common_h
#define	__tcb_common_h

/* ANSI C standard includes */
#include        <assert.h>
#include        <stdlib.h>
#include        <string.h>
#include        <ctype.h>
#include        <stdio.h>
#include        <stdarg.h>


#ifndef FALSE
#define FALSE 0
#endif

#ifndef EOS
#define EOS  '\0'
#endif

#ifndef __variable_sizes

/* windows has _UI64_MAX.  C99 has ULLONG_MAX, but I don't compile
with C99 for portability with windows, so the ui64 is a guess.
I'll add an assert to cl_main to confirm these sizes are accurate.
*/
#ifdef _UI64_MAX   /* windows */
#if   (_UI64_MAX ==  0xFFFFFFFFFFFFFFFF)
typedef __int64                      si64;
typedef unsigned __int64             ui64;
#endif
#else  /*else of #ifdef _UI64_MAX */
typedef long long int                si64;
typedef unsigned long long int       ui64;
#endif /*endif of #ifdef _UI64_MAX */
#endif /* endif of #ifndef __variable_sizes */




typedef struct tcb_var {
  char *name;
  int   aux;
  int   lo;
  int   hi;

  char *faka;
  int  flo;
  int  fhi;

  char *aka;

  int   comp;

  char *desc;
  char *akadesc;
  
  ui64 rawval;  
  unsigned val;

} _TCBVAR;


enum comp_types {

  COMP_NONE=0,
  COMP_ULP,
  COMP_TX_MAX,
  COMP_RCV_NXT,
  COMP_PTR,
  COMP_LEN,

};


enum tidtypes {
  TIDTYPE_TCB=0,
  TIDTYPE_SCB=1,
  TIDTYPE_FCB=2,

};


enum prntstyls {
  PRNTSTYL_VERBOSE=0,
  PRNTSTYL_LIST=1,
  PRNTSTYL_COMP=2,
  PRNTSTYL_RAW=3,

};


/* from tp/src/tp.h */
#define PM_MODE_PASS  0
#define PM_MODE_DDP   1
#define PM_MODE_ISCSI 2
#define PM_MODE_IWARP 3
#define PM_MODE_RDDP  4
#define PM_MODE_IANDP 5
#define PM_MODE_FCOE  6
#define PM_MODE_USER  7
#define PM_MODE_TLS   8
#define PM_MODE_DTLS  9



#define SEQ_ADD(a,b)    (((a)+(b)) & 0xFFFFFFFF)
#define SEQ_SUB(a,b)    (((a)-(b)) & 0xFFFFFFFF)

///* functions needed by the tcbshowtN.c code */
extern unsigned val(char *name);
extern ui64 val64(char *name);
extern void PR(char *fmt, ...);
extern char *spr_tcp_state(unsigned state);
extern char *spr_ip_version(unsigned ipver);
extern char *spr_cctrl_sel(unsigned cctrl_sel0,unsigned cctrl_sel1);
extern char *spr_ulp_type(unsigned ulp_type);


extern unsigned parse_tcb( _TCBVAR *base_tvp, unsigned char *buf);
extern void display_tcb(_TCBVAR *tvp,unsigned char *buf,int aux);
extern void parse_n_display_xcb(unsigned char *buf);

extern void swizzle_tcb(unsigned char *buf);
extern void  set_tidtype(unsigned int tidtype);
extern void set_tcb_info(unsigned int tidtype, unsigned int cardtype);
extern void set_print_style(unsigned int prntstyl);

#endif /* __tcb_common_h */
