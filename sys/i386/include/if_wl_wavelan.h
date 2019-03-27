/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain all copyright 
 *    notices, this list of conditions and the following disclaimer.
 * 2. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */
#ifndef	_CHIPS_WAVELAN_H
#define _CHIPS_WAVELAN_H

/* This file contains definitions that are common for all versions of
 * the NCR WaveLAN
 */

#define WAVELAN_ADDR_SIZE	6	/* Size of a MAC address */
#define WAVELAN_MTU		1500	/* Maximum size of Wavelan packet */

/* Modem Management Controler write commands */
#define MMC_ENCR_KEY		0x00	/* to 0x07 */
#define MMC_ENCR_ENABLE		0x08
#define MMC_DES_IO_INVERT	0x0a
#define MMC_LOOPT_SEL		0x10
#define MMC_JABBER_ENABLE	0x11
#define MMC_FREEZE		0x12
#define MMC_ANTEN_SEL		0x13
#define MMC_IFS			0x14
#define MMC_MOD_DELAY		0x15
#define MMC_JAM_TIME		0x16
#define MMC_THR_PRE_SET		0x18
#define MMC_DECAY_PRM		0x19
#define MMC_DECAY_UPDAT_PRM	0x1a
#define MMC_QUALITY_THR		0x1b
#define MMC_NETW_ID_L		0x1c
#define MMC_NETW_ID_H		0x1d
#define MMC_MODE_SEL		0x1e
#define	MMC_EECTRL		0x20	/* 2.4 Gz */
#define	MMC_EEADDR		0x21	/* 2.4 Gz */
#define MMC_EEDATAL		0x22	/* 2.4 Gz */
#define	MMC_EEDATAH		0x23	/* 2.4 Gz */
#define	MMC_ANALCTRL		0x24	/* 2.4 Gz */

/* fields in MMC registers that relate to EEPROM in WaveMODEM daughtercard */
#define MMC_EECTRL_EEPRE	0x10	/* 2.4 Gz EEPROM Protect Reg Enable */
#define MMC_EECTRL_DWLD		0x08	/* 2.4 Gz EEPROM Download Synths   */
#define	MMC_EECTRL_EEOP		0x07	/* 2.4 Gz EEPROM Opcode mask	 */
#define MMC_EECTRL_EEOP_READ	0x06	/* 2.4 Gz EEPROM Read Opcode	 */
#define	MMC_EEADDR_CHAN		0xf0	/* 2.4 Gz EEPROM Channel # mask	 */
#define	MMC_EEADDR_WDCNT	0x0f	/* 2.4 Gz EEPROM DNLD WordCount-1 */
#define	MMC_ANALCTRL_ANTPOL	0x02	/* 2.4 Gz Antenna Polarity mask	 */
#define	MMC_ANALCTRL_EXTANT	0x01	/* 2.4 Gz External Antenna mask	 */

/* MMC read register names */
#define MMC_DCE_STATUS		0x10
#define MMC_CORRECT_NWID_L	0x14
#define MMC_CORRECT_NWID_H	0x15
#define MMC_WRONG_NWID_L	0x16
#define MMC_WRONG_NWID_H	0x17
#define MMC_THR_PRE_SET		0x18
#define MMC_SIGNAL_LVL		0x19
#define MMC_SILENCE_LVL		0x1a
#define MMC_SIGN_QUAL		0x1b
#define MMC_DES_AVAIL		0x09
#define	MMC_EECTRLstat		0x20	/* 2.4 Gz  EEPROM r/w/dwld status */
#define	MMC_EEDATALrv		0x22	/* 2.4 Gz  EEPROM read value	  */
#define	MMC_EEDATAHrv		0x23	/* 2.4 Gz  EEPROM read value	  */

/* fields in MMC registers that relate to EEPROM in WaveMODEM daughtercard */
#define	MMC_EECTRLstat_ID24	0xf0	/* 2.4 Gz  =A0 rev-A, =B0 rev-B   */
#define	MMC_EECTRLstat_DWLD	0x08	/* 2.4 Gz  Synth/Tx-Pwr DWLD busy */
#define	MMC_EECTRLstat_EEBUSY	0x04	/* 2.4 Gz  EEPROM busy		  */

/* additional socket ioctl params for wl card   
 * see sys/sockio.h for numbers.  The 2nd params here
 * must be greater than any values in sockio.h
 */

