/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 1999-2015, Broadcom Corporation
 * 
 * This file is derived from the bcmdevs.h header contributed by Broadcom 
 * to Android's bcmdhd driver module, later revisions of bcmdevs.h distributed
 * with the dd-wrt project, and the hndsoc.h header distributed with Broadcom's
 * initial brcm80211 Linux driver release as contributed to the Linux staging
 * repository.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _BHND_BHND_IDS_H_
#define _BHND_BHND_IDS_H_

/*
 * JEDEC JEP-106 Core Vendor IDs
 * 
 * These are the JEDEC JEP-106 manufacturer ID representions (with ARM's
 * non-standard 4-bit continutation code), as used in ARM's PrimeCell
 * identification registers, bcma(4) EROM core descriptors, etc.
 * 
 * @note
 * Bus implementations that predate the adoption of ARM IP
 * will need to convert bus-specific vendor IDs to their BHND_MFGID
 * JEP-106 equivalents.
 *
 * @par ARM 4-bit Continuation Code
 *
 * BHND MFGIDs are encoded using ARM's non-standard 4-bit continuation code
 * format:
 *
 * @code{.unparsed}
 * [11:8     ][7:0   ]
 * [cont code][mfg id]
 * @endcode
 *
 * The 4-bit continuation code field specifies the number of JEP-106
 * continuation codes that prefix the manufacturer's ID code. In the case of
 * ARM's JEP-106 ID of `0x7F 0x7F 0x7F 0x7F 0x3B`, the four 0x7F continuations
 * are encoded as '4' in the 4-bit continuation code field (i.e. 0x43B). 
 */
#define	BHND_MFGID_ARM		0x043b		/**< arm JEP-106 vendor id */
#define	BHND_MFGID_BCM		0x04bf		/**< broadcom JEP-106 vendor id */
#define	BHND_MFGID_MIPS		0x04a7		/**< mips JEP-106 vendor id */
#define	BHND_MFGID_INVALID	0x0000		/**< invalid JEP-106 vendor id */

/*
 * OCP (Open Core Protocol) Vendor IDs.
 * 
 * OCP-IP assigned vendor codes are used by siba(4)
 */
#define	OCP_VENDOR_BCM		0x4243		/**< Broadcom OCP vendor id */


/* PCI vendor IDs */
#define	PCI_VENDOR_ASUSTEK	0x1043
#define	PCI_VENDOR_EPIGRAM	0xfeda
#define	PCI_VENDOR_BROADCOM	0x14e4
#define	PCI_VENDOR_3COM		0x10b7
#define	PCI_VENDOR_NETGEAR	0x1385
#define	PCI_VENDOR_DIAMOND	0x1092
#define	PCI_VENDOR_INTEL	0x8086
#define	PCI_VENDOR_DELL		0x1028
#define	PCI_VENDOR_HP		0x103c
#define	PCI_VENDOR_HP_COMPAQ	0x0e11
#define	PCI_VENDOR_LINKSYS	0x1737
#define	PCI_VENDOR_MOTOROLA	0x1057
#define	PCI_VENDOR_APPLE	0x106b
#define	PCI_VENDOR_SI_IMAGE	0x1095		/* Silicon Image, used by Arasan SDIO Host */
#define	PCI_VENDOR_BUFFALO	0x1154		/* Buffalo vendor id */
#define	PCI_VENDOR_TI		0x104c		/* Texas Instruments */
#define	PCI_VENDOR_RICOH	0x1180		/* Ricoh */
#define	PCI_VENDOR_JMICRON	0x197b


/* PCMCIA vendor IDs */
#define	PCMCIA_VENDOR_BROADCOM	0x02d0


/* SDIO vendor IDs */
#define	SDIO_VENDOR_BROADCOM	0x00BF


/* USB dongle VID/PIDs */
#define	USB_VID_BROADCOM	0x0a5c
#define	USB_PID_BCM4328		0xbd12
#define	USB_PID_BCM4322		0xbd13
#define	USB_PID_BCM4319		0xbd16
#define	USB_PID_BCM43236	0xbd17
#define	USB_PID_BCM4332		0xbd18
#define	USB_PID_BCM4330		0xbd19
#define	USB_PID_BCM4334		0xbd1a
#define	USB_PID_BCM43239	0xbd1b
#define	USB_PID_BCM4324		0xbd1c
#define	USB_PID_BCM4360		0xbd1d
#define	USB_PID_BCM43143	0xbd1e
#define	USB_PID_BCM43242	0xbd1f
#define	USB_PID_BCM43342	0xbd21
#define	USB_PID_BCM4335		0xbd20
#define	USB_PID_BCM4350		0xbd23
#define	USB_PID_BCM43341	0xbd22

#define	USB_PID_BCM_DNGL_BDC	0x0bdc		/* BDC USB device controller IP? */
#define	USB_PID_BCM_DNGL_JTAG	0x4a44


/* HW USB BLOCK [CPULESS USB] PIDs */
#define	USB_PID_CCM_HWUSB_43239	43239


/* PCI Device IDs */
#define	PCI_DEVID_BCM4210		0x1072	/* never used */
#define	PCI_DEVID_BCM4230		0x1086	/* never used */
#define	PCI_DEVID_BCM4401_ENET		0x170c	/* 4401b0 production enet cards */
#define	PCI_DEVID_BCM3352		0x3352	/* bcm3352 device id */
#define	PCI_DEVID_BCM3360		0x3360	/* bcm3360 device id */
#define	PCI_DEVID_BCM4211		0x4211
#define	PCI_DEVID_BCM4231		0x4231
#define	PCI_DEVID_BCM4301		0x4301	/* 4031 802.11b */
#define	PCI_DEVID_BCM4303_D11B		0x4303	/* 4303 802.11b */
#define	PCI_DEVID_BCM4306		0x4306	/* 4306 802.11b/g */
#define	PCI_DEVID_BCM4307		0x4307	/* 4307 802.11b, 10/100 ethernet, V.92 modem */
#define	PCI_DEVID_BCM4311_D11G		0x4311	/* 4311 802.11b/g id */
#define	PCI_DEVID_BCM4311_D11DUAL	0x4312	/* 4311 802.11a/b/g id */
#define	PCI_DEVID_BCM4311_D11A		0x4313	/* 4311 802.11a id */
#define	PCI_DEVID_BCM4328_D11DUAL	0x4314	/* 4328/4312 802.11a/g id */
#define	PCI_DEVID_BCM4328_D11G		0x4315	/* 4328/4312 802.11g id */
#define	PCI_DEVID_BCM4328_D11A		0x4316	/* 4328/4312 802.11a id */
#define	PCI_DEVID_BCM4318_D11G		0x4318	/* 4318 802.11b/g id */
#define	PCI_DEVID_BCM4318_D11DUAL	0x4319	/* 4318 802.11a/b/g id */
#define	PCI_DEVID_BCM4318_D11A		0x431a	/* 4318 802.11a id */
#define	PCI_DEVID_BCM4325_D11DUAL	0x431b	/* 4325 802.11a/g id */
#define	PCI_DEVID_BCM4325_D11G		0x431c	/* 4325 802.11g id */
#define	PCI_DEVID_BCM4325_D11A		0x431d	/* 4325 802.11a id */
#define	PCI_DEVID_BCM4306_D11G		0x4320	/* 4306 802.11g */
#define	PCI_DEVID_BCM4306_D11A		0x4321	/* 4306 802.11a */
#define	PCI_DEVID_BCM4306_UART		0x4322	/* 4306 uart */
#define	PCI_DEVID_BCM4306_V90		0x4323	/* 4306 v90 codec */
#define	PCI_DEVID_BCM4306_D11DUAL	0x4324	/* 4306 dual A+B */
#define	PCI_DEVID_BCM4306_D11G_ID2	0x4325	/* BCM4306_D11G; INF w/loose binding war */
#define	PCI_DEVID_BCM4321_D11N		0x4328	/* 4321 802.11n dualband id */
#define	PCI_DEVID_BCM4321_D11N2G	0x4329	/* 4321 802.11n 2.4Ghz band id */
#define	PCI_DEVID_BCM4321_D11N5G	0x432a	/* 4321 802.11n 5Ghz band id */
#define	PCI_DEVID_BCM4322_D11N		0x432b	/* 4322 802.11n dualband device */
#define	PCI_DEVID_BCM4322_D11N2G	0x432c	/* 4322 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM4322_D11N5G	0x432d	/* 4322 802.11n 5GHz device */
#define	PCI_DEVID_BCM4329_D11N		0x432e	/* 4329 802.11n dualband device */
#define	PCI_DEVID_BCM4329_D11N2G	0x432f	/* 4329 802.11n 2.4G device */
#define	PCI_DEVID_BCM4329_D11N5G	0x4330	/* 4329 802.11n 5G device */
#define	PCI_DEVID_BCM4315_D11DUAL	0x4334	/* 4315 802.11a/g id */
#define	PCI_DEVID_BCM4315_D11G		0x4335	/* 4315 802.11g id */
#define	PCI_DEVID_BCM4315_D11A		0x4336	/* 4315 802.11a id */
#define	PCI_DEVID_BCM4319_D11N		0x4337	/* 4319 802.11n dualband device */
#define	PCI_DEVID_BCM4319_D11N2G	0x4338	/* 4319 802.11n 2.4G device */
#define	PCI_DEVID_BCM4319_D11N5G	0x4339	/* 4319 802.11n 5G device */
#define	PCI_DEVID_BCM43231_D11N2G	0x4340	/* 43231 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43221_D11N2G	0x4341	/* 43221 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43222_D11N		0x4350	/* 43222 802.11n dualband device */
#define	PCI_DEVID_BCM43222_D11N2G	0x4351	/* 43222 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43222_D11N5G	0x4352	/* 43222 802.11n 5GHz device */
#define	PCI_DEVID_BCM43224_D11N		0x4353	/* 43224 802.11n dualband device */
#define	PCI_DEVID_BCM43224_D11N_ID_VEN1	0x0576	/* Vendor specific 43224 802.11n db device */
#define	PCI_DEVID_BCM43226_D11N		0x4354	/* 43226 802.11n dualband device */
#define	PCI_DEVID_BCM43236_D11N		0x4346	/* 43236 802.11n dualband device */
#define	PCI_DEVID_BCM43236_D11N2G	0x4347	/* 43236 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43236_D11N5G	0x4348	/* 43236 802.11n 5GHz device */
#define	PCI_DEVID_BCM43225_D11N2G	0x4357	/* 43225 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43421_D11N		0xA99D	/* 43421 802.11n dualband device */
#define	PCI_DEVID_BCM4313_D11N2G	0x4727	/* 4313 802.11n 2.4G device */
#define	PCI_DEVID_BCM4330_D11N		0x4360	/* 4330 802.11n dualband device */
#define	PCI_DEVID_BCM4330_D11N2G	0x4361	/* 4330 802.11n 2.4G device */
#define	PCI_DEVID_BCM4330_D11N5G	0x4362	/* 4330 802.11n 5G device */
#define	PCI_DEVID_BCM4336_D11N		0x4343	/* 4336 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM6362_D11N		0x435f	/* 6362 802.11n dualband device */
#define	PCI_DEVID_BCM6362_D11N2G	0x433f	/* 6362 802.11n 2.4Ghz band id */
#define	PCI_DEVID_BCM6362_D11N5G	0x434f	/* 6362 802.11n 5Ghz band id */
#define	PCI_DEVID_BCM4331_D11N		0x4331	/* 4331 802.11n dualband id */
#define	PCI_DEVID_BCM4331_D11N2G	0x4332	/* 4331 802.11n 2.4Ghz band id */
#define	PCI_DEVID_BCM4331_D11N5G	0x4333	/* 4331 802.11n 5Ghz band id */
#define	PCI_DEVID_BCM43237_D11N		0x4355	/* 43237 802.11n dualband device */
#define	PCI_DEVID_BCM43237_D11N5G	0x4356	/* 43237 802.11n 5GHz device */
#define	PCI_DEVID_BCM43227_D11N2G	0x4358	/* 43228 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43228_D11N		0x4359	/* 43228 802.11n DualBand device */
#define	PCI_DEVID_BCM43228_D11N5G	0x435a	/* 43228 802.11n 5GHz device */
#define	PCI_DEVID_BCM43362_D11N		0x4363	/* 43362 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43239_D11N		0x4370	/* 43239 802.11n dualband device */
#define	PCI_DEVID_BCM4324_D11N		0x4374	/* 4324 802.11n dualband device */
#define	PCI_DEVID_BCM43217_D11N2G	0x43a9	/* 43217 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM43131_D11N2G	0x43aa	/* 43131 802.11n 2.4GHz device */
#define	PCI_DEVID_BCM4314_D11N2G	0x4364	/* 4314 802.11n 2.4G device */
#define	PCI_DEVID_BCM43142_D11N2G	0x4365	/* 43142 802.11n 2.4G device */
#define	PCI_DEVID_BCM43143_D11N2G	0x4366	/* 43143 802.11n 2.4G device */
#define	PCI_DEVID_BCM4334_D11N		0x4380	/* 4334 802.11n dualband device */
#define	PCI_DEVID_BCM4334_D11N2G	0x4381	/* 4334 802.11n 2.4G device */
#define	PCI_DEVID_BCM4334_D11N5G	0x4382	/* 4334 802.11n 5G device */
#define	PCI_DEVID_BCM43342_D11N		0x4383	/* 43342 802.11n dualband device */
#define	PCI_DEVID_BCM43342_D11N2G	0x4384	/* 43342 802.11n 2.4G device */
#define	PCI_DEVID_BCM43342_D11N5G	0x4385	/* 43342 802.11n 5G device */
#define	PCI_DEVID_BCM43341_D11N		0x4386	/* 43341 802.11n dualband device */
#define	PCI_DEVID_BCM43341_D11N2G	0x4387	/* 43341 802.11n 2.4G device */
#define	PCI_DEVID_BCM43341_D11N5G	0x4388	/* 43341 802.11n 5G device */
#define	PCI_DEVID_BCM4360_D11AC		0x43a0
#define	PCI_DEVID_BCM4360_D11AC2G	0x43a1
#define	PCI_DEVID_BCM4360_D11AC5G	0x43a2
#define	PCI_DEVID_BCM4335_D11AC		0x43ae
#define	PCI_DEVID_BCM4335_D11AC2G	0x43af
#define	PCI_DEVID_BCM4335_D11AC5G	0x43b0
#define	PCI_DEVID_BCM4352_D11AC		0x43b1	/* 4352 802.11ac dualband device */
#define	PCI_DEVID_BCM4352_D11AC2G	0x43b2	/* 4352 802.11ac 2.4G device */
#define	PCI_DEVID_BCM4352_D11AC5G	0x43b3	/* 4352 802.11ac 5G device */

