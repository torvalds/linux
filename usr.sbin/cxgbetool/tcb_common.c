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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "tcb_common.h"

/***:-----------------------------------------------------------------------
 ***: externals
 ***:-----------------------------------------------------------------------
 */

extern _TCBVAR g_tcb_info4[];
extern _TCBVAR g_scb_info4[];
extern _TCBVAR g_fcb_info4[];
extern void t4_display_tcb_aux_0(_TCBVAR *tvp,int aux);
extern void t4_display_tcb_aux_1(_TCBVAR *tvp,int aux);
extern void t4_display_tcb_aux_2(_TCBVAR *tvp,int aux);
extern void t4_display_tcb_aux_3(_TCBVAR *tvp,int aux);

extern _TCBVAR g_tcb_info5[];
extern _TCBVAR g_scb_info5[];
extern _TCBVAR g_fcb_info5[];
extern void t5_display_tcb_aux_0(_TCBVAR *tvp,int aux);
extern void t5_display_tcb_aux_1(_TCBVAR *tvp,int aux);
extern void t5_display_tcb_aux_2(_TCBVAR *tvp,int aux);
extern void t5_display_tcb_aux_3(_TCBVAR *tvp,int aux);

extern _TCBVAR g_tcb_info6[];
extern _TCBVAR g_scb_info6[];
extern _TCBVAR g_fcb_info6[];
extern void t6_display_tcb_aux_0(_TCBVAR *tvp,int aux);
extern void t6_display_tcb_aux_1(_TCBVAR *tvp,int aux);
extern void t6_display_tcb_aux_2(_TCBVAR *tvp,int aux);
extern void t6_display_tcb_aux_3(_TCBVAR *tvp,int aux);
extern void t6_display_tcb_aux_4(_TCBVAR *tvp,int aux);

/***:-----------------------------------------------------------------------
 ***: globals
 ***:-----------------------------------------------------------------------
 */

_TCBVAR *g_tcb_info=g_tcb_info5;
_TCBVAR *g_scb_info=g_scb_info5;
_TCBVAR *g_fcb_info=g_fcb_info5;
static int g_tN=0;

static int g_prntstyl=PRNTSTYL_COMP;

static int g_got_scb=0;
static int g_got_fcb=0;


/***:-----------------------------------------------------------------------
***: error exit functions
***:-----------------------------------------------------------------------
*/

/**: err_exit functions
*:  ------------------
*/

void tcb_prflush(void)
{
    fflush(stdout);
    fflush(stderr);
}


void tcb_code_err_exit(char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  printf("Coding Error in: ");
  vprintf(fmt, args);
  printf("\n");
  tcb_prflush();
  va_end(args);
  exit(1);
}

/***:-----------------------------------------------------------------------
***: tcb_hexdump functions
***:-----------------------------------------------------------------------
*/

void
tcb_hexdump(unsigned base, unsigned char *buf, unsigned int size)
{
    unsigned offset;

    for (offset = 0; offset < size; ++offset) {
        if (!(offset % 16)) printf("\n0x%4.4x: ", base + offset);
        else if (!(offset % 8)) printf(" ");
        printf("%2.2x ", (unsigned char)buf[offset]);
    }
}

int tcb_strmatch_nc(char *cs, char *ct) {
    while (*cs)
        if (tolower(*cs++) != tolower(*ct++)) return (FALSE);
    return (!(*ct));  /*return TRUE if *ct NULL at same time as *cs==NULL*/
}


/*: -------------------------------------------------------------------------
string functions
tcb_strmatch_nc:     Similar to exact match, but case insensitive.
*/


int
tcb_strncmp_nc(char *cs, char *ct, int n)
{
    /*case insensitive version of the standard strncmp() function */
    int i = 0;
    int ret;


    ret = 0;
    for (i = 0; i < n && 0 == ret && !(EOS == *cs && EOS == *ct); ++i) {
        /* this is weird, but it matched GCC linux when strings don't
        * have any upper case characters.
        */
        ret = tolower(*cs++) - tolower(*ct++);
    }
    return ret;
}

