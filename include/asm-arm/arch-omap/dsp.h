/*
 * linux/include/asm-arm/arch-omap/dsp.h
 *
 * Header for OMAP DSP driver
 *
 * Copyright (C) 2002-2005 Nokia Corporation
 *
 * Written by Toshihiro Kobayashi <toshihiro.kobayashi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * 2005/06/01:  DSP Gateway version 3.3
 */

#ifndef ASM_ARCH_DSP_H
#define ASM_ARCH_DSP_H


/*
 * for /dev/dspctl/ctl
 */
#define OMAP_DSP_IOCTL_RESET			1
#define OMAP_DSP_IOCTL_RUN			2
#define OMAP_DSP_IOCTL_SETRSTVECT		3
#define OMAP_DSP_IOCTL_CPU_IDLE			4
#define OMAP_DSP_IOCTL_MPUI_WORDSWAP_ON		5
#define OMAP_DSP_IOCTL_MPUI_WORDSWAP_OFF	6
#define OMAP_DSP_IOCTL_MPUI_BYTESWAP_ON		7
#define OMAP_DSP_IOCTL_MPUI_BYTESWAP_OFF	8
#define OMAP_DSP_IOCTL_GBL_IDLE			9
#define OMAP_DSP_IOCTL_DSPCFG			10
#define OMAP_DSP_IOCTL_DSPUNCFG			11
#define OMAP_DSP_IOCTL_TASKCNT			12
#define OMAP_DSP_IOCTL_POLL			13
#define OMAP_DSP_IOCTL_REGMEMR			40
#define OMAP_DSP_IOCTL_REGMEMW			41
#define OMAP_DSP_IOCTL_REGIOR			42
#define OMAP_DSP_IOCTL_REGIOW			43
#define OMAP_DSP_IOCTL_GETVAR			44
#define OMAP_DSP_IOCTL_SETVAR			45
#define OMAP_DSP_IOCTL_RUNLEVEL			50
#define OMAP_DSP_IOCTL_SUSPEND			51
#define OMAP_DSP_IOCTL_RESUME			52
#define OMAP_DSP_IOCTL_FBEN			53
#define OMAP_DSP_IOCTL_FBDIS			54
#define OMAP_DSP_IOCTL_MBSEND			99

/*
 * for taskdev
 * (ioctls below should be >= 0x10000)
 */
#define OMAP_DSP_TASK_IOCTL_BFLSH	0x10000
#define OMAP_DSP_TASK_IOCTL_SETBSZ	0x10001
#define OMAP_DSP_TASK_IOCTL_LOCK	0x10002
#define OMAP_DSP_TASK_IOCTL_UNLOCK	0x10003
#define OMAP_DSP_TASK_IOCTL_GETNAME	0x10004

/*
 * for /dev/dspctl/mem
 */
#define OMAP_DSP_MEM_IOCTL_EXMAP	1
#define OMAP_DSP_MEM_IOCTL_EXUNMAP	2
#define OMAP_DSP_MEM_IOCTL_EXMAP_FLUSH	3
#define OMAP_DSP_MEM_IOCTL_FBEXPORT	5
#define OMAP_DSP_MEM_IOCTL_MMUITACK	7
#define OMAP_DSP_MEM_IOCTL_MMUINIT	9
#define OMAP_DSP_MEM_IOCTL_KMEM_RESERVE	11
#define OMAP_DSP_MEM_IOCTL_KMEM_RELEASE	12

struct omap_dsp_mapinfo {
	unsigned long dspadr;
	unsigned long size;
};

/*
 * for /dev/dspctl/twch
 */
#define OMAP_DSP_TWCH_IOCTL_MKDEV	1
#define OMAP_DSP_TWCH_IOCTL_RMDEV	2
#define OMAP_DSP_TWCH_IOCTL_TADD	11
#define OMAP_DSP_TWCH_IOCTL_TDEL	12
#define OMAP_DSP_TWCH_IOCTL_TKILL	13