#define	PCI_DEVID_PCIXX21_FLASHMEDIA0	0x8033	/* TI PCI xx21 Standard Host Controller */
#define	PCI_DEVID_PCIXX21_SDIOH0	0x8034	/* TI PCI xx21 Standard Host Controller */


/* PCI Subsystem Vendor IDs */
#define	PCI_SUBVENDOR_BCM943228HMB	0x0607
#define	PCI_SUBVENDOR_BCM94313HMGBL	0x0608
#define	PCI_SUBVENDOR_BCM94313HMG	0x0609
#define	PCI_SUBVENDOR_BCM943142HM	0x0611


/* PCI Subsystem Device IDs */
#define	PCI_SUBDEVID_BCM43143_D11N2G		0x4366	/* 43143 802.11n 2.4G device */

#define	PCI_SUBDEVID_BCM43242_D11N		0x4367	/* 43242 802.11n dualband device */
#define	PCI_SUBDEVID_BCM43242_D11N2G		0x4368	/* 43242 802.11n 2.4G device */
#define	PCI_SUBDEVID_BCM43242_D11N5G		0x4369	/* 43242 802.11n 5G device */

#define	PCI_SUBDEVID_BCM4350_D11AC		0x43a3
#define	PCI_SUBDEVID_BCM4350_D11AC2G		0x43a4
#define	PCI_SUBDEVID_BCM4350_D11AC5G		0x43a5

#define	PCI_SUBDEVID_BCMGPRS_UART		0x4333	/* Uart id used by 4306/gprs card */
#define	PCI_SUBDEVID_BCMGPRS2_UART		0x4344	/* Uart id used by 4306/gprs card */
#define	PCI_SUBDEVID_BCM_FPGA_JTAGM		0x43f0	/* FPGA jtagm device id */
#define	PCI_SUBDEVID_BCM_JTAGM			0x43f1	/* BCM jtagm device id */
#define	PCI_SUBDEVID_BCM_SDIOH_FPGA		0x43f2	/* sdio host fpga */
#define	PCI_SUBDEVID_BCM_SDIOH			0x43f3	/* BCM sdio host id */
#define	PCI_SUBDEVID_BCM_SDIOD_FPGA		0x43f4	/* sdio device fpga */
#define	PCI_SUBDEVID_BCM_SPIH_FPGA		0x43f5	/* PCI SPI Host Controller FPGA */
#define	PCI_SUBDEVID_BCM_SPIH			0x43f6	/* Synopsis SPI Host Controller */
#define	PCI_SUBDEVID_BCM_MIMO_FPGA		0x43f8	/* FPGA mimo minimacphy device id */
#define	PCI_SUBDEVID_BCM_JTAGM2			0x43f9	/* PCI_SUBDEVID_BCM alternate jtagm device id */
#define	PCI_SUBDEVID_BCM_SDHCI_FPGA		0x43fa	/* Standard SDIO Host Controller FPGA */
#define	PCI_SUBDEVID_BCM4402_ENET		0x4402	/* 4402 enet */
#define	PCI_SUBDEVID_BCM4402_V90		0x4403	/* 4402 v90 codec */
#define	PCI_SUBDEVID_BCM4410			0x4410	/* bcm44xx family pci iline */
#define	PCI_SUBDEVID_BCM4412			0x4412	/* bcm44xx family pci enet */
#define	PCI_SUBDEVID_BCM4430			0x4430	/* bcm44xx family cardbus iline */
#define	PCI_SUBDEVID_BCM4432			0x4432	/* bcm44xx family cardbus enet */
#define	PCI_SUBDEVID_BCM4704_ENET		0x4706	/* 4704 enet (Use 47XX_ENET_ID instead!) */
#define	PCI_SUBDEVID_BCM4710			0x4710	/* 4710 primary function 0 */
#define	PCI_SUBDEVID_BCM47XX_AUDIO		0x4711	/* 47xx audio codec */
#define	PCI_SUBDEVID_BCM47XX_V90		0x4712	/* 47xx v90 codec */
#define	PCI_SUBDEVID_BCM47XX_ENET		0x4713	/* 47xx enet */
#define	PCI_SUBDEVID_BCM47XX_EXT		0x4714	/* 47xx external i/f */
#define	PCI_SUBDEVID_BCM47XX_GMAC		0x4715	/* 47xx Unimac based GbE */
#define	PCI_SUBDEVID_BCM47XX_USBH		0x4716	/* 47xx usb host */
#define	PCI_SUBDEVID_BCM47XX_USBD		0x4717	/* 47xx usb device */
#define	PCI_SUBDEVID_BCM47XX_IPSEC		0x4718	/* 47xx ipsec */
#define	PCI_SUBDEVID_BCM47XX_ROBO		0x4719	/* 47xx/53xx roboswitch core */
#define	PCI_SUBDEVID_BCM47XX_USB20H		0x471a	/* 47xx usb 2.0 host */
#define	PCI_SUBDEVID_BCM47XX_USB20D		0x471b	/* 47xx usb 2.0 device */
#define	PCI_SUBDEVID_BCM47XX_ATA100		0x471d	/* 47xx parallel ATA */
#define	PCI_SUBDEVID_BCM47XX_SATAXOR		0x471e	/* 47xx serial ATA & XOR DMA */
#define	PCI_SUBDEVID_BCM47XX_GIGETH		0x471f	/* 47xx GbE (5700) */
#define	PCI_SUBDEVID_BCM4712_MIPS		0x4720	/* 4712 base devid */
#define	PCI_SUBDEVID_BCM4716			0x4722	/* 4716 base devid */
#define	PCI_SUBDEVID_BCM47XX_USB30H		0x472a	/* 47xx usb 3.0 host */
#define	PCI_SUBDEVID_BCM47XX_USB30D		0x472b	/* 47xx usb 3.0 device */
#define	PCI_SUBDEVID_BCM47XX_SMBUS_EMU		0x47fe	/* 47xx emulated SMBus device */
#define	PCI_SUBDEVID_BCM47XX_XOR_EMU		0x47ff	/* 47xx emulated XOR engine */
#define	PCI_SUBDEVID_BCM_EPI41210		0xa0fa	/* bcm4210 */
#define	PCI_SUBDEVID_BCM_EPI41230		0xa10e	/* bcm4230 */
#define	PCI_SUBDEVID_BCM_JINVANI_SDIOH		0x4743	/* Jinvani SDIO Gold Host */
#define	PCI_SUBDEVID_BCM27XX_SDIOH		0x2702	/* PCI_SUBDEVID_BCM27xx Standard SDIO Host */
#define	PCI_SUBDEVID_BCM_PCIXX21_FLASHMEDIA	0x803b	/* TI PCI xx21 Standard Host Controller */
#define	PCI_SUBDEVID_BCM_PCIXX21_SDIOH		0x803c	/* TI PCI xx21 Standard Host Controller */
#define	PCI_SUBDEVID_BCM_R5C822_SDIOH		0x0822	/* Ricoh Co Ltd R5C822 SD/SDIO/MMC/MS/MSPro Host */
#define	PCI_SUBDEVID_BCM_JMICRON_SDIOH		0x2381	/* JMicron Standard SDIO Host Controller */