int
tcb_startswith_nc(char *cs, char *ct)
{ /* return true if cs start with ct */
    return (0 == tcb_strncmp_nc(cs, ct, (int)strlen(ct)));
}




/***:-----------------------------------------------------------------------
 ***: START OF WINDOWS FUNCTIONS
 ***:-----------------------------------------------------------------------
 */


/***:-----------------------------------------------------------------------
 ***: print utilties
 ***:-----------------------------------------------------------------------
 */

static int g_PR_indent=1;

void PR(char *fmt, ...)
{
  int fmt_len;
  va_list args;
  va_start(args,fmt);

  if (g_PR_indent) printf("  ");
  g_PR_indent=0;
  fmt_len=(int) strlen(fmt);
  if (fmt_len>0 && fmt[fmt_len-1]=='\n') g_PR_indent=1;

  vprintf(fmt,args);
  tcb_prflush();
  va_end(args);
}


/***:-----------------------------------------------------------------------
 ***: val()
 ***:-----------------------------------------------------------------------
 */

_TCBVAR *
lu_tcbvar(char *name)
{
  _TCBVAR *tvp=g_tcb_info;

  while (tvp->name!=NULL) {
    if      (tcb_strmatch_nc(name,tvp->name)) return tvp;
    else if (tcb_strmatch_nc(name,tvp->aka )) return tvp;
    tvp+=1;
  }
  tcb_code_err_exit("lu_tcbvar: bad name %s\n",name);
  return NULL;
}

unsigned
val(char *name)
{
  _TCBVAR *tvp;

  tvp=lu_tcbvar(name);
  return tvp->val;
}

ui64
val64(char *name)
{
  _TCBVAR *tvp;

  tvp=lu_tcbvar(name);
  return tvp->rawval;
}



/***:-----------------------------------------------------------------------
 ***: get_tcb_bits
 ***:-----------------------------------------------------------------------
 */


static int
get_tcb_bit(unsigned char *A, int bit)
{
  int ret=0;
  int ix,shift;

  ix = 127 - (bit>>3);
  shift=bit&0x7;
  /*  prdbg("  ix: %u, shift=%u\n",ix,shift); */
  ret=(A[ix] >> shift) & 1;
  return ret;
}

static ui64
get_tcb_bits (unsigned char  *A, int hi, int lo)
{
  ui64 ret=0;

  if (lo>hi) {
    int temp=lo;
    lo=hi;
    hi=temp;
  }

  while (hi>=lo) {
    ret = (ret<<1) | get_tcb_bit(A,hi);
    --hi;
  }
  
  return ret;
}


void
decompress_val(_TCBVAR *tvp,unsigned ulp_type,unsigned tx_max, 
	       unsigned rcv_nxt,unsigned rx_frag0_start_idx_raw)
{
  unsigned rawval=(unsigned) tvp->rawval;

  switch(tvp->comp) {
  case COMP_NONE: tvp->val=rawval;  break;
  case COMP_ULP:  tvp->val=rawval;  break;
  case COMP_TX_MAX: 
    tvp->val=(tx_max - rawval) & 0xFFFFFFFF;
    break;
  case COMP_RCV_NXT:
    if (tcb_startswith_nc(tvp->name,"rx_frag")) {
      unsigned fragx=0;
      if (!tcb_strmatch_nc(tvp->name,"rx_frag0_start_idx_raw")) 
	fragx=rawval;
      tvp->val=(rcv_nxt+rx_frag0_start_idx_raw+fragx) & 0xFFFFFFFF;
    } else {
      tvp->val=(rcv_nxt - rawval) & 0xFFFFFFFF;
    }
    break;
  case COMP_PTR: tvp->val=rawval;  break;
  case COMP_LEN: 
    {
      tvp->val=rawval;  
      if (PM_MODE_RDDP==ulp_type ||  PM_MODE_DDP==ulp_type ||
	  PM_MODE_IANDP==ulp_type) {
	/* TP does this internally.  Not sure if I should show the
	 *  unaltered value or the raw value.  For now I
	 *  will diplay the raw value.  For now I've added the code
	 *  mainly to stop windows compiler from warning about ulp_type
	 *  being an unreferenced parameter.
	 */
	tvp->val=0;
	tvp->val=rawval;  /* comment this out to display altered value */
      }
    }
    break;
  default:
    tcb_code_err_exit("decompress_val, bad switch: %d",tvp->comp);
    break;
  }



}


