/*
 * bbc.h: Defines for BootBus Controller found on UltraSPARC-III
 *        systems.
 *
 * Copyright (C) 2000 David S. Miller (davem@redhat.com)
 */

#ifndef _SPARC64_BBC_H
#define _SPARC64_BBC_H

/* Register sizes are indicated by "B" (Byte, 1-byte),
 * "H" (Half-word, 2 bytes), "W" (Word, 4 bytes) or
 * "Q" (Quad, 8 bytes) inside brackets.
 */

#define BBC_AID		0x00	/* [B] Agent ID			*/
#define BBC_DEVP	0x01	/* [B] Device Present		*/
#define BBC_ARB		0x02	/* [B] Arbitration		*/
#define BBC_QUIESCE	0x03	/* [B] Quiesce			*/
#define BBC_WDACTION	0x04	/* [B] Watchdog Action		*/
#define BBC_SPG		0x06	/* [B] Soft POR Gen		*/
#define BBC_SXG		0x07	/* [B] Soft XIR Gen		*/
#define BBC_PSRC	0x08	/* [W] POR Source		*/
#define BBC_XSRC	0x0c	/* [B] XIR Source		*/
#define BBC_CSC		0x0d	/* [B] Clock Synthesizers Control*/
#define BBC_ES_CTRL	0x0e	/* [H] Energy Star Control	*/
#define BBC_ES_ACT	0x10	/* [W] E* Assert Change Time	*/
#define BBC_ES_DACT	0x14	/* [B] E* De-Assert Change Time	*/
#define BBC_ES_DABT	0x15	/* [B] E* De-Assert Bypass Time	*/
#define BBC_ES_ABT	0x16	/* [H] E* Assert Bypass Time	*/
#define BBC_ES_PST	0x18	/* [W] E* PLL Settle Time	*/
#define BBC_ES_FSL	0x1c	/* [W] E* Frequency Switch Latency*/
#define BBC_EBUST	0x20	/* [Q] EBUS Timing		*/
#define BBC_JTAG_CMD	0x28	/* [W] JTAG+ Command		*/
#define BBC_JTAG_CTRL	0x2c	/* [B] JTAG+ Control		*/
#define BBC_I2C_SEL	0x2d	/* [B] I2C Selection		*/
#define BBC_I2C_0_S1	0x2e	/* [B] I2C ctrlr-0 reg S1	*/
#define BBC_I2C_0_S0	0x2f	/* [B] I2C ctrlr-0 regs S0,S0',S2,S3*/
#define BBC_I2C_1_S1	0x30	/* [B] I2C ctrlr-1 reg S1	*/
#define BBC_I2C_1_S0	0x31	/* [B] I2C ctrlr-1 regs S0,S0',S2,S3*/
#define BBC_KBD_BEEP	0x32	/* [B] Keyboard Beep		*/
#define BBC_KBD_BCNT	0x34	/* [W] Keyboard Beep Counter	*/

#define BBC_REGS_SIZE	0x40

/* There is a 2K scratch ram area at offset 0x80000 but I doubt
 * we will use it for anything.
 */

/* Agent ID register.  This register shows the Safari Agent ID
 * for the processors.  The value returned depends upon which
 * cpu is reading the register.
 */
#define BBC_AID_ID	0x07	/* Safari ID		*/
#define BBC_AID_RESV	0xf8	/* Reserved		*/

/* Device Present register.  One can determine which cpus are actually
 * present in the machine by interrogating this register.
 */
#define BBC_DEVP_CPU0	0x01	/* Processor 0 present	*/
#define BBC_DEVP_CPU1	0x02	/* Processor 1 present	*/
#define BBC_DEVP_CPU2	0x04	/* Processor 2 present	*/
#define BBC_DEVP_CPU3	0x08	/* Processor 3 present	*/
#define BBC_DEVP_RESV	0xf0	/* Reserved		*/

/* Arbitration register.  This register is used to block access to
 * the BBC from a particular cpu.
 */
#define BBC_ARB_CPU0	0x01	/* Enable cpu 0 BBC arbitratrion */
#define BBC_ARB_CPU1	0x02	/* Enable cpu 1 BBC arbitratrion */
#define BBC_ARB_CPU2	0x04	/* Enable cpu 2 BBC arbitratrion */
#define BBC_ARB_CPU3	0x08	/* Enable cpu 3 BBC arbitratrion */
#define BBC_ARB_RESV	0xf0	/* Reserved			 */

