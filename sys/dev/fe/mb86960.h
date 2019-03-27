/*-
 * All Rights Reserved, Copyright (C) Fujitsu Limited 1995
 *
 * This software may be used, modified, copied, distributed, and sold, in
 * both source and binary form provided that the above copyright, these
 * terms and the following disclaimer are retained.  The name of the author
 * and/or the contributor may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND THE CONTRIBUTOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR THE CONTRIBUTOR BE LIABLE
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
 * Registers of Fujitsu MB86960A/MB86965A series Ethernet controllers.
 * Written and contributed by M.S. <seki@sysrap.cs.fujitsu.co.jp>
 */

/*
 * Notes on register naming:
 *
 * Fujitsu documents for MB86960A/MB86965A uses no mnemorable names
 * for their registers.  They defined only three names for 32
 * registers and appended numbers to distinguish registers of
 * same name.  Surprisingly, the numbers represent I/O address
 * offsets of the registers from the base addresses, and their
 * names correspond to the "bank" the registers are allocated.
 * All this means that, for example, to say "read DLCR8" has no more
 * than to say "read a register at offset 8 on bank DLCR."
 *
 * The following definitions may look silly, but that's what Fujitsu
 * did, and it is necessary to know these names to read Fujitsu
 * documents..
 */

/* Data Link Control Registers, on invaliant port addresses.  */
#define FE_DLCR0	0
#define FE_DLCR1	1
#define FE_DLCR2	2
#define FE_DLCR3	3
#define FE_DLCR4	4
#define FE_DLCR5	5
#define FE_DLCR6	6
#define FE_DLCR7	7

/* More DLCRs, on register bank #0.  */
#define FE_DLCR8	8
#define FE_DLCR9	9
#define FE_DLCR10	10
#define FE_DLCR11	11
#define FE_DLCR12	12
#define FE_DLCR13	13
#define FE_DLCR14	14
#define FE_DLCR15	15

/* Malticast Address Registers.  On register bank #1.  */
#define FE_MAR8		8
#define FE_MAR9		9
#define FE_MAR10	10
#define FE_MAR11	11
#define FE_MAR12	12
#define FE_MAR13	13
#define FE_MAR14	14
#define FE_MAR15	15

/* Buffer Memory Port Registers.  On register back #2.  */
#define FE_BMPR8	8
#define FE_BMPR9	9
#define FE_BMPR10	10
#define FE_BMPR11	11
#define FE_BMPR12	12
#define FE_BMPR13	13
#define FE_BMPR14	14
#define FE_BMPR15	15

/* More BMPRs, only on 86965, accessible only when JLI mode.  */
#define FE_BMPR16	16
#define FE_BMPR17	17
#define FE_BMPR18	18
#define FE_BMPR19	19

/*
 * Definitions of registers.
 * I don't have Fujitsu documents of MB86960A/MB86965A, so I don't
 * know the official names for each flags and fields.  The following
 * names are assigned by me (the author of this file,) since I cannot
 * mnemorize hexadecimal constants for all of these functions.
 * Comments?
 *
 * I've got documents from Fujitsu web site, recently.  However, it's
 * too late.  Names for some fields (bits) are kept different from
 * those used in the Fujitsu documents...
 */

/* DLCR0 -- transmitter status */
#define FE_D0_BUSERR	0x01	/* Bus write error?			*/
#define FE_D0_COLL16	0x02	/* Collision limit (16) encountered	*/
#define FE_D0_COLLID	0x04	/* Collision on last transmission	*/
#define FE_D0_JABBER	0x08	/* Jabber				*/
#define FE_D0_CRLOST	0x10	/* Carrier lost on last transmission	*/
#define FE_D0_PKTRCD	0x20	/* Last packet looped back correctly	*/
#define FE_D0_NETBSY	0x40	/* Network Busy (Carrier Detected)	*/
#define FE_D0_TXDONE	0x80	/* Transmission complete		*/