void
get_tcb_field(_TCBVAR *tvp,unsigned char *buf)
{
  assert(tvp->hi-tvp->lo+1<=64);
  assert(tvp->hi>=tvp->lo);

  tvp->rawval=get_tcb_bits(buf,tvp->lo,tvp->hi);
  /* assume no compression and 32-bit value for now */
  tvp->val=(unsigned) (tvp->rawval & 0xFFFFFFFF);  


}


/***:-----------------------------------------------------------------------
 ***: spr_* functions
 ***:-----------------------------------------------------------------------
 */

char *
spr_tcp_state (unsigned state) 
{
  char *ret="UNKNOWN";

  if      ( 0 == state) {ret = "CLOSED";}
  else if ( 1 == state) {ret = "LISTEN";}
  else if ( 2 == state) {ret = "SYN_SENT";}
  else if ( 3 == state) {ret = "SYN_RCVD";}
  else if ( 4 == state) {ret = "ESTABLISHED";}
  else if ( 5 == state) {ret = "CLOSE_WAIT";}
  else if ( 6 == state) {ret = "FIN_WAIT_1";}
  else if ( 7 == state) {ret = "CLOSING";}
  else if ( 8 == state) {ret = "LAST_ACK";}
  else if ( 9 == state) {ret = "FIN_WAIT_2";}
  else if (10 == state) {ret = "TIME_WAIT";}
  else if (11 == state) {ret = "ESTABLISHED_RX";}
  else if (12 == state) {ret = "ESTABLISHED_TX";}
  else if (13 == state) {ret = "SYN_PEND";}
  else if (14 == state) {ret = "ESC_1_STATE";}
  else if (15 == state) {ret = "ESC_2_STATE";}

  return ret;
}

char *
spr_cctrl_sel(unsigned sel0,unsigned sel1)
{
  unsigned sel=(sel1<<1) | sel0;
  char *ret="UNKNOWN";

  if      ( 0 == sel) {ret = "Reno";}
  else if ( 1 == sel) {ret = "Tahoe";}
  else if ( 2 == sel) {ret = "NewReno";}
  else if ( 3 == sel) {ret = "HighSpeed";}

  return ret;
}


char *
spr_ulp_type(unsigned ulp_type)
{
  char *ret="UNKNOWN";

  /*The tp.h PM_MODE_XXX call 1 DDP and 5 IANDP, but external
   * documentation (tcb.h" calls 5 ddp, and doesn't mention 1 or 3.
   */
  
  if      ( PM_MODE_PASS  == ulp_type) {ret = "TOE";}
  else if ( PM_MODE_DDP   == ulp_type) {ret = "DDP";}
  else if ( PM_MODE_ISCSI == ulp_type) {ret = "ISCSI";}
  else if ( PM_MODE_IWARP == ulp_type) {ret = "IWARP";}
  else if ( PM_MODE_RDDP  == ulp_type) {ret = "RDMA";}
  else if ( PM_MODE_IANDP == ulp_type) {ret = "IANDP_DDP";}
  else if ( PM_MODE_FCOE  == ulp_type) {ret = "FCoE";}
  else if ( PM_MODE_USER  == ulp_type) {ret = "USER";}
  else if ( PM_MODE_TLS   == ulp_type) {ret = "TLS";}
  else if ( PM_MODE_DTLS  == ulp_type) {ret = "DTLS";}

  return ret;
}

char *
spr_ip_version(unsigned ip_version)
{
  char *ret="UNKNOWN";

  if      ( 0 == ip_version) {ret = "IPv4";}
  else if ( 1 == ip_version) {ret = "IPv6";}

  return ret;
}



/***:-----------------------------------------------------------------------
 ***: display_tcb()
 ***:-----------------------------------------------------------------------
 */