/* Quiesce register.  Bus and BBC segments for cpus can be disabled
 * with this register, ie. for hot plugging.
 */
#define BBC_QUIESCE_S02	0x01	/* Quiesce Safari segment for cpu 0 and 2 */
#define BBC_QUIESCE_S13	0x02	/* Quiesce Safari segment for cpu 1 and 3 */
#define BBC_QUIESCE_B02	0x04	/* Quiesce BBC segment for cpu 0 and 2    */
#define BBC_QUIESCE_B13	0x08	/* Quiesce BBC segment for cpu 1 and 3    */
#define BBC_QUIESCE_FD0 0x10	/* Disable Fatal_Error[0] reporting	  */
#define BBC_QUIESCE_FD1 0x20	/* Disable Fatal_Error[1] reporting	  */
#define BBC_QUIESCE_FD2 0x40	/* Disable Fatal_Error[2] reporting	  */
#define BBC_QUIESCE_FD3 0x80	/* Disable Fatal_Error[3] reporting	  */

/* Watchdog Action register.  When the watchdog device timer expires
 * a line is enabled to the BBC.  The action BBC takes when this line
 * is asserted can be controlled by this regiser.
 */
#define BBC_WDACTION_RST  0x01	/* When set, watchdog causes system reset.
				 * When clear, BBC ignores watchdog signal.
				 */
#define BBC_WDACTION_RESV 0xfe	/* Reserved */

/* Soft_POR_GEN register.  The POR (Power On Reset) signal may be asserted
 * for specific processors or all processors via this register.
 */
#define BBC_SPG_CPU0	0x01 /* Assert POR for processor 0	*/
#define BBC_SPG_CPU1	0x02 /* Assert POR for processor 1	*/
#define BBC_SPG_CPU2	0x04 /* Assert POR for processor 2	*/
#define BBC_SPG_CPU3	0x08 /* Assert POR for processor 3	*/
#define BBC_SPG_CPUALL	0x10 /* Reset all processors and reset
			      * the entire system.
			      */
#define BBC_SPG_RESV	0xe0 /* Reserved			*/

/* Soft_XIR_GEN register.  The XIR (eXternally Initiated Reset) signal
 * may be asserted to specific processors via this register.
 */
#define BBC_SXG_CPU0	0x01 /* Assert XIR for processor 0	*/
#define BBC_SXG_CPU1	0x02 /* Assert XIR for processor 1	*/
#define BBC_SXG_CPU2	0x04 /* Assert XIR for processor 2	*/
#define BBC_SXG_CPU3	0x08 /* Assert XIR for processor 3	*/
#define BBC_SXG_RESV	0xf0 /* Reserved			*/

/* POR Source register.  One may identify the cause of the most recent
 * reset by reading this register.
 */
#define BBC_PSRC_SPG0	0x0001 /* CPU 0 reset via BBC_SPG register	*/
#define BBC_PSRC_SPG1	0x0002 /* CPU 1 reset via BBC_SPG register	*/
#define BBC_PSRC_SPG2	0x0004 /* CPU 2 reset via BBC_SPG register	*/
#define BBC_PSRC_SPG3	0x0008 /* CPU 3 reset via BBC_SPG register	*/
#define BBC_PSRC_SPGSYS	0x0010 /* System reset via BBC_SPG register	*/
#define BBC_PSRC_JTAG	0x0020 /* System reset via JTAG+		*/
#define BBC_PSRC_BUTTON	0x0040 /* System reset via push-button dongle	*/
#define BBC_PSRC_PWRUP	0x0080 /* System reset via power-up		*/
#define BBC_PSRC_FE0	0x0100 /* CPU 0 reported Fatal_Error		*/
#define BBC_PSRC_FE1	0x0200 /* CPU 1 reported Fatal_Error		*/
#define BBC_PSRC_FE2	0x0400 /* CPU 2 reported Fatal_Error		*/
#define BBC_PSRC_FE3	0x0800 /* CPU 3 reported Fatal_Error		*/
#define BBC_PSRC_FE4	0x1000 /* Schizo reported Fatal_Error		*/
#define BBC_PSRC_FE5	0x2000 /* Safari device 5 reported Fatal_Error	*/
#define BBC_PSRC_FE6	0x4000 /* CPMS reported Fatal_Error		*/
#define BBC_PSRC_SYNTH	0x8000 /* System reset when on-board clock synthesizers
				* were updated.
				*/
#define BBC_PSRC_WDT   0x10000 /* System reset via Super I/O watchdog	*/
#define BBC_PSRC_RSC   0x20000 /* System reset via RSC remote monitoring
				* device
				*/