#define SIOCGWLCNWID	_IOWR('i', 60, struct ifreq)	/* get wlan current nwid */
#define SIOCSWLCNWID	_IOWR('i', 61, struct ifreq)	/* set wlan current nwid */
#define SIOCGWLPSA	_IOWR('i', 62, struct ifreq)	/* get wlan PSA (all) */
#define SIOCSWLPSA	_IOWR('i', 63, struct ifreq)	/* set wlan PSA (all) */
#define	SIOCDWLCACHE	_IOW('i',  64, struct ifreq)	/* clear SNR cache    */
#define SIOCSWLTHR	_IOW('i',  65, struct ifreq)	/* set new quality threshold */
#define	SIOCGWLEEPROM	_IOWR('i', 66, struct ifreq)	/* get modem EEPROM   */
#define	SIOCGWLCACHE	_IOWR('i', 67, struct ifreq)	/* get SNR cache */
#define	SIOCGWLCITEM	_IOWR('i', 68, struct ifreq)	/* get cache element count */

/* PSA address definitions */
#define WLPSA_ID		0x0	/* ID byte (0 for ISA, 0x14 for MCA) */
#define WLPSA_IO1		0x1	/* I/O address 1 */
#define WLPSA_IO2		0x2	/* I/O address 2 */
#define WLPSA_IO3		0x3	/* I/O address 3 */
#define WLPSA_BR1		0x4	/* Bootrom address 1 */
#define WLPSA_BR2		0x5	/* Bootrom address 2 */
#define WLPSA_BR3		0x6	/* Bootrom address 3 */
#define WLPSA_HWCONF		0x7	/* HW config bits */
#define WLPSA_IRQNO		0x8	/* IRQ value */
#define WLPSA_UNIMAC		0x10	/* Universal MAC address */
#define WLPSA_LOCALMAC		0x16	/* Locally configured MAC address */
#define WLPSA_MACSEL		0x1c	/* MAC selector */
#define WLPSA_COMPATNO		0x1d	/* compatibility number */
#define WLPSA_THRESH		0x1e	/* RF modem threshold preset */
#define WLPSA_FEATSEL		0x1f	/* feature select */
#define WLPSA_SUBBAND		0x20	/* subband selector */
#define WLPSA_QUALTHRESH	0x21	/* RF modem quality threshold preset */
#define WLPSA_HWVERSION		0x22	/* hardware version indicator */
#define WLPSA_NWID		0x23	/* network ID */
#define WLPSA_NWIDENABLE	0x24	/* network ID enable */
#define WLPSA_SECURITY		0x25	/* datalink security enable */
#define WLPSA_DESKEY		0x26	/* datalink security DES key */
#define WLPSA_DBWIDTH		0x2f	/* databus width select */
#define WLPSA_CALLCODE		0x30	/* call code (japan only) */
#define WLPSA_CONFIGURED	0x3c	/* configuration status */
#define WLPSA_CRCLOW		0x3d	/* CRC-16 (lowbyte) */
#define WLPSA_CRCHIGH		0x3e	/*        (highbyte) */
#define WLPSA_CRCOK		0x3f	/* CRC OK flag */

#define WLPSA_COMPATNO_WL24B	0x04	/* 2.4 Gz WaveMODEM ISA rev-B  */

/* 
 * signal strength cache
 *
 * driver (wlp only at the moment) keeps cache of last
 * IP (only) packets to arrive including signal strength info.
 * daemons may read this with kvm.  See if_wlp.c for globals
 * that may be accessed through kvm.
 *
 * Each entry in the w_sigcache has a unique macsrc and age.
 * Each entry is identified by its macsrc field.
 * Age of the packet is identified by its age field.
 */

#define  MAXCACHEITEMS	10
#ifndef INT_MAX
#define        INT_MAX         2147483647
#endif
#define  MAX_AGE        (INT_MAX - MAXCACHEITEMS)

/* signal is 7 bits, 0..63, although it doesn't seem to get to 63.
 * silence is 7 bits, 0..63
 * quality is 4 bits, 0..15
 */
struct w_sigcache {
        char   macsrc[6]; /* unique MAC address for entry */
        int    ipsrc;     /* ip address associated with packet */
        int    signal;    /* signal strength of the packet */
        int    silence;   /* silence of the packet */
        int    quality;   /* quality of the packet */
        int    snr;       /* packet has unique age between 1 to MAX_AGE - 1 */
};

#endif /* _CHIPS_WAVELAN_H */

