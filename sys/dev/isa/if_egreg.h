/*	$OpenBSD: if_egreg.h,v 1.3 2000/06/05 20:56:20 niklas Exp $	*/
/*	$NetBSD: if_egreg.h,v 1.3 1995/07/23 21:14:35 mycroft Exp $	*/

/*
 * Copyright (c) 1993 Dean Huxley (dean@fsa.ca)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Dean Huxley.
 * 4. The name of Dean Huxley may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * Register offsets from base.
 */
#define EG_COMMAND	0x00
#define EG_STATUS	0x02
#define EG_DATA		0x04
#define EG_CONTROL	0x06

#define EG_IO_PORTS	8

/*
 * Host Control Register bits
 * EG_CTL_ATTN - does a soft reset
 * EG_CTL_FLSH - flushes the data register
 * EG_CTL_RESET - does a hard reset
 * EG_CTL_DMAE - Used with DIR bit, enables DMA transfers to/from data reg.
 * EG_CTL_DIR  - if clear then host -> adapter, if set then adapter -> host
 * EG_CTL_TCEN - terminal count enable. enables host interrupt after DMA.
 * EG_CTL_CMDE - command reg interrupt enable. (when it is written)
 * EG_CTL_HSF1 - Host status flag 1
 * EG_CTL_HSF2 - Host status flag 2
 */

#define EG_CTL_ATTN 0x80
#define EG_CTL_FLSH 0x40
#define EG_CTL_RESET (EG_CTL_ATTN|EG_CTL_FLSH)
#define EG_CTL_DMAE 0x20
#define EG_CTL_DIR  0x10
#define EG_CTL_TCEN 0x08
#define EG_CTL_CMDE 0x04
#define EG_CTL_HSF2 0x02
#define EG_CTL_HSF1 0x01

/*
 * Host Status Register bits
 * EG_STAT_HRDY - Data Register ready 
 * EG_STAT_HCRE - Host Command Register empty
 * EG_STAT_ACRF - Adapter Command register full
 * EG_STAT_DIR  - Direction flag, 0 = host -> adapter, 1 = adapter -> host
 * EG_STAT_DONE - DMA done
 * EG_STAT_ASF1 - Adapter status flag 1
 * EG_STAT_ASF2 - Adapter status flag 2
 * EG_STAT_ASF3 - Adapter status flag 3
 */

#define EG_STAT_HRDY 0x80
#define EG_STAT_HCRE 0x40
#define EG_STAT_ACRF 0x20
#define EG_STAT_DIR  0x10
#define EG_STAT_DONE 0x08
#define EG_STAT_ASF3 0x04
#define EG_STAT_ASF2 0x02
#define EG_STAT_ASF1 0x01

#define	EG_PCB_NULL	0x00
#define EG_PCB_ACCEPT	0x01 
#define EG_PCB_REJECT	0x02
#define EG_PCB_DONE	0x03
#define EG_PCB_STAT	0x03

#define EG_CMD_CONFIG82586	0x02
#define EG_CMD_GETEADDR		0x03
#define EG_CMD_RECVPACKET	0x08
#define EG_CMD_SENDPACKET	0x09
#define EG_CMD_GETSTATS		0x0a
#define EG_CMD_SETEADDR		0x10
#define EG_CMD_GETINFO		0x11

#define EG_RSP_CONFIG82586	0x32
#define EG_RSP_GETEADDR		0x33
#define EG_RSP_RECVPACKET	0x38
#define EG_RSP_SENDPACKET	0x39
#define EG_RSP_GETSTATS		0x3a
#define EG_RSP_SETEADDR		0x40
#define EG_RSP_GETINFO		0x41