void
display_tcb_compressed(_TCBVAR *tvp,int aux)
{

  if (g_tN==4) {
    t4_display_tcb_aux_0(tvp,aux);
    if      (1==aux) t4_display_tcb_aux_1(tvp,aux);
    else if (2==aux) t4_display_tcb_aux_2(tvp,aux);
    else if (3==aux) t4_display_tcb_aux_3(tvp,aux);

  } else if (g_tN==5) {
    t5_display_tcb_aux_0(tvp,aux);
    if      (1==aux) t5_display_tcb_aux_1(tvp,aux);
    else if (2==aux) t5_display_tcb_aux_2(tvp,aux);
    else if (3==aux) t5_display_tcb_aux_3(tvp,aux);
  } else if (g_tN==6) {
    t6_display_tcb_aux_0(tvp,aux);
    if      (1==aux) t6_display_tcb_aux_1(tvp,aux);
    else if (2==aux) t6_display_tcb_aux_2(tvp,aux);
    else if (3==aux) t6_display_tcb_aux_3(tvp,aux);
    else if (4==aux) t6_display_tcb_aux_4(tvp,aux); 
  }
}




/***:-----------------------------------------------------------------------
 ***: parse_n_decode_tcb
 ***:-----------------------------------------------------------------------
 */


unsigned
parse_tcb( _TCBVAR *base_tvp, unsigned char *buf)
{   /* parse the TCB */
  _TCBVAR *tvp=base_tvp;
  unsigned ulp_type;
  int aux=1;  /* assume TOE or iSCSI */
  unsigned tx_max=0, rcv_nxt=0, rx_frag0_start_idx_raw=0;
  int got_tx_max=0, got_rcv_nxt=0, got_rx_frag0_start_idx_raw=0;


  /* parse the TCB */
  while (tvp->name!=NULL) {
    get_tcb_field(tvp,buf);
    if (!got_tx_max && tcb_strmatch_nc("tx_max",tvp->name)) {
      tx_max=tvp->val;
      got_tx_max=1;
    }
    if (!got_rcv_nxt && tcb_strmatch_nc("rcv_nxt",tvp->name)) {
      rcv_nxt=tvp->val;
      got_rcv_nxt=1;
    }
    if (!got_rx_frag0_start_idx_raw && 
	tcb_strmatch_nc("rx_frag0_start_idx_raw",tvp->name)) {
      rx_frag0_start_idx_raw=tvp->val;
      got_rx_frag0_start_idx_raw=1;
    }
    tvp+=1;
  }

  tvp=base_tvp;
  ulp_type=tvp->val;  /* ULP type is always first variable in TCB */
  if (PM_MODE_IANDP==ulp_type || PM_MODE_FCOE==ulp_type) aux=3;
  else if (PM_MODE_RDDP==ulp_type) aux=2;
  else if (6==g_tN && (PM_MODE_TLS==ulp_type || PM_MODE_DTLS==ulp_type)) aux=4;
  else aux=1;

  assert(got_tx_max && got_rcv_nxt && got_rx_frag0_start_idx_raw);
 
  /* decompress the compressed values */
  tvp=base_tvp;
  while (tvp->name!=NULL) {
    decompress_val(tvp,ulp_type,tx_max,rcv_nxt,rx_frag0_start_idx_raw);
    tvp+=1;
  }

  return aux;
}



void
parse_scb( _TCBVAR *base_tvp, unsigned char *buf)
{   /* parse the SCB */
  _TCBVAR *tvp=base_tvp;

  while (tvp->name!=NULL) {
    if (tcb_strmatch_nc("scb_slush",tvp->name)) {
      /* the scb_slush field is all of remaining memory */
      tvp->rawval=0;
      tvp->val=0;
    } else {
      get_tcb_field(tvp,buf);
    }
    tvp+=1;
  }
}


void
parse_fcb( _TCBVAR *base_tvp, unsigned char *buf)
{   /* parse the FCB */
  _TCBVAR *tvp=base_tvp;

  while (tvp->name!=NULL) {
    get_tcb_field(tvp,buf);
    tvp+=1;
  }
}