/* Broadcom ChipCommon Chip IDs */
#define	BHND_CHIPID_BCM4306		0x4306		/* 4306 chipcommon chipid */
#define	BHND_CHIPID_BCM4311		0x4311		/* 4311 PCIe 802.11a/b/g */
#define	BHND_CHIPID_BCM43111		43111		/* 43111 chipcommon chipid (OTP chipid) */
#define	BHND_CHIPID_BCM43112		43112		/* 43112 chipcommon chipid (OTP chipid) */
#define	BHND_CHIPID_BCM4312		0x4312		/* 4312 chipcommon chipid */
#define	BHND_CHIPID_BCM4313		0x4313		/* 4313 chip id */
#define	BHND_CHIPID_BCM43131		43131		/* 43131 chip id (OTP chipid) */
#define	BHND_CHIPID_BCM4315		0x4315		/* 4315 chip id */
#define	BHND_CHIPID_BCM4318		0x4318		/* 4318 chipcommon chipid */
#define	BHND_CHIPID_BCM4319		0x4319		/* 4319 chip id */
#define	BHND_CHIPID_BCM4320		0x4320		/* 4320 chipcommon chipid */
#define	BHND_CHIPID_BCM4321		0x4321		/* 4321 chipcommon chipid */
#define	BHND_CHIPID_BCM43217		43217		/* 43217 chip id (OTP chipid) */
#define	BHND_CHIPID_BCM4322		0x4322		/* 4322 chipcommon chipid */
#define	BHND_CHIPID_BCM43221		43221		/* 43221 chipcommon chipid (OTP chipid) */
#define	BHND_CHIPID_BCM43222		43222		/* 43222 chipcommon chipid */
#define	BHND_CHIPID_BCM43224		43224		/* 43224 chipcommon chipid */
#define	BHND_CHIPID_BCM43225		43225		/* 43225 chipcommon chipid */
#define	BHND_CHIPID_BCM43227		43227		/* 43227 chipcommon chipid */
#define	BHND_CHIPID_BCM43228		43228		/* 43228 chipcommon chipid */
#define	BHND_CHIPID_BCM43226		43226		/* 43226 chipcommon chipid */
#define	BHND_CHIPID_BCM43231		43231		/* 43231 chipcommon chipid (OTP chipid) */
#define	BHND_CHIPID_BCM43234		43234		/* 43234 chipcommon chipid */
#define	BHND_CHIPID_BCM43235		43235		/* 43235 chipcommon chipid */
#define	BHND_CHIPID_BCM43236		43236		/* 43236 chipcommon chipid */
#define	BHND_CHIPID_BCM43237		43237		/* 43237 chipcommon chipid */
#define	BHND_CHIPID_BCM43238		43238		/* 43238 chipcommon chipid */
#define	BHND_CHIPID_BCM43239		43239		/* 43239 chipcommon chipid */
#define	BHND_CHIPID_BCM43420		43420		/* 43222 chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM43421		43421		/* 43224 chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM43428		43428		/* 43228 chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM43431		43431		/* 4331  chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM43460		43460		/* 4360  chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM43462		0xA9C6		/* 43462 chipcommon chipid */
#define	BHND_CHIPID_BCM4325		0x4325		/* 4325 chip id */
#define	BHND_CHIPID_BCM4328		0x4328		/* 4328 chip id */
#define	BHND_CHIPID_BCM4329		0x4329		/* 4329 chipcommon chipid */
#define	BHND_CHIPID_BCM4331		0x4331		/* 4331 chipcommon chipid */
#define	BHND_CHIPID_BCM4336		0x4336		/* 4336 chipcommon chipid */
#define	BHND_CHIPID_BCM43362		43362		/* 43362 chipcommon chipid */
#define	BHND_CHIPID_BCM4330		0x4330		/* 4330 chipcommon chipid */
#define	BHND_CHIPID_BCM6362		0x6362		/* 6362 chipcommon chipid */
#define	BHND_CHIPID_BCM4314		0x4314		/* 4314 chipcommon chipid */
#define	BHND_CHIPID_BCM43142		43142		/* 43142 chipcommon chipid */
#define	BHND_CHIPID_BCM43143		43143		/* 43143 chipcommon chipid */
#define	BHND_CHIPID_BCM4324		0x4324		/* 4324 chipcommon chipid */
#define	BHND_CHIPID_BCM43242		43242		/* 43242 chipcommon chipid */
#define	BHND_CHIPID_BCM43243		43243		/* 43243 chipcommon chipid */
#define	BHND_CHIPID_BCM4334		0x4334		/* 4334 chipcommon chipid */
#define	BHND_CHIPID_BCM4335		0x4335		/* 4335 chipcommon chipid */
#define	BHND_CHIPID_BCM4360		0x4360          /* 4360 chipcommon chipid */
#define	BHND_CHIPID_BCM43602		0xaa52          /* 43602 chipcommon chipid */
#define	BHND_CHIPID_BCM4352		0x4352          /* 4352 chipcommon chipid */
#define	BHND_CHIPID_BCM43526		0xAA06
#define	BHND_CHIPID_BCM43341		43341		/* 43341 chipcommon chipid */
#define	BHND_CHIPID_BCM43342		43342		/* 43342 chipcommon chipid */
#define	BHND_CHIPID_BCM4335		0x4335
#define	BHND_CHIPID_BCM4350		0x4350          /* 4350 chipcommon chipid */

#define	BHND_CHIPID_BCM4342		4342		/* 4342 chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM4402		0x4402		/* 4402 chipid */
#define	BHND_CHIPID_BCM4704		0x4704		/* 4704 chipcommon chipid */
#define	BHND_CHIPID_BCM4706		0x5300		/* 4706 chipcommon chipid */
#define	BHND_CHIPID_BCM4707		53010		/* 4707 chipcommon chipid */
#define	BHND_CHIPID_BCM53018		53018		/* 53018 chipcommon chipid */
#define	BHND_CHIPID_IS_BCM4707(chipid) \
	(((chipid) == BHND_CHIPID_BCM4707) || \
	((chipid) == BHND_CHIPID_BCM53018))
#define	BHND_CHIPID_BCM4710		0x4710		/* 4710 chipid */
#define	BHND_CHIPID_BCM4712		0x4712		/* 4712 chipcommon chipid */
#define	BHND_CHIPID_BCM4716		0x4716		/* 4716 chipcommon chipid */
#define	BHND_CHIPID_BCM47162		47162		/* 47162 chipcommon chipid */
#define	BHND_CHIPID_BCM4748		0x4748		/* 4716 chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM4749		0x4749		/* 5357 chipcommon chipid (OTP, RBBU) */
#define	BHND_CHIPID_BCM4785		0x4785		/* 4785 chipcommon chipid */
#define	BHND_CHIPID_BCM5350		0x5350		/* 5350 chipcommon chipid */
#define	BHND_CHIPID_BCM5352		0x5352		/* 5352 chipcommon chipid */
#define	BHND_CHIPID_BCM5354		0x5354		/* 5354 chipcommon chipid */
#define	BHND_CHIPID_BCM5365		0x5365		/* 5365 chipcommon chipid */
#define	BHND_CHIPID_BCM5356		0x5356		/* 5356 chipcommon chipid */
#define	BHND_CHIPID_BCM5357		0x5357		/* 5357 chipcommon chipid */
#define	BHND_CHIPID_BCM53572		53572		/* 53572 chipcommon chipid */