/* DLCR1 -- receiver status */
#define FE_D1_OVRFLO	0x01	/* Receiver buffer overflow		*/
#define FE_D1_CRCERR	0x02	/* CRC error on last packet		*/
#define FE_D1_ALGERR	0x04	/* Alignment error on last packet	*/
#define FE_D1_SRTPKT	0x08	/* Short (RUNT) packet is received	*/
#define FE_D1_RMTRST	0x10	/* Remote reset packet (type = 0x0900)	*/
#define FE_D1_DMAEOP	0x20	/* Host asserted End of DMA OPeration	*/
#define FE_D1_BUSERR	0x40	/* Bus read error			*/
#define FE_D1_PKTRDY	0x80	/* Packet(s) ready on receive buffer	*/

/* DLCR2 -- transmitter interrupt control; same layout as DLCR0 */
#define FE_D2_BUSERR	FE_D0_BUSERR
#define FE_D2_COLL16	FE_D0_COLL16
#define FE_D2_COLLID	FE_D0_COLLID
#define FE_D2_JABBER	FE_D0_JABBER
#define FE_D2_TXDONE	FE_D0_TXDONE

#define FE_D2_RESERVED	0x70

/* DLCR3 -- receiver interrupt control; same layout as DLCR1 */
#define FE_D3_OVRFLO	FE_D1_OVRFLO
#define FE_D3_CRCERR	FE_D1_CRCERR
#define FE_D3_ALGERR	FE_D1_ALGERR
#define FE_D3_SRTPKT	FE_D1_SRTPKT
#define FE_D3_RMTRST	FE_D1_RMTRST
#define FE_D3_DMAEOP	FE_D1_DMAEOP
#define FE_D3_BUSERR	FE_D1_BUSERR
#define FE_D3_PKTRDY	FE_D1_PKTRDY

/* DLCR4 -- transmitter operation mode */
#define FE_D4_DSC	0x01	/* Disable carrier sense on trans.	*/
#define FE_D4_LBC	0x02	/* Loop back test control		*/
#define FE_D4_CNTRL	0x04	/* - tied to CNTRL pin of the chip	*/
#define FE_D4_TEST1	0x08	/* Test output #1			*/
#define FE_D4_COL	0xF0	/* Collision counter			*/

#define FE_D4_LBC_ENABLE	0x00	/* Perform loop back test	*/
#define FE_D4_LBC_DISABLE	0x02	/* Normal operation		*/

#define FE_D4_COL_SHIFT	4

/* DLCR5 -- receiver operation mode */
#define FE_D5_AFM0	0x01	/* Receive packets for other stations	*/
#define FE_D5_AFM1	0x02	/* Receive packets for this station	*/
#define FE_D5_RMTRST	0x04	/* Enable remote reset operation	*/
#define FE_D5_SRTPKT	0x08	/* Accept short (RUNT) packets		*/
#define FE_D5_SRTADR	0x10	/* Short (16 bits?) MAC address		*/
#define FE_D5_BADPKT	0x20	/* Accept packets with error		*/
#define FE_D5_BUFEMP	0x40	/* Receive buffer is empty		*/
#define FE_D5_TEST2	0x80	/* Test output #2			*/

/* DLCR6 -- hardware configuration #0 */
#define FE_D6_BUFSIZ	0x03	/* Size of NIC buffer SRAM		*/
#define FE_D6_TXBSIZ	0x0C	/* Size (and config)of trans. buffer	*/
#define FE_D6_BBW	0x10	/* Buffer SRAM bus width		*/
#define FE_D6_SBW	0x20	/* System bus width			*/
#define FE_D6_SRAM	0x40	/* Buffer SRAM access time		*/
#define FE_D6_DLC	0x80	/* Disable DLC (recever/transmitter)	*/

#define FE_D6_BUFSIZ_8KB	0x00	/* The board has  8KB SRAM	*/
#define FE_D6_BUFSIZ_16KB	0x01	/* The board has 16KB SRAM	*/
#define FE_D6_BUFSIZ_32KB	0x02	/* The board has 32KB SRAM	*/
#define FE_D6_BUFSIZ_64KB	0x03	/* The board has 64KB SRAM	*/