void
display_list_tcb(_TCBVAR *base_tvp,int aux)
{
  _TCBVAR *tvp=base_tvp;
  while (tvp->name!=NULL) {
    if (tvp->aux==0 || tvp->aux==aux) {
      if (tvp->hi-tvp->lo+1<=32) {
	printf("  %4d:%4d %31s: %10u (0x%1x)",tvp->lo,tvp->hi,tvp->name,
	       (unsigned) tvp->rawval,(unsigned) tvp->rawval);
	if (COMP_TX_MAX==tvp->comp || COMP_RCV_NXT==tvp->comp) 
	  printf("  -> %1u (0x%x)", tvp->val,tvp->val);
      } else {
	printf("  %4d:%4d %31s: 0x%1llx",tvp->lo,tvp->hi,tvp->name,
	       tvp->rawval);
      }
      printf("\n");
    }
    tvp+=1;
  }
}

void
display_tcb(_TCBVAR *tvp,unsigned char *buf,int aux)
{
  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_RAW) {
    tcb_hexdump(0,buf,128); 
    printf("\n"); 
  }

  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_LIST) {
    display_list_tcb(tvp,aux);
  }

  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_COMP) {
    display_tcb_compressed(tvp,aux);
  }
  
}

void
parse_n_display_tcb(unsigned char *buf)
{
  _TCBVAR *tvp=g_tcb_info;
  int aux;

  aux=parse_tcb(tvp,buf);
  display_tcb(tvp,buf,aux);
}

void
parse_n_display_scb(unsigned char *buf)
{
  _TCBVAR *tvp=g_scb_info;

  parse_scb(tvp,buf);
  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_RAW) {
    tcb_hexdump(0,buf,128); 
    printf("\n"); 
  }
  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_LIST || 
      g_prntstyl==PRNTSTYL_COMP) {
    display_list_tcb(tvp,0);
  }
}

void
parse_n_display_fcb(unsigned char *buf)
{
  _TCBVAR *tvp=g_fcb_info;

  parse_fcb(tvp,buf);
  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_RAW) {
    tcb_hexdump(0,buf,128); 
    printf("\n"); 
  }

  if (g_prntstyl==PRNTSTYL_VERBOSE ||
      g_prntstyl==PRNTSTYL_LIST || 
      g_prntstyl==PRNTSTYL_COMP) {
    display_list_tcb(tvp,0);
  }
}

void
parse_n_display_xcb(unsigned char *buf)
{
  if      (g_got_scb) parse_n_display_scb(buf);
  else if (g_got_fcb) parse_n_display_fcb(buf);
  else                parse_n_display_tcb(buf);
}

/***:-----------------------------------------------------------------------
 ***: swizzle_tcb
 ***:-----------------------------------------------------------------------
 */

void
swizzle_tcb(unsigned char *buf)
{
  int i,j,k;

  for (i=0, j=128-16 ; i<j ; i+=16, j-=16) {
    unsigned char temp;
    for (k=0; k<16; ++k) {
      temp=buf[i+k];
      buf[i+k]=buf[j+k];
      buf[j+k]=temp;
    }
  }
}


/***:-----------------------------------------------------------------------
 ***: END OF WINDOWS FUNCTIONS
 ***:-----------------------------------------------------------------------
 */

void set_tidtype(unsigned int tidtype)
{
    if (tidtype == TIDTYPE_SCB)
    {
        g_got_scb = 1;
    }
    else if (tidtype == TIDTYPE_FCB)
    {
        g_got_fcb = 1;
    }
    else
    {
        g_got_scb = 0;
        g_got_fcb = 0;
    }

}

void
set_tcb_info(unsigned int tidtype, unsigned int cardtype)
{
    set_tidtype(tidtype);

    g_tN = cardtype;
    if (4 == g_tN) {
        g_tcb_info = g_tcb_info4;
        g_scb_info = g_scb_info4;
        g_fcb_info = g_fcb_info4;
    }
    else if (5 == g_tN) {
        g_tcb_info = g_tcb_info5;
        g_scb_info = g_scb_info5;
        g_fcb_info = g_fcb_info5;
    }
    else if (6 == g_tN) {
        g_tcb_info = g_tcb_info6;
        g_scb_info = g_scb_info6;
        g_fcb_info = g_fcb_info6;
    }
}

void
set_print_style(unsigned int prntstyl)
{
  g_prntstyl=prntstyl;
}