/* XIR Source register.  The source of an XIR event sent to a processor may
 * be determined via this register.
 */
#define BBC_XSRC_SXG0	0x01	/* CPU 0 received XIR via Soft_XIR_GEN reg */
#define BBC_XSRC_SXG1	0x02	/* CPU 1 received XIR via Soft_XIR_GEN reg */
#define BBC_XSRC_SXG2	0x04	/* CPU 2 received XIR via Soft_XIR_GEN reg */
#define BBC_XSRC_SXG3	0x08	/* CPU 3 received XIR via Soft_XIR_GEN reg */
#define BBC_XSRC_JTAG	0x10	/* All CPUs received XIR via JTAG+         */
#define BBC_XSRC_W_OR_B	0x20	/* All CPUs received XIR either because:
				 * a) Super I/O watchdog fired, or
				 * b) XIR push button was activated
				 */
#define BBC_XSRC_RESV	0xc0	/* Reserved				   */

/* Clock Synthesizers Control register.  This register provides the big-bang
 * programming interface to the two clock synthesizers of the machine.
 */
#define BBC_CSC_SLOAD	0x01	/* Directly connected to S_LOAD pins	*/
#define BBC_CSC_SDATA	0x02	/* Directly connected to S_DATA pins	*/
#define BBC_CSC_SCLOCK	0x04	/* Directly connected to S_CLOCK pins	*/
#define BBC_CSC_RESV	0x78	/* Reserved				*/
#define BBC_CSC_RST	0x80	/* Generate system reset when S_LOAD==1	*/

/* Energy Star Control register.  This register is used to generate the
 * clock frequency change trigger to the main system devices (Schizo and
 * the processors).  The transition occurs when bits in this register
 * go from 0 to 1, only one bit must be set at once else no action
 * occurs.  Basically the sequence of events is:
 * a) Choose new frequency: full, 1/2 or 1/32
 * b) Program this desired frequency into the cpus and Schizo.
 * c) Set the same value in this register.
 * d) 16 system clocks later, clear this register.
 */
#define BBC_ES_CTRL_1_1		0x01	/* Full frequency	*/
#define BBC_ES_CTRL_1_2		0x02	/* 1/2 frequency	*/
#define BBC_ES_CTRL_1_32	0x20	/* 1/32 frequency	*/
#define BBC_ES_RESV		0xdc	/* Reserved		*/

/* Energy Star Assert Change Time register.  This determines the number
 * of BBC clock cycles (which is half the system frequency) between
 * the detection of FREEZE_ACK being asserted and the assertion of
 * the CLK_CHANGE_L[2:0] signals.
 */
#define BBC_ES_ACT_VAL	0xff

/* Energy Star Assert Bypass Time register.  This determines the number
 * of BBC clock cycles (which is half the system frequency) between
 * the assertion of the CLK_CHANGE_L[2:0] signals and the assertion of
 * the ESTAR_PLL_BYPASS signal.
 */
#define BBC_ES_ABT_VAL	0xffff

/* Energy Star PLL Settle Time register.  This determines the number of
 * BBC clock cycles (which is half the system frequency) between the
 * de-assertion of CLK_CHANGE_L[2:0] and the de-assertion of the FREEZE_L
 * signal.
 */
#define BBC_ES_PST_VAL	0xffffffff

/* Energy Star Frequency Switch Latency register.  This is the number of
 * BBC clocks between the de-assertion of CLK_CHANGE_L[2:0] and the first
 * edge of the Safari clock at the new frequency.
 */
#define BBC_ES_FSL_VAL	0xffffffff

/* Keyboard Beep control register.  This is a simple enabler for the audio
 * beep sound.
 */
#define BBC_KBD_BEEP_ENABLE	0x01 /* Enable beep	*/
#define BBC_KBD_BEEP_RESV	0xfe /* Reserved	*/

/* Keyboard Beep Counter register.  There is a free-running counter inside
 * the BBC which runs at half the system clock.  The bit set in this register
 * determines when the audio sound is generated.  So for example if bit
 * 10 is set, the audio beep will oscillate at 1/(2**12).  The keyboard beep
 * generator automatically selects a different bit to use if the system clock
 * is changed via Energy Star.
 */
#define BBC_KBD_BCNT_BITS	0x0007fc00
#define BBC_KBC_BCNT_RESV	0xfff803ff

#endif /* _SPARC64_BBC_H */