#define FE_D6_TXBSIZ_1x2KB	0x00	/* Single 2KB buffer for trans.	*/
#define FE_D6_TXBSIZ_2x2KB	0x04	/* Double 2KB buffers		*/
#define FE_D6_TXBSIZ_2x4KB	0x08	/* Double 4KB buffers		*/
#define FE_D6_TXBSIZ_2x8KB	0x0C	/* Double 8KB buffers		*/

#define FE_D6_BBW_WORD		0x00	/* SRAM has 16 bit data line	*/
#define FE_D6_BBW_BYTE		0x10	/* SRAM has  8 bit data line	*/

#define FE_D6_SBW_WORD		0x00	/* Access with 16 bit (AT) bus	*/
#define FE_D6_SBW_BYTE		0x20	/* Access with  8 bit (XT) bus	*/

#define FE_D6_SRAM_150ns	0x00	/* The board has slow SRAM	*/
#define FE_D6_SRAM_100ns	0x40	/* The board has fast SRAM	*/

#define FE_D6_DLC_ENABLE	0x00	/* Normal operation		*/
#define FE_D6_DLC_DISABLE	0x80	/* Stop sending/receiving	*/

/* DLC7 -- hardware configuration #1 */
#define FE_D7_BYTSWP	0x01	/* Host byte order control		*/
#define FE_D7_EOPPOL	0x02	/* Polarity of DMA EOP signal		*/
#define FE_D7_RBS	0x0C	/* Register bank select			*/
#define FE_D7_RDYPNS	0x10	/* Senses RDYPNSEL input signal		*/
#define FE_D7_POWER	0x20	/* Stand-by (power down) mode control	*/
#define FE_D7_IDENT	0xC0	/* Chip identification			*/

#define FE_D7_BYTSWP_LH	0x00	/* DEC/Intel byte order		*/
#define FE_D7_BYTSWP_HL	0x01	/* IBM/Motorolla byte order	*/

#define FE_D7_RBS_DLCR		0x00	/* Select DLCR8-15		*/
#define FE_D7_RBS_MAR		0x04	/* Select MAR8-15		*/
#define FE_D7_RBS_BMPR		0x08	/* Select BMPR8-15		*/

#define FE_D7_POWER_DOWN	0x00	/* Power down (stand-by) mode	*/
#define FE_D7_POWER_UP		0x20	/* Normal operation		*/

#define FE_D7_IDENT_TDK		0x00	/* TDK chips?			*/
#define FE_D7_IDENT_NICE	0x80	/* Fujitsu NICE (86960)		*/
#define FE_D7_IDENT_EC		0xC0	/* Fujitsu EtherCoupler (86965)	*/

/* DLCR8 thru DLCR13 are for Ethernet station address.  */

/* DLCR14 and DLCR15 are for TDR.  (TDR is used for cable diagnostic.)  */

/* MAR8 thru MAR15 are for Multicast address filter.  */

/* BMPR8 and BMPR9 are for packet data.  */

/* BMPR10 -- transmitter start trigger */
#define FE_B10_START	0x80	/* Start transmitter			*/
#define FE_B10_COUNT	0x7F	/* Packet count				*/

/* BMPR11 -- 16 collisions control */
#define FE_B11_CTRL	0x01	/* Skip or resend errored packets	*/
#define FE_B11_MODE1	0x02	/* Restart transmitter after COLL16	*/
#define FE_B11_MODE2	0x04	/* Automatic restart enable		*/

#define FE_B11_CTRL_RESEND	0x00	/* Re-send the collided packet	*/
#define FE_B11_CTRL_SKIP	0x01	/* Skip the collided packet	*/

/* BMPR12 -- DMA enable */
#define FE_B12_TXDMA	0x01	/* Enable transmitter DMA		*/
#define FE_B12_RXDMA	0x02	/* Enable receiver DMA			*/

/* BMPR13 -- DMA control */
#define FE_B13_BSTCTL	0x03	/* DMA burst mode control		*/
#define FE_B13_TPTYPE	0x04	/* Twisted pair cable impedance		*/
#define FE_B13_PORT	0x18	/* Port (TP/AUI) selection		*/
#define FE_B13_LNKTST	0x20	/* Link test enable			*/
#define FE_B13_SQTHLD	0x40	/* Lower squelch threshold		*/
#define FE_B13_IOUNLK	0x80	/* Change I/O base address, on JLI mode	*/

