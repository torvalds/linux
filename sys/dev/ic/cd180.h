/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1995 by Pavel Antonov, Moscow, Russia.
 * Copyright (C) 1995 by Andrey A. Chernov, Moscow, Russia.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND
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
 * $FreeBSD$
 */

/*
 * Cirrus Logic CD180 registers
 */

/* Global registers */
#define CD180_GIVR      0x40    /* Global Interrupt Verctor Register      */
#define CD180_GICR      0x41    /* Global Interrupting Channel Register   */
#define CD180_PILR1     0x61    /* Priority Interrupt Level Register 1    */
#define CD180_PILR2     0x62    /* Priority Interrupt Level Register 2    */
#define CD180_PILR3     0x63    /* Priority Interrupt Level Register 3    */
#define CD180_CAR       0x64    /* Channel Access Register                */
#define CD180_GFRCR     0x6B    /* Global Firmware Revision Code Register */
#define CD180_PPRH      0x70    /* Prescaler Period Register MSB          */
#define CD180_PPRL      0x71    /* Prescaler Period Register LSB          */
#define CD180_RDR       0x78    /* Receiver Data Register                 */
#define CD180_RCSR      0x7A    /* Receiver Character Status Register     */
#define CD180_TDR       0x7B    /* Transmit Data Register                 */
#define CD180_EOIR      0x7F    /* End of Interrupt Register              */

/* Channel Registers */
#define CD180_CCR       0x01    /* Channel Command Register               */
#define CD180_IER       0x02    /* Interrupt Enable Register              */
#define CD180_COR1      0x03    /* Channel Option Register 1              */
#define CD180_COR2      0x04    /* Channel Option Register 1              */
#define CD180_COR3      0x05    /* Channel Option Register 1              */
#define CD180_CCSR      0x06    /* Channel Control STatus Register        */
#define CD180_RDCR      0x07    /* Receive Data Count Register            */
#define CD180_SCHR1     0x09    /* Special Character Register 1           */
#define CD180_SCHR2     0x0A    /* Special Character Register 2           */
#define CD180_SCHR3     0x0B    /* Special Character Register 3           */
#define CD180_SCHR4     0x0C    /* Special Character Register 4           */
#define CD180_MCOR1     0x10    /* Modem Change Option 1 Register         */
#define CD180_MCOR2     0x11    /* Modem Change Option 2 Register         */
#define CD180_MCR       0x12    /* Modem Change Register                  */
#define CD180_RTPR      0x18    /* Receive Timeout Period Register        */
#define CD180_MSVR      0x28    /* Modem Signal Value Register            */
#define CD180_RBPRH     0x31    /* Receive Baud Rate Period Register MSB  */
#define CD180_RBPRL     0x32    /* Receive Baud Rate Period Register LSB  */
#define CD180_TBPRH     0x39    /* Transmit Baud Rate Period Register MSB */
#define CD180_TBPRL     0x3A    /* Transmit Baud Rate Period Register LSB */

/** Register descritpions **/

/* Global Interrupt Vector Register */
#define GIVR_IT_MSCI    0x01    /* Modem Signal Change Interrupt          */
#define GIVR_IT_TDI     0x02    /* Transmit Data Interrupt                */
#define GIVR_IT_RGDI    0x03    /* Receive Good Data Interrupt            */
#define GIVR_IT_REI     0x07    /* Receive Exception Interrupt            */

/* Global Interrupt Channel Register */
#define GICR_CHAN       0x1C    /* Channel Number Mask                    */
#define GICR_LSH        2       /* Channel Number Shift                   */

/* Channel Address Register */
#define CAR_CHAN        0x07    /* Channel Number Mask                    */
#define	CAR_A7          0x08    /* Address bit 7 (unused)                  */

/* Receive Character Status Register */
#define RCSR_OE         0x01    /* Overrun Error                          */
#define RCSR_FE         0x02    /* Frame Error                            */
#define RCSR_PE         0x04    /* Parity Error                           */
#define RCSR_Break      0x08    /* Break detected                         */
#define RCSR_Timeout    0x80    /* Rx Timeout                             */
#define RCSR_SCMASK     0x70    /* Special Character Detected Mask        */
#define RCSR_SC1        0x10    /* Special Char 1 (or 1 & 3 seq matched)  */
#define RCSR_SC2        0x20    /* Special Char 2 (or 2 & 4 seq matched)  */
#define RCSR_SC3        0x30    /* Special Char 3                         */
#define RCSR_SC4        0x40    /* Special Char 4                         */

/* Channel Command Register */
#define CCR_ResetChan   0x80    /* Reset Channel                          */
#define CCR_HWRESET     0x81    /* Hardware Reset (all channels)          */
#define CCR_CORCHG1     0x42    /* Channel Option Register 1 Changed      */
#define CCR_CORCHG2     0x44    /* Channel Option Register 2 Changed      */
#define CCR_CORCHG3     0x48    /* Channel Option Register 3 Changed      */
#define CCR_SENDSPCH1   0x21    /* Send Special Character 1               */
#define CCR_SENDSPCH2   0x22    /* Send Special Character 2               */
#define CCR_SENDSPCH3   0x23    /* Send Special Character 3               */
#define CCR_SENDSPCH4   0x24    /* Send Special Character 4               */
#define CCR_RCVRDIS     0x11    /* Receiver Disable                       */
#define CCR_RCVREN      0x12    /* Receiver Enable                        */
#define CCR_XMTRDIS     0x14    /* Transmitter Disable                    */
#define CCR_XMTREN      0x18    /* Transmitter Enable                     */