#define OMAP_DSP_DEVSTATE_NOTASK	0x00000001
#define OMAP_DSP_DEVSTATE_ATTACHED	0x00000002
#define OMAP_DSP_DEVSTATE_GARBAGE	0x00000004
#define OMAP_DSP_DEVSTATE_INVALID	0x00000008
#define OMAP_DSP_DEVSTATE_ADDREQ	0x00000100
#define OMAP_DSP_DEVSTATE_DELREQ	0x00000200
#define OMAP_DSP_DEVSTATE_ADDFAIL	0x00001000
#define OMAP_DSP_DEVSTATE_ADDING	0x00010000
#define OMAP_DSP_DEVSTATE_DELING	0x00020000
#define OMAP_DSP_DEVSTATE_KILLING	0x00040000
#define OMAP_DSP_DEVSTATE_STATE_MASK	0x7fffffff
#define OMAP_DSP_DEVSTATE_STALE		0x80000000

struct omap_dsp_taddinfo {
	unsigned char minor;
	unsigned long taskadr;
};
#define OMAP_DSP_TADD_ABORTADR	0xffffffff


/*
 * error cause definition (for error detection device)
 */
#define OMAP_DSP_ERRDT_WDT	0x00000001
#define OMAP_DSP_ERRDT_MMU	0x00000002


/*
 * mailbox protocol definitions
 */

struct omap_dsp_mailbox_cmd {
	unsigned short cmd;
	unsigned short data;
};

struct omap_dsp_reginfo {
	unsigned short adr;
	unsigned short val;
};

struct omap_dsp_varinfo {
	unsigned char varid;
	unsigned short val[0];
};

#define OMAP_DSP_MBPROT_REVISION	0x0019

#define OMAP_DSP_MBCMD_WDSND	0x10
#define OMAP_DSP_MBCMD_WDREQ	0x11
#define OMAP_DSP_MBCMD_BKSND	0x20
#define OMAP_DSP_MBCMD_BKREQ	0x21
#define OMAP_DSP_MBCMD_BKYLD	0x23
#define OMAP_DSP_MBCMD_BKSNDP	0x24
#define OMAP_DSP_MBCMD_BKREQP	0x25
#define OMAP_DSP_MBCMD_TCTL	0x30
#define OMAP_DSP_MBCMD_TCTLDATA	0x31
#define OMAP_DSP_MBCMD_POLL	0x32
#define OMAP_DSP_MBCMD_WDT	0x50	/* v3.3: obsolete */
#define OMAP_DSP_MBCMD_RUNLEVEL	0x51
#define OMAP_DSP_MBCMD_PM	0x52
#define OMAP_DSP_MBCMD_SUSPEND	0x53
#define OMAP_DSP_MBCMD_KFUNC	0x54
#define OMAP_DSP_MBCMD_TCFG	0x60
#define OMAP_DSP_MBCMD_TADD	0x62
#define OMAP_DSP_MBCMD_TDEL	0x63
#define OMAP_DSP_MBCMD_TSTOP	0x65
#define OMAP_DSP_MBCMD_DSPCFG	0x70
#define OMAP_DSP_MBCMD_REGRW	0x72
#define OMAP_DSP_MBCMD_GETVAR	0x74
#define OMAP_DSP_MBCMD_SETVAR	0x75
#define OMAP_DSP_MBCMD_ERR	0x78
#define OMAP_DSP_MBCMD_DBG	0x79

#define OMAP_DSP_MBCMD_TCTL_TINIT	0x0000
#define OMAP_DSP_MBCMD_TCTL_TEN		0x0001
#define OMAP_DSP_MBCMD_TCTL_TDIS	0x0002
#define OMAP_DSP_MBCMD_TCTL_TCLR	0x0003
#define OMAP_DSP_MBCMD_TCTL_TCLR_FORCE	0x0004

#define OMAP_DSP_MBCMD_RUNLEVEL_USER		0x01
#define OMAP_DSP_MBCMD_RUNLEVEL_SUPER		0x0e
#define OMAP_DSP_MBCMD_RUNLEVEL_RECOVERY	0x10