/* Broadcom ChipCommon Package IDs */
#define	BHND_PKGID_BCM4303		2		/* 4303 package id */
#define	BHND_PKGID_BCM4309		1		/* 4309 package id */
#define	BHND_PKGID_BCM4712LARGE		0		/* 340pin 4712 package id */
#define	BHND_PKGID_BCM4712SMALL		1		/* 200pin 4712 package id */
#define	BHND_PKGID_BCM4712MID		2		/* 225pin 4712 package id */
#define	BHND_PKGID_BCM4328USBD11G	2		/* 4328 802.11g USB package id */
#define	BHND_PKGID_BCM4328USBDUAL	3		/* 4328 802.11a/g USB package id */
#define	BHND_PKGID_BCM4328SDIOD11G	4		/* 4328 802.11g SDIO package id */
#define	BHND_PKGID_BCM4328SDIODUAL	5		/* 4328 802.11a/g SDIO package id */
#define	BHND_PKGID_BCM4329_289PIN	0		/* 4329 289-pin package id */
#define	BHND_PKGID_BCM4329_182PIN	1		/* 4329N 182-pin package id */
#define	BHND_PKGID_BCM5354E		1		/* 5354E package id */
#define	BHND_PKGID_BCM4716		8		/* 4716 package id */
#define	BHND_PKGID_BCM4717		9		/* 4717 package id */
#define	BHND_PKGID_BCM4718		10		/* 4718 package id */
#define	BHND_PKGID_BCM5356_NONMODE	1		/* 5356 package without nmode suppport */
#define	BHND_PKGID_BCM5358U		8		/* 5358U package id */
#define	BHND_PKGID_BCM5358		9		/* 5358 package id */
#define	BHND_PKGID_BCM47186		10		/* 47186 package id */
#define	BHND_PKGID_BCM5357		11		/* 5357 package id */
#define	BHND_PKGID_BCM5356U		12		/* 5356U package id */
#define	BHND_PKGID_BCM53572		8		/* 53572 package id */
#define	BHND_PKGID_BCM5357C0		8		/* 5357c0 package id (the same as 53572) */
#define	BHND_PKGID_BCM47188		9		/* 47188 package id */
#define	BHND_PKGID_BCM5358C0		0xa		/* 5358c0 package id */
#define	BHND_PKGID_BCM5356C0		0xb		/* 5356c0 package id */
#define	BHND_PKGID_BCM4331TT		8		/* 4331 12x12 package id */
#define	BHND_PKGID_BCM4331TN		9		/* 4331 12x9 package id */
#define	BHND_PKGID_BCM4331TNA0		0xb		/* 4331 12x9 package id */
#define	BHND_PKGID_BCM4706L		1		/* 4706L package id */

#define	BHND_PKGID_HDLSIM5350		1		/* HDL simulator package id for a 5350 */
#define	BHND_PKGID_HDLSIM		14		/* HDL simulator package id */
#define	BHND_PKGID_HWSIM		15		/* Hardware simulator package id */
#define	BHND_PKGID_BCM43224_FAB_CSM	0x8		/* the chip is manufactured by CSM */
#define	BHND_PKGID_BCM43224_FAB_SMIC	0xa		/* the chip is manufactured by SMIC */
#define	BHND_PKGID_BCM4336_WLBGA	0x8
#define	BHND_PKGID_BCM4330_WLBGA	0x0
#define	BHND_PKGID_BCM4314PCIE_ARM	(8 | 0)		/* 4314 QFN PCI package id, bit 3 tie high */
#define	BHND_PKGID_BCM4314SDIO		(8 | 1)		/* 4314 QFN SDIO package id */
#define	BHND_PKGID_BCM4314PCIE		(8 | 2)		/* 4314 QFN PCI (ARM-less) package id */
#define	BHND_PKGID_BCM4314SDIO_ARM	(8 | 3)		/* 4314 QFN SDIO (ARM-less) package id */
#define	BHND_PKGID_BCM4314SDIO_FPBGA	(8 | 4)		/* 4314 FpBGA SDIO package id */
#define	BHND_PKGID_BCM4314DEV		(8 | 6)		/* 4314 Development package id */

#define	BHND_PKGID_BCM4707		1		/* 4707 package id */
#define	BHND_PKGID_BCM4708		2		/* 4708 package id */
#define	BHND_PKGID_BCM4709		0		/* 4709 package id */

#define	BHND_PKGID_BCM4335_WLCSP	(0x0)		/* WLCSP Module/Mobile SDIO/HSIC. */
#define	BHND_PKGID_BCM4335_FCBGA	(0x1)		/* FCBGA PC/Embedded/Media PCIE/SDIO */
#define	BHND_PKGID_BCM4335_WLBGA	(0x2)		/* WLBGA COB/Mobile SDIO/HSIC. */
#define	BHND_PKGID_BCM4335_FCBGAD	(0x3)		/* FCBGA Debug Debug/Dev All if's. */
#define	BHND_PKGID_PKG_MASK_BCM4335	(0x3)

/* Broadcom Core IDs */
#define	BHND_COREID_INVALID		0x700		/* Invalid coreid */
#define	BHND_COREID_CC			0x800		/* chipcommon core */
#define	BHND_COREID_ILINE20		0x801		/* iline20 core */
#define	BHND_COREID_SRAM		0x802		/* sram core */
#define	BHND_COREID_SDRAM		0x803		/* sdram core */
#define	BHND_COREID_PCI			0x804		/* pci core */
#define	BHND_COREID_MIPS		0x805		/* mips core */
#define	BHND_COREID_ENET		0x806		/* enet mac core */
#define	BHND_COREID_V90_CODEC		0x807		/* v90 codec core */
#define	BHND_COREID_USB			0x808		/* usb 1.1 host/device core */
#define	BHND_COREID_ADSL		0x809		/* ADSL core */
#define	BHND_COREID_ILINE100		0x80a		/* iline100 core */
#define	BHND_COREID_IPSEC		0x80b		/* ipsec core */
#define	BHND_COREID_UTOPIA		0x80c		/* utopia core */
#define	BHND_COREID_PCMCIA		0x80d		/* pcmcia core */
#define	BHND_COREID_SOCRAM		0x80e		/* internal memory core */
#define	BHND_COREID_MEMC		0x80f		/* memc sdram core */
#define	BHND_COREID_OFDM		0x810		/* OFDM phy core */
#define	BHND_COREID_EXTIF		0x811		/* external interface core */
#define	BHND_COREID_D11			0x812		/* 802.11 MAC core */
#define	BHND_COREID_APHY		0x813		/* 802.11a phy core */
#define	BHND_COREID_BPHY		0x814		/* 802.11b phy core */
#define	BHND_COREID_GPHY		0x815		/* 802.11g phy core */
#define	BHND_COREID_MIPS33		0x816		/* mips3302 core */
#define	BHND_COREID_USB11H		0x817		/* usb 1.1 host core */
#define	BHND_COREID_USB11D		0x818		/* usb 1.1 device core */
#define	BHND_COREID_USB20H		0x819		/* usb 2.0 host core */
#define	BHND_COREID_USB20D		0x81a		/* usb 2.0 device core */
#define	BHND_COREID_SDIOH		0x81b		/* sdio host core */
#define	BHND_COREID_ROBO		0x81c		/* roboswitch core */
#define	BHND_COREID_ATA100		0x81d		/* parallel ATA core */
#define	BHND_COREID_SATAXOR		0x81e		/* serial ATA & XOR DMA core */
#define	BHND_COREID_GIGETH		0x81f		/* gigabit ethernet core */
#define	BHND_COREID_PCIE		0x820		/* pci express core */
#define	BHND_COREID_NPHY		0x821		/* 802.11n 2x2 phy core */
#define	BHND_COREID_SRAMC		0x822		/* SRAM controller core */
#define	BHND_COREID_MINIMAC		0x823		/* MINI MAC/phy core */
#define	BHND_COREID_ARM11		0x824		/* ARM 1176 core */
#define	BHND_COREID_ARM7S		0x825		/* ARM7tdmi-s core */
#define	BHND_COREID_LPPHY		0x826		/* 802.11a/b/g phy core */
#define	BHND_COREID_PMU			0x827		/* PMU core */
#define	BHND_COREID_SSNPHY		0x828		/* 802.11n single-stream phy core */
#define	BHND_COREID_SDIOD		0x829		/* SDIO device core */
#define	BHND_COREID_ARMCM3		0x82a		/* ARM Cortex M3 core */
#define	BHND_COREID_HTPHY		0x82b		/* 802.11n 4x4 phy core */
#define	BHND_COREID_MIPS74K		0x82c		/* mips 74k core */
#define	BHND_COREID_GMAC		0x82d		/* Gigabit MAC core */
#define	BHND_COREID_DMEMC		0x82e		/* DDR1/2 memory controller core */
#define	BHND_COREID_PCIERC		0x82f		/* PCIE Root Complex core */
#define	BHND_COREID_OCP			0x830		/* OCP2OCP bridge core */
#define	BHND_COREID_SC			0x831		/* shared common core */
#define	BHND_COREID_AHB			0x832		/* OCP2AHB bridge core */
#define	BHND_COREID_SPIH		0x833		/* SPI host core */
#define	BHND_COREID_I2S			0x834		/* I2S core */
#define	BHND_COREID_DMEMS		0x835		/* SDR/DDR1 memory controller core */
#define	BHND_COREID_UBUS_SHIM		0x837		/* SHIM component in ubus/6362 */
#define	BHND_COREID_PCIE2		0x83c		/* pci express (gen2) core */
/* ARM/AMBA Core IDs */
#define	BHND_COREID_APB_BRIDGE		0x135		/* BP135 AMBA AXI-APB bridge */
#define	BHND_COREID_PL301		0x301		/* PL301 AMBA AXI Interconnect */
#define	BHND_COREID_EROM		0x366		/* Enumeration ROM */
#define	BHND_COREID_OOB_ROUTER		0x367		/* OOB router core ID */
#define	BHND_COREID_AXI_UNMAPPED	0xfff		/* AXI "Default Slave"; maps all unused address
							 * ranges, returning DECERR on read or write. */