/* Interrupt Enable Register */
#define IER_DSR         0x80    /* Enable interrupt on DSR change         */
#define IER_CD          0x40    /* Enable interrupt on CD change          */
#define IER_CTS         0x20    /* Enable interrupt on CTS change         */
#define IER_RxData      0x10    /* Enable interrupt on Receive Data       */
#define IER_RxSC        0x08    /* Enable interrupt on Receive Spec. Char */
#define IER_TxRdy       0x04    /* Enable interrupt on TX FIFO empty      */
#define IER_TxMpty      0x02    /* Enable interrupt on TX completely empty*/
#define IER_RET         0x01    /* Enable interrupt on RX Except. Timeout */

/* Channel Option Register 1 */
#define COR1_ODDP       0x80    /* Odd Parity                             */
#define COR1_ParMMASK   0x60    /* Parity Mode mask                       */
#define COR1_NOPAR      0x02    /* No Parity                              */
#define COR1_FORCEPAR   0x20    /* Force Parity                           */
#define COR1_NORMPAR    0x40    /* Normal Parity                          */
#define COR1_Ignore     0x10    /* Ignore Parity on RX                    */
#define COR1_StopMASK   0x0C    /* Stop Bits mode mask                    */
#define COR1_1SB        0x00    /* 1 Stop Bit                             */
#define COR1_15SB       0x04    /* 1.5 Stop Bits                          */
#define COR1_2SB        0x08    /* 2 Stop Bits                            */
#define COR1_CHLMASK    0x03    /* Character Length mask                  */
#define COR1_5BITS      0x00    /* 5 bits                                 */
#define COR1_6BITS      0x01    /* 6 bits                                 */
#define COR1_7BITS      0x02    /* 7 bits                                 */
#define COR1_8BITS      0x03    /* 8 bits                                 */

/* Channel Option Register 2 */
#define COR2_IXM        0x80    /* Implied XON mode                       */
#define COR2_TxIBE      0x40    /* Enable In-Band XON/XOFF Flow Control   */
#define COR2_ETC        0x20    /* Embedded Tx Commands Enable            */
#define COR2_LLM        0x10    /* Local Loopback Mode                    */
#define COR2_RLM        0x08    /* Remote Loopback Mode                   */
#define COR2_RtsAO      0x04    /* RTS Automatic Output Enable            */
#define COR2_CtsAE      0x02    /* CTS Automatic Enable                   */
#define COR2_DsrAE      0x01    /* DSR Automatic Enable                   */

/* Channel Option Register 3 */
#define COR3_XonCH      0x80    /* XON is a double seq (1 & 3)            */
#define COR3_XoffCH     0x40    /* XOFF is a double seq (1 & 3)           */
#define COR3_FCT        0x20    /* Flow-Control Transparency Mode         */
#define COR3_SCDE       0x10    /* Special Character Detection Enable     */
#define COR3_RxTHMASK   0x0F    /* RX FIFO Threshold value (1-8)          */

/* Channel Control Status Register */
#define CCSR_RxEn       0x80    /* Revceiver Enabled                      */
#define CCSR_RxFloff    0x40    /* Receive Flow Off (XOFF sent)           */
#define CCSR_RxFlon     0x20    /* Receive Flow On (XON sent)             */
#define CCSR_TxEn       0x08    /* Transmitter Enabled                    */
#define CCSR_TxFloff    0x04    /* Transmit Flow Off (got XOFF)           */
#define CCSR_TxFlon     0x02    /* Transmit Flow On (got XON)             */

/* Modem Change Option Register 1 */
#define MCOR1_DSRzd     0x80    /* Detect 0->1 transition of DSR          */
#define MCOR1_CDzd      0x40    /* Detect 0->1 transition of CD           */
#define MCOR1_CTSzd     0x20    /* Detect 0->1 transition of CTS          */
#define MCOR1_DTRthMASK 0x0F    /* Automatic DTR FC Threshold (1-8) chars */

/* Modem Change Option Register 2 */
#define MCOR2_DSRod     0x80    /* Detect 1->0 transition of DSR          */
#define MCOR2_CDod      0x40    /* Detect 1->0 transition of CD           */
#define MCOR2_CTSod     0x20    /* Detect 1->0 transition of CTS          */

/* Modem Change Register */
#define MCR_DSRchg      0x80    /* DSR Changed                            */
#define MCR_CDchg       0x40    /* CD  Changed                            */
#define MCR_CTSchg      0x20    /* CTS Changed                            */

/* Modem Signal Value Register */
#define MSVR_DSR        0x80    /* Current state of DSR input             */
#define MSVR_CD         0x40    /* Current state of DSR input             */
#define MSVR_CTS        0x20    /* Current state of CTS input             */
#define MSVR_DTR        0x02    /* Current state of DTR output            */
#define MSVR_RTS        0x01    /* Current state of RTS output            */

/* Escape characters */
#define CD180_C_ESC     0x00    /* Escape character                       */
#define CD180_C_SBRK    0x81    /* Start sending BREAK                    */
#define CD180_C_DELAY   0x82    /* Delay output                           */
#define CD180_C_EBRK    0x83    /* Stop sending BREAK                     */

/* Miscellaneous */
#define CD180_NCHAN     8       /* 8 channels per chip                    */
#define CD180_CTICKS    16      /* 16 ticks for character processing      */
#define CD180_NFIFO     8       /* 8 bytes in FIFO                        */
