/****************************************************************************/
/*
 *  linux/include/asm-arm/arch-l7200/sib.h
 *
 *  Registers and helper functions for the Serial Interface Bus.
 *
 *  (C) Copyright 2000, S A McConnell  (samcconn@cotw.com)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/****************************************************************************/

#define SIB_OFF   0x00040000  /* Offset from IO_START to the SIB reg's. */

/* IO_START and IO_BASE are defined in hardware.h */

#define SIB_START (IO_START + SIB_OFF) /* Physical addr of the SIB reg. */
#define SIB_BASE  (IO_BASE  + SIB_OFF) /* Virtual addr of the SIB reg.  */

/* Offsets from the start of the SIB for all the registers. */

/* Define the SIB registers for use by device drivers and the kernel. */

typedef struct
{
     unsigned int MCCR;    /* SIB Control Register           Offset: 0x00 */
     unsigned int RES1;    /* Reserved                       Offset: 0x04 */
     unsigned int MCDR0;   /* SIB Data Register 0            Offset: 0x08 */
     unsigned int MCDR1;   /* SIB Data Register 1            Offset: 0x0c */
     unsigned int MCDR2;   /* SIB Data Register 2 (UCB1x00)  Offset: 0x10 */
     unsigned int RES2;    /* Reserved                       Offset: 0x14 */
     unsigned int MCSR;    /* SIB Status Register            Offset: 0x18 */
} SIB_Interface;

#define SIB ((volatile SIB_Interface *) (SIB_BASE))

/* MCCR */

#define INTERNAL_FREQ   9216000  /* Hertz */
#define AUDIO_FREQ         5000  /* Hertz */
#define TELECOM_FREQ       5000  /* Hertz */

#define AUDIO_DIVIDE    (INTERNAL_FREQ / (32 * AUDIO_FREQ))
#define TELECOM_DIVIDE  (INTERNAL_FREQ / (32 * TELECOM_FREQ))

#define MCCR_ASD57      AUDIO_DIVIDE
#define MCCR_TSD57      (TELECOM_DIVIDE << 8)
#define MCCR_MCE        (1 << 16)             /* SIB enable */
#define MCCR_ECS        (1 << 17)             /* External Clock Select */
#define MCCR_ADM        (1 << 18)             /* A/D Data Sampling */
#define MCCR_PMC        (1 << 26)             /* PIN Multiplexer Control */


#define GET_ASD ((SIB->MCCR >>  0) & 0x3f) /* Audio Sample Rate Div. */
#define GET_TSD ((SIB->MCCR >>  8) & 0x3f) /* Telcom Sample Rate Div. */
#define GET_MCE ((SIB->MCCR >> 16) & 0x01) /* SIB Enable */
#define GET_ECS ((SIB->MCCR >> 17) & 0x01) /* External Clock Select */
#define GET_ADM ((SIB->MCCR >> 18) & 0x01) /* A/D Data Sampling Mode */
#define GET_TTM ((SIB->MCCR >> 19) & 0x01) /* Telco Trans. FIFO I mask */ 
#define GET_TRM ((SIB->MCCR >> 20) & 0x01) /* Telco Recv. FIFO I mask */
#define GET_ATM ((SIB->MCCR >> 21) & 0x01) /* Audio Trans. FIFO I mask */ 
#define GET_ARM ((SIB->MCCR >> 22) & 0x01) /* Audio Recv. FIFO I mask */
#define GET_LBM ((SIB->MCCR >> 23) & 0x01) /* Loop Back Mode */
#define GET_ECP ((SIB->MCCR >> 24) & 0x03) /* Extern. Clck Prescale sel */
#define GET_PMC ((SIB->MCCR >> 26) & 0x01) /* PIN Multiplexer Control */
#define GET_ERI ((SIB->MCCR >> 27) & 0x01) /* External Read Interrupt */
#define GET_EWI ((SIB->MCCR >> 28) & 0x01) /* External Write Interrupt */

/* MCDR0 */

#define AUDIO_RECV     ((SIB->MCDR0 >> 4) & 0xfff)
#define AUDIO_WRITE(v) ((SIB->MCDR0 = (v & 0xfff) << 4))

/* MCDR1 */

#define TELECOM_RECV     ((SIB->MCDR1 >> 2) & 032fff)
#define TELECOM_WRITE(v) ((SIB->MCDR1 = (v & 0x3fff) << 2))


/* MCSR */

#define MCSR_ATU (1 << 4)  /* Audio Transmit FIFO Underrun */
#define MCSR_ARO (1 << 5)  /* Audio Receive  FIFO Underrun */
#define MCSR_TTU (1 << 6)  /* TELECOM Transmit FIFO Underrun */
#define MCSR_TRO (1 << 7)  /* TELECOM Receive  FIFO Underrun */

#define MCSR_CLEAR_UNDERUN_BITS (MCSR_ATU | MCSR_ARO | MCSR_TTU | MCSR_TRO)


#define GET_ATS ((SIB->MCSR >>  0) & 0x01) /* Audio Transmit FIFO Service Req*/
#define GET_ARS ((SIB->MCSR >>  1) & 0x01) /* Audio Recv FIFO Service Request*/
#define GET_TTS ((SIB->MCSR >>  2) & 0x01) /* TELECOM Transmit FIFO  Flag */
#define GET_TRS ((SIB->MCSR >>  3) & 0x01) /* TELECOM Recv FIFO Service Req. */
#define GET_ATU ((SIB->MCSR >>  4) & 0x01) /* Audio Transmit FIFO Underrun */
#define GET_ARO ((SIB->MCSR >>  5) & 0x01) /* Audio Receive  FIFO Underrun */
#define GET_TTU ((SIB->MCSR >>  6) & 0x01) /* TELECOM Transmit FIFO Underrun */
#define GET_TRO ((SIB->MCSR >>  7) & 0x01) /* TELECOM Receive  FIFO Underrun */
#define GET_ANF ((SIB->MCSR >>  8) & 0x01) /* Audio Transmit FIFO not full */
#define GET_ANE ((SIB->MCSR >>  9) & 0x01) /* Audio Receive FIFO not empty */
#define GET_TNF ((SIB->MCSR >> 10) & 0x01) /* Telecom Transmit FIFO not full */
#define GET_TNE ((SIB->MCSR >> 11) & 0x01) /* Telecom Receive FIFO not empty */
#define GET_CWC ((SIB->MCSR >> 12) & 0x01) /* Codec Write Complete */
#define GET_CRC ((SIB->MCSR >> 13) & 0x01) /* Codec Read Complete */
#define GET_ACE ((SIB->MCSR >> 14) & 0x01) /* Audio Codec Enabled */
#define GET_TCE ((SIB->MCSR >> 15) & 0x01) /* Telecom Codec Enabled */

/* MCDR2 */

#define MCDR2_rW               (1 << 16)

#define WRITE_MCDR2(reg, data) (SIB->MCDR2 =((reg<<17)|MCDR2_rW|(data&0xffff)))
#define MCDR2_WRITE_COMPLETE   GET_CWC

#define INITIATE_MCDR2_READ(reg) (SIB->MCDR2 = (reg << 17))
#define MCDR2_READ_COMPLETE      GET_CRC
#define MCDR2_READ               (SIB->MCDR2 & 0xffff)