/* Northstar Plus and BCM4706 Core IDs */
#define	BHND_COREID_4706_CC		0x500		/* chipcommon core */
#define	BHND_COREID_NS_PCIE2		0x501		/* pci express (gen2) core */
#define	BHND_COREID_NS_DMA		0x502		/* dma core */
#define	BHND_COREID_NS_SDIO		0x503		/* sdio host core */
#define	BHND_COREID_NS_USB20H		0x504		/* usb 2.0 host core */
#define	BHND_COREID_NS_USB30H		0x505		/* usb 3.0 host core */
#define	BHND_COREID_NS_A9JTAG		0x506		/* ARM Cortex A9 JTAG core */
#define	BHND_COREID_NS_DDR23_MEMC	0x507		/* DDR2/3 cadence/denali memory controller core () */
#define	BHND_COREID_NS_ROM		0x508		/* device ROM core */
#define	BHND_COREID_NS_NAND		0x509		/* NAND flash controller core */
#define	BHND_COREID_NS_QSPI		0x50a		/* QSPI flash controller core */
#define	BHND_COREID_NS_CC_B		0x50b		/* chipcommon `b' (auxiliary) core */
#define	BHND_COREID_4706_SOCRAM		0x50e		/* internal memory core */
#define	BHND_COREID_IHOST_ARMCA9	0x510		/* ARM Cortex A9 core */
#define	BHND_COREID_4706_GMAC_CMN	0x5dc		/* Gigabit MAC common core */
#define	BHND_COREID_4706_GMAC		0x52d           /* Gigabit MAC core */
#define	BHND_COREID_AMEMC		0x52e           /* DDR1/2 cadence/denali memory controller core */



/* ARM PrimeCell Peripherial IDs. These were derived from inspection of the
 * PrimeCell-compatible BCM4331 cores, but due to lack of documentation, the
 * surmised core name/description may be incorrect. */
#define	BHND_PRIMEID_EROM		0x364		/* Enumeration ROM's primecell ID */
#define	BHND_PRIMEID_SWRAP		0x368		/* PL368 Device Management Interface (Slave) */
#define	BHND_PRIMEID_MWRAP		0x369		/* PL369 Device Management Interface (Master) */

/* Core HW Revision Numbers */
#define	BHND_HWREV_INVALID		0xFF		/* Invalid hardware revision ID */

/* Chip Types */
#define	BHND_CHIPTYPE_SIBA		0		/**< siba(4) interconnect */
#define	BHND_CHIPTYPE_BCMA		1		/**< bcma(4) interconnect */
#define	BHND_CHIPTYPE_UBUS		2		/**< ubus interconnect found in bcm63xx devices */
#define	BHND_CHIPTYPE_BCMA_ALT		3		/**< bcma(4) interconnect */

/** Evaluates to true if @p _type is a BCMA or BCMA-compatible interconenct */
#define	BHND_CHIPTYPE_IS_BCMA_COMPATIBLE(_type)	\
	((_type) == BHND_CHIPTYPE_BCMA ||	\
	 (_type) == BHND_CHIPTYPE_BCMA_ALT ||	\
	 (_type) == BHND_CHIPTYPE_UBUS)

/** Evaluates to true if @p _type uses a BCMA EROM table */
#define	BHND_CHIPTYPE_HAS_EROM(_type)		\
	BHND_CHIPTYPE_IS_BCMA_COMPATIBLE(_type)

/* Boardflags */
#define	BHND_BFL_BTC2WIRE		0x00000001	/* old 2wire Bluetooth coexistence, OBSOLETE */
#define	BHND_BFL_BTCOEX			0x00000001	/* Board supports BTCOEX */
#define	BHND_BFL_PACTRL			0x00000002	/* Board has gpio 9 controlling the PA */
#define	BHND_BFL_AIRLINEMODE		0x00000004	/* Board implements gpio 13 radio disable indication, UNUSED */
#define	BHND_BFL_ADCDIV			0x00000008	/* Board has the rssi ADC divider */
#define	BHND_BFL_DIS_256QAM		0x00000008
#define	BHND_BFL_ENETROBO		0x00000010	/* Board has robo switch or core */
#define	BHND_BFL_NOPLLDOWN		0x00000020	/* Not ok to power down the chip pll and oscillator */
#define	BHND_BFL_CCKHIPWR		0x00000040	/* Can do high-power CCK transmission */
#define	BHND_BFL_ENETADM		0x00000080	/* Board has ADMtek switch */
#define	BHND_BFL_ENETVLAN		0x00000100	/* Board has VLAN capability */
#define	BHND_BFL_LTECOEX		0x00000200	/* Board has LTE coex capability */
#define	BHND_BFL_NOPCI			0x00000400	/* Board leaves PCI floating */
#define	BHND_BFL_FEM			0x00000800	/* Board supports the Front End Module */
#define	BHND_BFL_EXTLNA			0x00001000	/* Board has an external LNA in 2.4GHz band */
#define	BHND_BFL_HGPA			0x00002000	/* Board has a high gain PA */
#define	BHND_BFL_BTC2WIRE_ALTGPIO	0x00004000
/* Board's BTC 2wire is in the alternate gpios OBSLETE */
#define	BHND_BFL_ALTIQ			0x00008000	/* Alternate I/Q settings */
#define	BHND_BFL_NOPA			0x00010000	/* Board has no PA */
#define	BHND_BFL_RSSIINV		0x00020000	/* Board's RSSI uses positive slope(not TSSI) */
#define	BHND_BFL_PAREF			0x00040000	/* Board uses the PARef LDO */
#define	BHND_BFL_3TSWITCH		0x00080000	/* Board uses a triple throw switch shared with BT */
#define	BHND_BFL_PHASESHIFT		0x00100000	/* Board can support phase shifter */
#define	BHND_BFL_BUCKBOOST		0x00200000	/* Power topology uses BUCKBOOST */
#define	BHND_BFL_FEM_BT			0x00400000	/* Board has FEM and switch to share antenna w/ BT */
#define	BHND_BFL_RXCHAIN_OFF_BT		0x00400000	/* one rxchain is to be shut off when BT is active */
#define	BHND_BFL_NOCBUCK		0x00800000	/* Power topology doesn't use CBUCK */
#define	BHND_BFL_CCKFAVOREVM		0x01000000	/* Favor CCK EVM over spectral mask */
#define	BHND_BFL_PALDO			0x02000000	/* Power topology uses PALDO */
#define	BHND_BFL_LNLDO2_2P5		0x04000000	/* Select 2.5V as LNLDO2 output voltage */
#define	BHND_BFL_FASTPWR		0x08000000
#define	BHND_BFL_UCPWRCTL_MININDX	0x08000000	/* Enforce min power index to avoid FEM damage */
#define	BHND_BFL_EXTLNA_5GHZ		0x10000000	/* Board has an external LNA in 5GHz band */
#define	BHND_BFL_TRSW_1BY2		0x20000000	/* Board has 2 TRSW's in 1by2 designs */
#define	BHND_BFL_GAINBOOSTA01	        0x20000000	/* 5g Gainboost for core0 and core1 */
#define	BHND_BFL_LO_TRSW_R_5GHZ		0x40000000	/* In 5G do not throw TRSW to T for clipLO gain */
#define	BHND_BFL_ELNA_GAINDEF		0x80000000	/* Backoff InitGain based on elna_2g/5g field
							 * when this flag is set
							 */
#define	BHND_BFL_EXTLNA_TX		0x20000000	/* Temp boardflag to indicate to */


/* Boardflags2 */
#define	BHND_BFL2_RXBB_INT_REG_DIS	0x00000001	/* Board has an external rxbb regulator */
#define	BHND_BFL2_APLL_WAR		0x00000002	/* Flag to implement alternative A-band PLL settings */
#define	BHND_BFL2_TXPWRCTRL_EN		0x00000004	/* Board permits enabling TX Power Control */
#define	BHND_BFL2_2X4_DIV		0x00000008	/* Board supports the 2X4 diversity switch */
#define	BHND_BFL2_5G_PWRGAIN		0x00000010	/* Board supports 5G band power gain */
#define	BHND_BFL2_PCIEWAR_OVR		0x00000020	/* Board overrides ASPM and Clkreq settings */
#define	BHND_BFL2_CAESERS_BRD		0x00000040	/* Board is Caesers brd (unused by sw) */
#define	BHND_BFL2_BTC3WIRE		0x00000080	/* Board support legacy 3 wire or 4 wire */
#define	BHND_BFL2_BTCLEGACY		0x00000080	/* Board support legacy 3/4 wire, to replace 
							 * BHND_BFL2_BTC3WIRE
							 */
#define	BHND_BFL2_SKWRKFEM_BRD		0x00000100	/* 4321mcm93 board uses Skyworks FEM */
#define	BHND_BFL2_SPUR_WAR		0x00000200	/* Board has a WAR for clock-harmonic spurs */
#define	BHND_BFL2_GPLL_WAR		0x00000400	/* Flag to narrow G-band PLL loop b/w */
#define	BHND_BFL2_TRISTATE_LED		0x00000800	/* Tri-state the LED */
#define	BHND_BFL2_SINGLEANT_CCK		0x00001000	/* Tx CCK pkts on Ant 0 only */
#define	BHND_BFL2_2G_SPUR_WAR		0x00002000	/* WAR to reduce and avoid clock-harmonic spurs in 2G */
#define	BHND_BFL2_BPHY_ALL_TXCORES	0x00004000	/* Transmit bphy frames using all tx cores */
#define	BHND_BFL2_FCC_BANDEDGE_WAR	0x00008000	/* Activates WAR to improve FCC bandedge performance */
#define	BHND_BFL2_GPLL_WAR2	        0x00010000	/* Flag to widen G-band PLL loop b/w */
#define	BHND_BFL2_IPALVLSHIFT_3P3	0x00020000
#define	BHND_BFL2_INTERNDET_TXIQCAL	0x00040000	/* Use internal envelope detector for TX IQCAL */
#define	BHND_BFL2_XTALBUFOUTEN		0x00080000	/* Keep the buffered Xtal output from radio on */
						  	/* Most drivers will turn it off without this flag */
						  	/* to save power. */

#define	BHND_BFL2_ANAPACTRL_2G		0x00100000	/* 2G ext PAs are controlled by analog PA ctrl lines */
#define	BHND_BFL2_ANAPACTRL_5G		0x00200000	/* 5G ext PAs are controlled by analog PA ctrl lines */
#define	BHND_BFL2_ELNACTRL_TRSW_2G	0x00400000	/* AZW4329: 2G gmode_elna_gain controls TR Switch */
#define	BHND_BFL2_BT_SHARE_ANT0		0x00800000	/* WLAN/BT share antenna 0 */
#define	BHND_BFL2_BT_SHARE_BM_BIT0	0x00800000	/* bit 0 of WLAN/BT shared core bitmap */
#define	BHND_BFL2_TEMPSENSE_HIGHER	0x01000000	/* The tempsense threshold can sustain higher value
							 * than programmed. The exact delta is decided by
							 * driver per chip/boardtype. This can be used
							 * when tempsense qualification happens after shipment
							 */