#define FE_B13_BSTCTL_1		0x00
#define FE_B13_BSTCTL_4		0x01
#define FE_B13_BSTCTL_8		0x02
#define FE_B13_BSTCLT_12	0x03

#define FE_B13_TPTYPE_UTP	0x00	/* Unshielded (standard) cable	*/
#define FE_B13_TPTYPE_STP	0x04	/* Shielded (IBM) cable		*/

#define FE_B13_PORT_AUTO	0x00	/* Auto detected		*/
#define FE_B13_PORT_TP		0x08	/* Force TP			*/
#define FE_B13_PORT_AUI		0x18	/* Force AUI			*/

/* BMPR14 -- More receiver control and more transmission interrupts */
#define FE_B14_FILTER	0x01	/* Filter out self-originated packets	*/
#define FE_B14_SQE	0x02	/* SQE interrupt enable			*/
#define FE_B14_SKIP	0x04	/* Skip a received packet		*/
#define FE_B14_RJAB	0x20	/* RJAB interrupt enable		*/
#define FE_B14_LLD	0x40	/* Local-link-down interrupt enable	*/
#define FE_B14_RLD	0x80	/* Remote-link-down interrupt enable	*/

/* BMPR15 -- More transmitter status; basically same layout as BMPR14 */
#define FE_B15_SQE	FE_B14_SQE
#define FE_B15_RCVPOL	0x08	/* Reversed receive line polarity	*/
#define FE_B15_RMTPRT	0x10	/* ???					*/
#define FE_B15_RAJB	FE_B14_RJAB
#define FE_B15_LLD	FE_B14_LLD
#define FE_B15_RLD	FE_B14_RLD

/* BMPR16 -- EEPROM control */
#define FE_B16_DOUT	0x04	/* EEPROM Data in (CPU to EEPROM)	*/
#define FE_B16_SELECT	0x20	/* EEPROM chip select			*/
#define FE_B16_CLOCK	0x40	/* EEPROM shift clock			*/
#define FE_B16_DIN	0x80	/* EEPROM data out (EEPROM to CPU)	*/

/* BMPR17 -- EEPROM data */
#define FE_B17_DATA	0x80	/* EEPROM data bit			*/

/* BMPR18 -- cycle I/O address setting in JLI mode */

/* BMPR19 -- ISA interface configuration in JLI mode */
#define FE_B19_IRQ		0xC0
#define FE_B19_IRQ_SHIFT	6

#define FE_B19_ROM		0x38
#define FE_B19_ROM_SHIFT	3

#define FE_B19_ADDR		0x07
#define FE_B19_ADDR_SHIFT	0

/*
 * An extra I/O port address to reset 86965.  This location is called
 * "ID ROM area" by Fujitsu document.
 */

/*
 * Flags in Receive Packet Header... Basically same layout as DLCR1.
 */
#define FE_RPH_OVRFLO	FE_D1_OVRFLO
#define FE_RPH_CRCERR	FE_D1_CRCERR
#define FE_RPH_ALGERR	FE_D1_ALGERR
#define FE_RPH_SRTPKT	FE_D1_SRTPKT
#define FE_RPH_RMTRST	FE_D1_RMTRST
#define FE_RPH_GOOD	0x20	/* Good packet follows			*/

/*
 * EEPROM specification (of JLI mode).
 */

/* Number of bytes in an EEPROM accessible through 86965.  */
#define FE_EEPROM_SIZE	32

/* Offset for JLI config; automatically copied into BMPR19 at startup.  */
#define FE_EEPROM_CONF	0

/*
 * Some 8696x specific constants.
 */

/* Length (in bytes) of a Multicast Address Filter.  */
#define FE_FILTER_LEN	8

/* How many packets we can put in the transmission buffer on NIC memory.  */
#define FE_QUEUEING_MAX 127

/* Length (in bytes) of a "packet length" word in transmission buffer.  */
#define FE_DATA_LEN_LEN 2

/* Special Multicast Address Filter value.  */
#define FE_FILTER_NOTHING	{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }
#define FE_FILTER_ALL		{ 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF }
