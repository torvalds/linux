/*
 * Driver for Digigram miXart soundcards
 *
 * definitions and makros for basic card access
 *
 * Copyright (c) 2003 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SOUND_MIXART_HWDEP_H
#define __SOUND_MIXART_HWDEP_H

#include <sound/hwdep.h>

#ifndef readl_be
#define readl_be(x) be32_to_cpu((__force __be32)__raw_readl(x))
#endif

#ifndef writel_be
#define writel_be(data,addr) __raw_writel((__force u32)cpu_to_be32(data),addr)
#endif

#ifndef readl_le
#define readl_le(x) le32_to_cpu((__force __le32)__raw_readl(x))
#endif

#ifndef writel_le
#define writel_le(data,addr) __raw_writel((__force u32)cpu_to_le32(data),addr)
#endif

#define MIXART_MEM(mgr,x)	((mgr)->mem[0].virt + (x))
#define MIXART_REG(mgr,x)	((mgr)->mem[1].virt + (x))


/* Daughter board Type */
#define DAUGHTER_TYPE_MASK     0x0F 
#define DAUGHTER_VER_MASK      0xF0 
#define DAUGHTER_TYPEVER_MASK  (DAUGHTER_TYPE_MASK|DAUGHTER_VER_MASK)
 
#define MIXART_DAUGHTER_TYPE_NONE     0x00 
#define MIXART_DAUGHTER_TYPE_COBRANET 0x08 
#define MIXART_DAUGHTER_TYPE_AES      0x0E



#define MIXART_BA0_SIZE 	(16 * 1024 * 1024) /* 16M */
#define MIXART_BA1_SIZE 	(4  * 1024)        /* 4k */

/*
 * -----------BAR 0 --------------------------------------------------------------------------------------------------------
 */
#define  MIXART_PSEUDOREG                          0x2000                    /* base address for pseudoregister */

#define  MIXART_PSEUDOREG_BOARDNUMBER              MIXART_PSEUDOREG+0        /* board number */

/* perfmeter (available when elf loaded)*/
#define  MIXART_PSEUDOREG_PERF_STREAM_LOAD_OFFSET  MIXART_PSEUDOREG+0x70     /* streaming load */
#define  MIXART_PSEUDOREG_PERF_SYSTEM_LOAD_OFFSET  MIXART_PSEUDOREG+0x78     /* system load (reference)*/
#define  MIXART_PSEUDOREG_PERF_MAILBX_LOAD_OFFSET  MIXART_PSEUDOREG+0x7C     /* mailbox load */
#define  MIXART_PSEUDOREG_PERF_INTERR_LOAD_OFFSET  MIXART_PSEUDOREG+0x74     /* interrupt handling  load */

/* motherboard xilinx loader info */
#define  MIXART_PSEUDOREG_MXLX_BASE_ADDR_OFFSET    MIXART_PSEUDOREG+0x9C     /* 0x00600000 */ 
#define  MIXART_PSEUDOREG_MXLX_SIZE_OFFSET         MIXART_PSEUDOREG+0xA0     /* xilinx size in bytes */ 
#define  MIXART_PSEUDOREG_MXLX_STATUS_OFFSET       MIXART_PSEUDOREG+0xA4     /* status = EMBEBBED_STAT_XXX */ 

/* elf loader info */
#define  MIXART_PSEUDOREG_ELF_STATUS_OFFSET        MIXART_PSEUDOREG+0xB0     /* status = EMBEBBED_STAT_XXX */ 

/* 
*  after the elf code is loaded, and the flowtable info was passed to it,
*  the driver polls on this address, until it shows 1 (presence) or 2 (absence)
*  once it is non-zero, the daughter board type may be read
*/
#define  MIXART_PSEUDOREG_DBRD_PRESENCE_OFFSET     MIXART_PSEUDOREG+0x990   

/* Global info structure */
#define  MIXART_PSEUDOREG_DBRD_TYPE_OFFSET         MIXART_PSEUDOREG+0x994    /* Type and version of daughterboard  */