#define	BHND_BFL2_BTC3WIREONLY		0x02000000	/* standard 3 wire btc only.  4 wire not supported */
#define	BHND_BFL2_PWR_NOMINAL		0x04000000	/* 0: power reduction on, 1: no power reduction */
#define	BHND_BFL2_EXTLNA_PWRSAVE	0x08000000	/* boardflag to enable ucode to apply power save
						  	 * ucode control of eLNA during Tx */
#define	BHND_BFL2_4313_RADIOREG		0x10000000
							/*  board rework */
#define	BHND_BFL2_DYNAMIC_VMID		0x10000000	/* boardflag to enable dynamic Vmid idle TSSI CAL */
#define	BHND_BFL2_SDR_EN		0x20000000	/* SDR enabled or disabled */
#define	BHND_BFL2_LNA1BYPFORTR2G  	0x40000000	/* acphy, enable lna1 bypass for clip gain, 2g */
#define	BHND_BFL2_LNA1BYPFORTR5G  	0x80000000	/* acphy, enable lna1 bypass for clip gain, 5g */


/* SROM 11 - 11ac boardflag definitions */
#define	BHND_BFL_SROM11_BTCOEX		0x00000001	/* Board supports BTCOEX */
#define	BHND_BFL_SROM11_WLAN_BT_SH_XTL	0x00000002	/* bluetooth and wlan share same crystal */
#define	BHND_BFL_SROM11_EXTLNA		0x00001000	/* Board has an external LNA in 2.4GHz band */
#define	BHND_BFL_SROM11_EXTLNA_5GHZ	0x10000000	/* Board has an external LNA in 5GHz band */
#define	BHND_BFL_SROM11_GAINBOOSTA01	0x20000000	/* 5g Gainboost for core0 and core1 */
#define	BHND_BFL2_SROM11_APLL_WAR	0x00000002	/* Flag to implement alternative A-band PLL settings */
#define	BHND_BFL2_SROM11_ANAPACTRL_2G	0x00100000	/* 2G ext PAs are ctrl-ed by analog PA ctrl lines */
#define	BHND_BFL2_SROM11_ANAPACTRL_5G	0x00200000	/* 5G ext PAs are ctrl-ed by analog PA ctrl lines */


/* Boardflags3 */
#define	BHND_BFL3_FEMCTRL_SUB			0x00000007	/* acphy, subrevs of femctrl on top of srom_femctrl */
#define	BHND_BFL3_RCAL_WAR			0x00000008	/* acphy, rcal war active on this board (4335a0) */
#define	BHND_BFL3_TXGAINTBLID			0x00000070	/* acphy, txgain table id */
#define	BHND_BFL3_TXGAINTBLID_SHIFT		0x4		/* acphy, txgain table id shift bit */
#define	BHND_BFL3_TSSI_DIV_WAR			0x00000080	/* acphy, Separate paparam for 20/40/80 */
#define	BHND_BFL3_TSSI_DIV_WAR_SHIFT		0x7		/* acphy, Separate paparam for 20/40/80 shift bit */
#define	BHND_BFL3_FEMTBL_FROM_NVRAM		0x00000100	/* acphy, femctrl table is read from nvram */
#define	BHND_BFL3_FEMTBL_FROM_NVRAM_SHIFT	0x8		/* acphy, femctrl table is read from nvram */
#define	BHND_BFL3_AGC_CFG_2G			0x00000200	/* acphy, gain control configuration for 2G */
#define	BHND_BFL3_AGC_CFG_5G			0x00000400	/* acphy, gain control configuration for 5G */
#define	BHND_BFL3_PPR_BIT_EXT			0x00000800	/* acphy, bit position for 1bit extension for ppr */
#define	BHND_BFL3_PPR_BIT_EXT_SHIFT		11		/* acphy, bit shift for 1bit extension for ppr */
#define	BHND_BFL3_BBPLL_SPR_MODE_DIS		0x00001000	/* acphy, disables bbpll spur modes */
#define	BHND_BFL3_RCAL_OTP_VAL_EN		0x00002000	/* acphy, to read rcal_trim value from otp */
#define	BHND_BFL3_2GTXGAINTBL_BLANK		0x00004000	/* acphy, blank the first X ticks of 2g gaintbl */
#define	BHND_BFL3_2GTXGAINTBL_BLANK_SHIFT	14		/* acphy, blank the first X ticks of 2g gaintbl */
#define	BHND_BFL3_5GTXGAINTBL_BLANK		0x00008000	/* acphy, blank the first X ticks of 5g gaintbl */
#define	BHND_BFL3_5GTXGAINTBL_BLANK_SHIFT	15		/* acphy, blank the first X ticks of 5g gaintbl */
#define	BHND_BFL3_BT_SHARE_BM_BIT1		0x40000000	/* bit 1 of WLAN/BT shared core bitmap */
#define	BHND_BFL3_PHASETRACK_MAX_ALPHABETA	0x00010000	/* acphy, to max out alpha,beta to 511 */
#define	BHND_BFL3_PHASETRACK_MAX_ALPHABETA_SHIFT 16		/* acphy, to max out alpha,beta to 511 */
#define	BHND_BFL3_BT_SHARE_BM_BIT1		0x40000000	/* bit 1 of WLAN/BT shared core bitmap */
#define	BHND_BFL3_EN_NONBRCM_TXBF		0x10000000	/* acphy, enable non-brcm TXBF */
#define	BHND_BFL3_EN_P2PLINK_TXBF		0x20000000	/* acphy, enable TXBF in p2p links */


/* board specific GPIO assignment, gpio 0-3 are also customer-configurable led */
#define	BHND_GPIO_BOARD_BTC3W_IN	0x850	/* bit 4 is RF_ACTIVE, bit 6 is STATUS, bit 11 is PRI */
#define	BHND_GPIO_BOARD_BTC3W_OUT	0x020	/* bit 5 is TX_CONF */
#define	BHND_GPIO_BOARD_BTCMOD_IN	0x010	/* bit 4 is the alternate BT Coexistence Input */
#define	BHND_GPIO_BOARD_BTCMOD_OUT	0x020	/* bit 5 is the alternate BT Coexistence Out */
#define	BHND_GPIO_BOARD_BTC_IN		0x080	/* bit 7 is BT Coexistence Input */
#define	BHND_GPIO_BOARD_BTC_OUT		0x100	/* bit 8 is BT Coexistence Out */
#define	BHND_GPIO_BOARD_PACTRL		0x200	/* bit 9 controls the PA on new 4306 boards */
#define	BHND_GPIO_BOARD_12		0x1000	/* gpio 12 */
#define	BHND_GPIO_BOARD_13		0x2000	/* gpio 13 */
#define	BHND_GPIO_BOARD_BTC4_IN		0x0800	/* gpio 11, coex4, in */
#define	BHND_GPIO_BOARD_BTC4_BT		0x2000	/* gpio 12, coex4, bt active */
#define	BHND_GPIO_BOARD_BTC4_STAT	0x4000	/* gpio 14, coex4, status */
#define	BHND_GPIO_BOARD_BTC4_WLAN	0x8000	/* gpio 15, coex4, wlan active */
#define	BHND_GPIO_BOARD_1_WLAN_PWR	0x02	/* throttle WLAN power on X21 board */
#define	BHND_GPIO_BOARD_3_WLAN_PWR	0x08	/* throttle WLAN power on X28 board */
#define	BHND_GPIO_BOARD_4_WLAN_PWR	0x10	/* throttle WLAN power on X19 board */

#define	BHND_GPIO_BTC4W_OUT_4312	0x010	/* bit 4 is BT_IODISABLE */
#define	BHND_GPIO_BTC4W_OUT_43224	0x020	/* bit 5 is BT_IODISABLE */
#define	BHND_GPIO_BTC4W_OUT_43224_SHARED 0x0e0  /* bit 5 is BT_IODISABLE */
#define	BHND_GPIO_BTC4W_OUT_43225	0x0e0	/* bit 5 BT_IODISABLE, bit 6 SW_BT, bit 7 SW_WL */
#define	BHND_GPIO_BTC4W_OUT_43421	0x020	/* bit 5 is BT_IODISABLE */
#define	BHND_GPIO_BTC4W_OUT_4313	0x060	/* bit 5 SW_BT, bit 6 SW_WL */
#define	BHND_GPIO_BTC4W_OUT_4331_SHARED	0x010	/* GPIO 4  */

/* Board Types */
#define	BHND_BOARD_BU4710		0x0400
#define	BHND_BOARD_VSIM4710		0x0401
#define	BHND_BOARD_QT4710		0x0402

#define	BHND_BOARD_BU4309		0x040a
#define	BHND_BOARD_BCM94309CB		0x040b
#define	BHND_BOARD_BCM94309MP		0x040c
#define	BHND_BOARD_BCM4309AP		0x040d

#define	BHND_BOARD_BCM94302MP		0x040e

#define	BHND_BOARD_BU4306		0x0416
#define	BHND_BOARD_BCM94306CB		0x0417
#define	BHND_BOARD_BCM94306MP		0x0418

#define	BHND_BOARD_BCM94710D		0x041a
#define	BHND_BOARD_BCM94710R1		0x041b
#define	BHND_BOARD_BCM94710R4		0x041c
#define	BHND_BOARD_BCM94710AP		0x041d

#define	BHND_BOARD_BU2050		0x041f


#define	BHND_BOARD_BCM94309G		0x0421

#define	BHND_BOARD_BU4704		0x0423
#define	BHND_BOARD_BU4702		0x0424

#define	BHND_BOARD_BCM94306PC		0x0425		/* pcmcia 3.3v 4306 card */