#define OMAP_DSP_MBCMD_PM_DISABLE	0x00
#define OMAP_DSP_MBCMD_PM_ENABLE	0x01

#define OMAP_DSP_MBCMD_KFUNC_FBCTL	0x00
#define OMAP_DSP_MBCMD_KFUNC_AUDIO_PWR	0x01

#define OMAP_DSP_MBCMD_FBCTL_UPD	0x0000
#define OMAP_DSP_MBCMD_FBCTL_ENABLE	0x0002
#define OMAP_DSP_MBCMD_FBCTL_DISABLE	0x0003

#define OMAP_DSP_MBCMD_AUDIO_PWR_UP	0x0000
#define OMAP_DSP_MBCMD_AUDIO_PWR_DOWN1	0x0001
#define OMAP_DSP_MBCMD_AUDIO_PWR_DOWN2	0x0002

#define OMAP_DSP_MBCMD_TDEL_SAFE	0x0000
#define OMAP_DSP_MBCMD_TDEL_KILL	0x0001

#define OMAP_DSP_MBCMD_DSPCFG_REQ	0x00
#define OMAP_DSP_MBCMD_DSPCFG_SYSADRH	0x28
#define OMAP_DSP_MBCMD_DSPCFG_SYSADRL	0x29
#define OMAP_DSP_MBCMD_DSPCFG_PROTREV	0x70
#define OMAP_DSP_MBCMD_DSPCFG_ABORT	0x78
#define OMAP_DSP_MBCMD_DSPCFG_LAST	0x80

#define OMAP_DSP_MBCMD_REGRW_MEMR	0x00
#define OMAP_DSP_MBCMD_REGRW_MEMW	0x01
#define OMAP_DSP_MBCMD_REGRW_IOR	0x02
#define OMAP_DSP_MBCMD_REGRW_IOW	0x03
#define OMAP_DSP_MBCMD_REGRW_DATA	0x04

#define OMAP_DSP_MBCMD_VARID_ICRMASK	0x00
#define OMAP_DSP_MBCMD_VARID_LOADINFO	0x01

#define OMAP_DSP_TTYP_ARCV	0x0001
#define OMAP_DSP_TTYP_ASND	0x0002
#define OMAP_DSP_TTYP_BKMD	0x0004
#define OMAP_DSP_TTYP_BKDM	0x0008
#define OMAP_DSP_TTYP_PVMD	0x0010
#define OMAP_DSP_TTYP_PVDM	0x0020

#define OMAP_DSP_EID_BADTID	0x10
#define OMAP_DSP_EID_BADTCN	0x11
#define OMAP_DSP_EID_BADBID	0x20
#define OMAP_DSP_EID_BADCNT	0x21
#define OMAP_DSP_EID_NOTLOCKED	0x22
#define OMAP_DSP_EID_STVBUF	0x23
#define OMAP_DSP_EID_BADADR	0x24
#define OMAP_DSP_EID_BADTCTL	0x30
#define OMAP_DSP_EID_BADPARAM	0x50
#define OMAP_DSP_EID_FATAL	0x58
#define OMAP_DSP_EID_NOMEM	0xc0
#define OMAP_DSP_EID_NORES	0xc1
#define OMAP_DSP_EID_IPBFULL	0xc2
#define OMAP_DSP_EID_WDT	0xd0
#define OMAP_DSP_EID_TASKNOTRDY	0xe0
#define OMAP_DSP_EID_TASKBSY	0xe1
#define OMAP_DSP_EID_TASKERR	0xef
#define OMAP_DSP_EID_BADCFGTYP	0xf0
#define OMAP_DSP_EID_DEBUG	0xf8
#define OMAP_DSP_EID_BADSEQ	0xfe
#define OMAP_DSP_EID_BADCMD	0xff

#define OMAP_DSP_TNM_LEN	16

#define OMAP_DSP_TID_FREE	0xff
#define OMAP_DSP_TID_ANON	0xfe

#define OMAP_DSP_BID_NULL	0xffff
#define OMAP_DSP_BID_PVT	0xfffe

#endif /* ASM_ARCH_DSP_H */