/* daughterboard xilinx loader info */
#define  MIXART_PSEUDOREG_DXLX_BASE_ADDR_OFFSET    MIXART_PSEUDOREG+0x998    /* get the address here where to write the file */ 
#define  MIXART_PSEUDOREG_DXLX_SIZE_OFFSET         MIXART_PSEUDOREG+0x99C    /* xilinx size in bytes */ 
#define  MIXART_PSEUDOREG_DXLX_STATUS_OFFSET       MIXART_PSEUDOREG+0x9A0    /* status = EMBEBBED_STAT_XXX */ 

/*  */
#define  MIXART_FLOWTABLE_PTR                      0x3000                    /* pointer to flow table */

/* mailbox addresses  */

/* message DRV -> EMB */
#define MSG_INBOUND_POST_HEAD       0x010008	/* DRV posts MF + increment4 */
#define	MSG_INBOUND_POST_TAIL       0x01000C	/* EMB gets MF + increment4 */
/* message EMB -> DRV */
#define	MSG_OUTBOUND_POST_TAIL      0x01001C	/* DRV gets MF + increment4 */
#define	MSG_OUTBOUND_POST_HEAD      0x010018	/* EMB posts MF + increment4 */
/* Get Free Frames */
#define MSG_INBOUND_FREE_TAIL       0x010004	/* DRV gets MFA + increment4 */
#define MSG_OUTBOUND_FREE_TAIL      0x010014	/* EMB gets MFA + increment4 */
/* Put Free Frames */
#define MSG_OUTBOUND_FREE_HEAD      0x010010	/* DRV puts MFA + increment4 */
#define MSG_INBOUND_FREE_HEAD       0x010000    /* EMB puts MFA + increment4 */

/* firmware addresses of the message fifos */
#define MSG_BOUND_STACK_SIZE        0x004000    /* size of each following stack */
/* posted messages */
#define MSG_OUTBOUND_POST_STACK     0x108000    /* stack of messages to the DRV */
#define MSG_INBOUND_POST_STACK      0x104000    /* stack of messages to the EMB */
/* available empty messages */
#define MSG_OUTBOUND_FREE_STACK     0x10C000    /* stack of free enveloped for EMB */
#define MSG_INBOUND_FREE_STACK      0x100000    /* stack of free enveloped for DRV */


/* defines for mailbox message frames */
#define MSG_FRAME_OFFSET            0x64
#define MSG_FRAME_SIZE              0x6400
#define MSG_FRAME_NUMBER            32
#define MSG_FROM_AGENT_ITMF_OFFSET  (MSG_FRAME_OFFSET + (MSG_FRAME_SIZE * MSG_FRAME_NUMBER))
#define MSG_TO_AGENT_ITMF_OFFSET    (MSG_FROM_AGENT_ITMF_OFFSET + MSG_FRAME_SIZE)
#define MSG_HOST_RSC_PROTECTION     (MSG_TO_AGENT_ITMF_OFFSET + MSG_FRAME_SIZE)
#define MSG_AGENT_RSC_PROTECTION    (MSG_HOST_RSC_PROTECTION + 4)


/*
 * -----------BAR 1 --------------------------------------------------------------------------------------------------------
 */

/* interrupt addresses and constants */
#define MIXART_PCI_OMIMR_OFFSET                 0x34    /* outbound message interrupt mask register */
#define MIXART_PCI_OMISR_OFFSET                 0x30    /* outbound message interrupt status register */
#define MIXART_PCI_ODBR_OFFSET                  0x60    /* outbound doorbell register */

#define MIXART_BA1_BRUTAL_RESET_OFFSET          0x68    /* write 1 in LSBit to reset board */

#define MIXART_HOST_ALL_INTERRUPT_MASKED        0x02B   /* 0000 0010 1011 */
#define MIXART_ALLOW_OUTBOUND_DOORBELL          0x023   /* 0000 0010 0011 */
#define MIXART_OIDI                             0x008   /* 0000 0000 1000 */


int snd_mixart_setup_firmware(struct mixart_mgr *mgr);

#endif /* __SOUND_MIXART_HWDEP_H */