#define	BHND_BOARD_BCM94702MN		0x0428

/* BCM4702 1U CompactPCI Board */
#define	BHND_BOARD_BCM94702CPCI		0x0429

/* BCM4702 with BCM95380 VLAN Router */
#define	BHND_BOARD_BCM95380RR		0x042a

/* cb4306 with SiGe PA */
#define	BHND_BOARD_BCM94306CBSG		0x042b

/* cb4306 with SiGe PA */
#define	BHND_BOARD_PCSG94306		0x042d

/* bu4704 with sdram */
#define	BHND_BOARD_BU4704SD		0x042e

/* Dual 11a/11g Router */
#define	BHND_BOARD_BCM94704AGR		0x042f

/* 11a-only minipci */
#define	BHND_BOARD_BCM94308MP		0x0430



#define	BHND_BOARD_BU4712		0x0444
#define	BHND_BOARD_BU4712SD		0x045d
#define	BHND_BOARD_BU4712L		0x045f

/* BCM4712 boards */
#define	BHND_BOARD_BCM94712AP		0x0445
#define	BHND_BOARD_BCM94712P		0x0446

/* BCM4318 boards */
#define	BHND_BOARD_BU4318		0x0447
#define	BHND_BOARD_CB4318		0x0448
#define	BHND_BOARD_MPG4318		0x0449
#define	BHND_BOARD_MP4318		0x044a
#define	BHND_BOARD_SD4318		0x044b

/* BCM4313 boards */
#define	BHND_BOARD_BCM94313BU		0x050f
#define	BHND_BOARD_BCM94313HM		0x0510
#define	BHND_BOARD_BCM94313EPA		0x0511
#define	BHND_BOARD_BCM94313HMG		0x051C

/* BCM63XX boards */
#define	BHND_BOARD_BCM96338		0x6338
#define	BHND_BOARD_BCM96348		0x6348
#define	BHND_BOARD_BCM96358		0x6358
#define	BHND_BOARD_BCM96368		0x6368

/* Another mp4306 with SiGe */
#define	BHND_BOARD_BCM94306P		0x044c

/* mp4303 */
#define	BHND_BOARD_BCM94303MP		0x044e

/* mpsgh4306 */
#define	BHND_BOARD_BCM94306MPSGH	0x044f

/* BRCM 4306 w/ Front End Modules */
#define	BHND_BOARD_BCM94306MPM		0x0450
#define	BHND_BOARD_BCM94306MPL		0x0453

/* 4712agr */
#define	BHND_BOARD_BCM94712AGR		0x0451

/* pcmcia 4303 */
#define	BHND_BOARD_PC4303		0x0454

/* 5350K */
#define	BHND_BOARD_BCM95350K		0x0455

/* 5350R */
#define	BHND_BOARD_BCM95350R		0x0456

/* 4306mplna */
#define	BHND_BOARD_BCM94306MPLNA	0x0457

/* 4320 boards */
#define	BHND_BOARD_BU4320		0x0458
#define	BHND_BOARD_BU4320S		0x0459
#define	BHND_BOARD_BCM94320PH		0x045a

/* 4306mph */
#define	BHND_BOARD_BCM94306MPH		0x045b

/* 4306pciv */
#define	BHND_BOARD_BCM94306PCIV		0x045c

#define	BHND_BOARD_BU4712SD		0x045d

#define	BHND_BOARD_BCM94320PFLSH	0x045e

#define	BHND_BOARD_BU4712L		0x045f
#define	BHND_BOARD_BCM94712LGR		0x0460
#define	BHND_BOARD_BCM94320R		0x0461

#define	BHND_BOARD_BU5352		0x0462

#define	BHND_BOARD_BCM94318MPGH		0x0463

#define	BHND_BOARD_BU4311		0x0464
#define	BHND_BOARD_BCM94311MC		0x0465
#define	BHND_BOARD_BCM94311MCAG		0x0466

#define	BHND_BOARD_BCM95352GR		0x0467

/* bcm95351agr */
#define	BHND_BOARD_BCM95351AGR		0x0470

/* bcm94704mpcb */
#define	BHND_BOARD_BCM94704MPCB		0x0472

/* 4785 boards */
#define	BHND_BOARD_BU4785		0x0478

/* 4321 boards */
#define	BHND_BOARD_BCM4321BU		0x046b
#define	BHND_BOARD_BCM4321BUE		0x047c
#define	BHND_BOARD_BCM4321MP		0x046c
#define	BHND_BOARD_BCM4321CB2		0x046d
#define	BHND_BOARD_BCM4321CB2_AG	0x0066
#define	BHND_BOARD_BCM4321MC		0x046e

/* 4328 boards */
#define	BHND_BOARD_BU4328		0x0481
#define	BHND_BOARD_BCM4328SDG		0x0482
#define	BHND_BOARD_BCM4328SDAG		0x0483
#define	BHND_BOARD_BCM4328UG		0x0484
#define	BHND_BOARD_BCM4328UAG		0x0485
#define	BHND_BOARD_BCM4328PC		0x0486
#define	BHND_BOARD_BCM4328CF		0x0487

/* 4325 boards */
#define	BHND_BOARD_BCM94325DEVBU	0x0490
#define	BHND_BOARD_BCM94325BGABU	0x0491

#define	BHND_BOARD_BCM94325SDGWB	0x0492

#define	BHND_BOARD_BCM94325SDGMDL	0x04aa
#define	BHND_BOARD_BCM94325SDGMDL2	0x04c6
#define	BHND_BOARD_BCM94325SDGMDL3	0x04c9

#define	BHND_BOARD_BCM94325SDABGWBA	0x04e1

/* 4322 boards */
#define	BHND_BOARD_BCM94322MC		0x04a4
#define	BHND_BOARD_BCM94322USB		0x04a8	/* dualband */
#define	BHND_BOARD_BCM94322HM		0x04b0
#define	BHND_BOARD_BCM94322USB2D	0x04bf	/* single band discrete front end */

/* 4312 boards */
#define	BHND_BOARD_BCM4312MCGSG		0x04b5

/* 4315 boards */
#define	BHND_BOARD_BCM94315DEVBU	0x04c2
#define	BHND_BOARD_BCM94315USBGP	0x04c7
#define	BHND_BOARD_BCM94315BGABU	0x04ca
#define	BHND_BOARD_BCM94315USBGP41	0x04cb

/* 4319 boards */
#define	BHND_BOARD_BCM94319DEVBU	0X04e5
#define	BHND_BOARD_BCM94319USB		0X04e6
#define	BHND_BOARD_BCM94319SD		0X04e7

/* 4716 boards */
#define	BHND_BOARD_BCM94716NR2		0x04cd

/* 4319 boards */
#define	BHND_BOARD_BCM94319DEVBU	0X04e5
#define	BHND_BOARD_BCM94319USBNP4L	0X04e6
#define	BHND_BOARD_BCM94319WLUSBN4L	0X04e7
#define	BHND_BOARD_BCM94319SDG		0X04ea
#define	BHND_BOARD_BCM94319LCUSBSDN4L	0X04eb
#define	BHND_BOARD_BCM94319USBB		0x04ee
#define	BHND_BOARD_BCM94319LCSDN4L	0X0507
#define	BHND_BOARD_BCM94319LSUSBN4L	0X0508
#define	BHND_BOARD_BCM94319SDNA4L	0X0517
#define	BHND_BOARD_BCM94319SDELNA4L	0X0518
#define	BHND_BOARD_BCM94319SDELNA6L	0X0539
#define	BHND_BOARD_BCM94319ARCADYAN	0X0546
#define	BHND_BOARD_BCM94319WINDSOR	0x0561
#define	BHND_BOARD_BCM94319MLAP		0x0562
#define	BHND_BOARD_BCM94319SDNA		0x058b
#define	BHND_BOARD_BCM94319BHEMU3	0x0563
#define	BHND_BOARD_BCM94319SDHMB	0x058c
#define	BHND_BOARD_BCM94319SDBREF	0x05a1
#define	BHND_BOARD_BCM94319USBSDB	0x05a2

/* 4329 boards */
#define	BHND_BOARD_BCM94329AGB		0X04b9
#define	BHND_BOARD_BCM94329TDKMDL1	0X04ba
#define	BHND_BOARD_BCM94329TDKMDL11	0X04fc
#define	BHND_BOARD_BCM94329OLYMPICN18	0X04fd
#define	BHND_BOARD_BCM94329OLYMPICN90	0X04fe
#define	BHND_BOARD_BCM94329OLYMPICN90U	0X050c
#define	BHND_BOARD_BCM94329OLYMPICN90M	0X050b
#define	BHND_BOARD_BCM94329AGBF		0X04ff
#define	BHND_BOARD_BCM94329OLYMPICX17	0X0504
#define	BHND_BOARD_BCM94329OLYMPICX17M	0X050a
#define	BHND_BOARD_BCM94329OLYMPICX17U	0X0509
#define	BHND_BOARD_BCM94329OLYMPICUNO	0X0564
#define	BHND_BOARD_BCM94329MOTOROLA	0X0565
#define	BHND_BOARD_BCM94329OLYMPICLOCO	0X0568

/* 4336 SDIO board types */
#define	BHND_BOARD_BCM94336SD_WLBGABU	0x0511
#define	BHND_BOARD_BCM94336SD_WLBGAREF	0x0519
#define	BHND_BOARD_BCM94336SDGP		0x0538
#define	BHND_BOARD_BCM94336SDG		0x0519
#define	BHND_BOARD_BCM94336SDGN		0x0538
#define	BHND_BOARD_BCM94336SDGFC	0x056B

/* 4330 SDIO board types */
#define	BHND_BOARD_BCM94330SDG		0x0528
#define	BHND_BOARD_BCM94330SD_FCBGABU	0x052e
#define	BHND_BOARD_BCM94330SD_WLBGABU	0x052f
#define	BHND_BOARD_BCM94330SD_FCBGA	0x0530
#define	BHND_BOARD_BCM94330FCSDAGB	0x0532
#define	BHND_BOARD_BCM94330OLYMPICAMG	0x0549
#define	BHND_BOARD_BCM94330OLYMPICAMGEPA	0x054F
#define	BHND_BOARD_BCM94330OLYMPICUNO3	0x0551
#define	BHND_BOARD_BCM94330WLSDAGB	0x0547
#define	BHND_BOARD_BCM94330CSPSDAGBB	0x054A

/* 43224 boards */
#define	BHND_BOARD_BCM943224X21		0x056e
#define	BHND_BOARD_BCM943224X21_FCC	0x00d1
#define	BHND_BOARD_BCM943224X21B	0x00e9
#define	BHND_BOARD_BCM943224M93		0x008b
#define	BHND_BOARD_BCM943224M93A	0x0090
#define	BHND_BOARD_BCM943224X16		0x0093
#define	BHND_BOARD_BCM94322X9		0x008d
#define	BHND_BOARD_BCM94322M35e		0x008e

/* 43228 Boards */
#define	BHND_BOARD_BCM943228BU8		0x0540
#define	BHND_BOARD_BCM943228BU9		0x0541
#define	BHND_BOARD_BCM943228BU		0x0542
#define	BHND_BOARD_BCM943227HM4L	0x0543
#define	BHND_BOARD_BCM943227HMB		0x0544
#define	BHND_BOARD_BCM943228HM4L	0x0545
#define	BHND_BOARD_BCM943228SD		0x0573

/* 43239 Boards */
#define	BHND_BOARD_BCM943239MOD		0x05ac
#define	BHND_BOARD_BCM943239REF		0x05aa

/* 4331 boards */
#define	BHND_BOARD_BCM94331X19		0x00D6	/* X19B */
#define	BHND_BOARD_BCM94331X28		0x00E4	/* X28 */
#define	BHND_BOARD_BCM94331X28B		0x010E	/* X28B */
#define	BHND_BOARD_BCM94331PCIEBT3Ax	BCM94331X28
#define	BHND_BOARD_BCM94331X12_2G	0x00EC	/* X12 2G */
#define	BHND_BOARD_BCM94331X12_5G	0x00ED	/* X12 5G */
#define	BHND_BOARD_BCM94331X29B		0x00EF	/* X29B */
#define	BHND_BOARD_BCM94331X29D		0x010F	/* X29D */
#define	BHND_BOARD_BCM94331CSAX		BCM94331X29B
#define	BHND_BOARD_BCM94331X19C		0x00F5	/* X19C */
#define	BHND_BOARD_BCM94331X33		0x00F4	/* X33 */
#define	BHND_BOARD_BCM94331BU		0x0523
#define	BHND_BOARD_BCM94331S9BU		0x0524
#define	BHND_BOARD_BCM94331MC		0x0525
#define	BHND_BOARD_BCM94331MCI		0x0526
#define	BHND_BOARD_BCM94331PCIEBT4	0x0527
#define	BHND_BOARD_BCM94331HM		0x0574
#define	BHND_BOARD_BCM94331PCIEDUAL	0x059B
#define	BHND_BOARD_BCM94331MCH5		0x05A9
#define	BHND_BOARD_BCM94331CS		0x05C6
#define	BHND_BOARD_BCM94331CD		0x05DA

/* 4314 Boards */
#define	BHND_BOARD_BCM94314BU		0x05b1

/* 53572 Boards */
#define	BHND_BOARD_BCM953572BU		0x058D
#define	BHND_BOARD_BCM953572NR2		0x058E
#define	BHND_BOARD_BCM947188NR2		0x058F
#define	BHND_BOARD_BCM953572SDRNR2	0x0590

/* 43236 boards */
#define	BHND_BOARD_BCM943236OLYMPICSULLEY	0x594
#define	BHND_BOARD_BCM943236PREPROTOBLU2O3	0x5b9
#define	BHND_BOARD_BCM943236USBELNA		0x5f8

/* 4314 Boards */
#define	BHND_BOARD_BCM94314BUSDIO	0x05c8
#define	BHND_BOARD_BCM94314BGABU	0x05c9
#define	BHND_BOARD_BCM94314HMEPA	0x05ca
#define	BHND_BOARD_BCM94314HMEPABK	0x05cb
#define	BHND_BOARD_BCM94314SUHMEPA	0x05cc
#define	BHND_BOARD_BCM94314SUHM		0x05cd
#define	BHND_BOARD_BCM94314HM		0x05d1

/* 4334 Boards */
#define	BHND_BOARD_BCM94334FCAGBI	0x05df
#define	BHND_BOARD_BCM94334WLAGBI	0x05dd

/* 4335 Boards */
#define	BHND_BOARD_BCM94335X52		0x0114

/* 4345 Boards */
#define	BHND_BOARD_BCM94345		0x0687

/* 4360 Boards */
#define	BHND_BOARD_BCM94360X52C		0X0117
#define	BHND_BOARD_BCM94360X52D		0X0137
#define	BHND_BOARD_BCM94360X29C		0X0112
#define	BHND_BOARD_BCM94360X29CP2	0X0134
#define	BHND_BOARD_BCM94360X51		0x0111
#define	BHND_BOARD_BCM94360X51P2	0x0129
#define	BHND_BOARD_BCM94360X51A		0x0135
#define	BHND_BOARD_BCM94360X51B		0x0136
#define	BHND_BOARD_BCM94360CS		0x061B
#define	BHND_BOARD_BCM94360J28_D11AC2G	0x0c00
#define	BHND_BOARD_BCM94360J28_D11AC5G	0x0c01
#define	BHND_BOARD_BCM94360USBH5_D11AC5G	0x06aa

/* 4350 Boards */
#define	BHND_BOARD_BCM94350X52B		0X0116
#define	BHND_BOARD_BCM94350X14		0X0131

/* 43217 Boards */
#define	BHND_BOARD_BCM943217BU		0x05d5
#define	BHND_BOARD_BCM943217HM2L	0x05d6
#define	BHND_BOARD_BCM943217HMITR2L	0x05d7

/* 43142 Boards */
#define	BHND_BOARD_BCM943142HM		0x05e0

/* 43341 Boards */
#define	BHND_BOARD_BCM943341WLABGS	0x062d

/* 43342 Boards */
#define	BHND_BOARD_BCM943342FCAGBI	0x0641

/* 43602 Boards, unclear yet what boards will be created. */
#define	BHND_BOARD_BCM943602RSVD1	0x06a5
#define	BHND_BOARD_BCM943602RSVD2	0x06a6
#define	BHND_BOARD_BCM943602X87		0X0133
#define	BHND_BOARD_BCM943602X238	0X0132

/* 4354 board types */
#define	BHND_BOARD_BCM94354WLSAGBI	0x06db
#define	BHND_BOARD_BCM94354Z		0x0707

/* # of GPIO pins */
#define	BHND_BCM43XX_GPIO_NUMPINS	32

/* These values are used by dhd USB host driver. */
#define	BHND_USB_RDL_RAM_BASE_4319	0x60000000
#define	BHND_USB_RDL_RAM_BASE_4329	0x60000000
#define	BHND_USB_RDL_RAM_SIZE_4319	0x48000
#define	BHND_USB_RDL_RAM_SIZE_4329 	0x48000
#define	BHND_USB_RDL_RAM_SIZE_43236	0x70000
#define	BHND_USB_RDL_RAM_BASE_43236	0x60000000
#define	BHND_USB_RDL_RAM_SIZE_4328	0x60000
#define	BHND_USB_RDL_RAM_BASE_4328	0x80000000
#define	BHND_USB_RDL_RAM_SIZE_4322	0x60000
#define	BHND_USB_RDL_RAM_BASE_4322	0x60000000
#define	BHND_USB_RDL_RAM_SIZE_4360	0xA0000
#define	BHND_USB_RDL_RAM_BASE_4360	0x60000000
#define	BHND_USB_RDL_RAM_SIZE_43242	0x90000
#define	BHND_USB_RDL_RAM_BASE_43242	0x60000000
#define	BHND_USB_RDL_RAM_SIZE_43143	0x70000
#define	BHND_USB_RDL_RAM_BASE_43143	0x60000000
#define	BHND_USB_RDL_RAM_SIZE_4350	0xC0000
#define	BHND_USB_RDL_RAM_BASE_4350	0x180800

/* generic defs for nvram "muxenab" bits
* Note: these differ for 4335a0. refer bcmchipc.h for specific mux options.
*/
#define	BHND_NVRAM_MUXENAB_UART		0x00000001
#define	BHND_NVRAM_MUXENAB_GPIO		0x00000002
#define	BHND_NVRAM_MUXENAB_ERCX		0x00000004	/* External Radio BT coex */
#define	BHND_NVRAM_MUXENAB_JTAG		0x00000008
#define	BHND_NVRAM_MUXENAB_HOST_WAKE	0x00000010	/* configure GPIO for SDIO host_wake */
#define	BHND_NVRAM_MUXENAB_I2S_EN	0x00000020
#define	BHND_NVRAM_MUXENAB_I2S_MASTER	0x00000040
#define	BHND_NVRAM_MUXENAB_I2S_FULL	0x00000080
#define	BHND_NVRAM_MUXENAB_SFLASH	0x00000100
#define	BHND_NVRAM_MUXENAB_RFSWCTRL0	0x00000200
#define	BHND_NVRAM_MUXENAB_RFSWCTRL1	0x00000400
#define	BHND_NVRAM_MUXENAB_RFSWCTRL2	0x00000800
#define	BHND_NVRAM_MUXENAB_SECI		0x00001000
#define	BHND_NVRAM_MUXENAB_BT_LEGACY	0x00002000
#define	BHND_NVRAM_MUXENAB_HOST_WAKE1	0x00004000	/* configure alternative GPIO for SDIO host_wake */

/* Boot flags */
#define	BHND_BOOTFLAG_FLASH_KERNEL_NFLASH	0x00000001
#define	BHND_BOOTFLAG_FLASH_BOOT_NFLASH		0x00000002

#endif /* _BHND_BHND_IDS_H_ */
