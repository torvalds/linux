/*
 *   Driver for the Conexant Riptide Soundchip
 *
 *	Copyright (c) 2004 Peter Gruber <nokos@gmx.net>
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
 *
 */
/*
  History:
   - 02/15/2004 first release
   
  This Driver is based on the OSS Driver version from Linuxant (riptide-0.6lnxtbeta03111100)
  credits from the original files:
  
  MODULE NAME:        cnxt_rt.h                       
  AUTHOR:             K. Lazarev  (Transcribed by KNL)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Created                           02/1/2000     KNL

  MODULE NAME:     int_mdl.c                       
  AUTHOR:          Konstantin Lazarev    (Transcribed by KNL)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Created                           10/01/99      KNL
	    
  MODULE NAME:        riptide.h                       
  AUTHOR:             O. Druzhinin  (Transcribed by OLD)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Created                           10/16/97      OLD

  MODULE NAME:        Rp_Cmdif.cpp                       
  AUTHOR:             O. Druzhinin  (Transcribed by OLD)
                      K. Lazarev    (Transcribed by KNL)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Adopted from NT4 driver            6/22/99      OLD
            Ported to Linux                    9/01/99      KNL

  MODULE NAME:        rt_hw.c                       
  AUTHOR:             O. Druzhinin  (Transcribed by OLD)
                      C. Lazarev    (Transcribed by CNL)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Created                           11/18/97      OLD
            Hardware functions for RipTide    11/24/97      CNL
            (ES1) are coded
            Hardware functions for RipTide    12/24/97      CNL
            (A0) are coded
            Hardware functions for RipTide    03/20/98      CNL
            (A1) are coded
            Boot loader is included           05/07/98      CNL
            Redesigned for WDM                07/27/98      CNL
            Redesigned for Linux              09/01/99      CNL

  MODULE NAME:        rt_hw.h
  AUTHOR:             C. Lazarev    (Transcribed by CNL)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Created                           11/18/97      CNL

  MODULE NAME:     rt_mdl.c                       
  AUTHOR:          Konstantin Lazarev    (Transcribed by KNL)
  HISTORY:         Major Revision               Date        By
            -----------------------------     --------     -----
            Created                           10/01/99      KNL

  MODULE NAME:        mixer.h                        
  AUTHOR:             K. Kenney
  HISTORY:         Major Revision                   Date          By
            -----------------------------          --------     -----
            Created from MS W95 Sample             11/28/95      KRS
            RipTide                                10/15/97      KRS
            Adopted for Windows NT driver          01/20/98      CNL
*/

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/gameport.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/initval.h>

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

MODULE_AUTHOR("Peter Gruber <nokos@gmx.net>");
MODULE_DESCRIPTION("riptide");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Conexant,Riptide}}");
MODULE_FIRMWARE("riptide.hex");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;

#ifdef SUPPORT_JOYSTICK
static int joystick_port[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS - 1)] = 0x200 };
#endif
static int mpu_port[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS - 1)] = 0x330 };
static int opl3_port[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS - 1)] = 0x388 };

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Riptide soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Riptide soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Riptide soundcard.");
#ifdef SUPPORT_JOYSTICK
module_param_array(joystick_port, int, NULL, 0444);
MODULE_PARM_DESC(joystick_port, "Joystick port # for Riptide soundcard.");
#endif
module_param_array(mpu_port, int, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU401 port # for Riptide driver.");
module_param_array(opl3_port, int, NULL, 0444);
MODULE_PARM_DESC(opl3_port, "OPL3 port # for Riptide driver.");

/*
 */

#define MPU401_HW_RIPTIDE MPU401_HW_MPU401
#define OPL3_HW_RIPTIDE   OPL3_HW_OPL3

#define PCI_EXT_CapId       0x40
#define PCI_EXT_NextCapPrt  0x41
#define PCI_EXT_PWMC        0x42
#define PCI_EXT_PWSCR       0x44
#define PCI_EXT_Data00      0x46
#define PCI_EXT_PMSCR_BSE   0x47
#define PCI_EXT_SB_Base     0x48
#define PCI_EXT_FM_Base     0x4a
#define PCI_EXT_MPU_Base    0x4C
#define PCI_EXT_Game_Base   0x4E
#define PCI_EXT_Legacy_Mask 0x50
#define PCI_EXT_AsicRev     0x52
#define PCI_EXT_Reserved3   0x53

#define LEGACY_ENABLE_ALL      0x8000	/* legacy device options */
#define LEGACY_ENABLE_SB       0x4000
#define LEGACY_ENABLE_FM       0x2000
#define LEGACY_ENABLE_MPU_INT  0x1000
#define LEGACY_ENABLE_MPU      0x0800
#define LEGACY_ENABLE_GAMEPORT 0x0400

#define MAX_WRITE_RETRY  10	/* cmd interface limits */
#define MAX_ERROR_COUNT  10
#define CMDIF_TIMEOUT    50000
#define RESET_TRIES      5

#define READ_PORT_ULONG(p)     inl((unsigned long)&(p))
#define WRITE_PORT_ULONG(p,x)  outl(x,(unsigned long)&(p))

#define READ_AUDIO_CONTROL(p)     READ_PORT_ULONG(p->audio_control)
#define WRITE_AUDIO_CONTROL(p,x)  WRITE_PORT_ULONG(p->audio_control,x)
#define UMASK_AUDIO_CONTROL(p,x)  WRITE_PORT_ULONG(p->audio_control,READ_PORT_ULONG(p->audio_control)|x)
#define MASK_AUDIO_CONTROL(p,x)   WRITE_PORT_ULONG(p->audio_control,READ_PORT_ULONG(p->audio_control)&x)
#define READ_AUDIO_STATUS(p)      READ_PORT_ULONG(p->audio_status)

#define SET_GRESET(p)     UMASK_AUDIO_CONTROL(p,0x0001)	/* global reset switch */
#define UNSET_GRESET(p)   MASK_AUDIO_CONTROL(p,~0x0001)
#define SET_AIE(p)        UMASK_AUDIO_CONTROL(p,0x0004)	/* interrupt enable */
#define UNSET_AIE(p)      MASK_AUDIO_CONTROL(p,~0x0004)
#define SET_AIACK(p)      UMASK_AUDIO_CONTROL(p,0x0008)	/* interrupt acknowledge */
#define UNSET_AIACKT(p)   MASKAUDIO_CONTROL(p,~0x0008)
#define SET_ECMDAE(p)     UMASK_AUDIO_CONTROL(p,0x0010)
#define UNSET_ECMDAE(p)   MASK_AUDIO_CONTROL(p,~0x0010)
#define SET_ECMDBE(p)     UMASK_AUDIO_CONTROL(p,0x0020)
#define UNSET_ECMDBE(p)   MASK_AUDIO_CONTROL(p,~0x0020)
#define SET_EDATAF(p)     UMASK_AUDIO_CONTROL(p,0x0040)
#define UNSET_EDATAF(p)   MASK_AUDIO_CONTROL(p,~0x0040)
#define SET_EDATBF(p)     UMASK_AUDIO_CONTROL(p,0x0080)
#define UNSET_EDATBF(p)   MASK_AUDIO_CONTROL(p,~0x0080)
#define SET_ESBIRQON(p)   UMASK_AUDIO_CONTROL(p,0x0100)
#define UNSET_ESBIRQON(p) MASK_AUDIO_CONTROL(p,~0x0100)
#define SET_EMPUIRQ(p)    UMASK_AUDIO_CONTROL(p,0x0200)
#define UNSET_EMPUIRQ(p)  MASK_AUDIO_CONTROL(p,~0x0200)
#define IS_CMDE(a)        (READ_PORT_ULONG(a->stat)&0x1)	/* cmd empty */
#define IS_DATF(a)        (READ_PORT_ULONG(a->stat)&0x2)	/* data filled */
#define IS_READY(p)       (READ_AUDIO_STATUS(p)&0x0001)
#define IS_DLREADY(p)     (READ_AUDIO_STATUS(p)&0x0002)
#define IS_DLERR(p)       (READ_AUDIO_STATUS(p)&0x0004)
#define IS_GERR(p)        (READ_AUDIO_STATUS(p)&0x0008)	/* error ! */
#define IS_CMDAEIRQ(p)    (READ_AUDIO_STATUS(p)&0x0010)
#define IS_CMDBEIRQ(p)    (READ_AUDIO_STATUS(p)&0x0020)
#define IS_DATAFIRQ(p)    (READ_AUDIO_STATUS(p)&0x0040)
#define IS_DATBFIRQ(p)    (READ_AUDIO_STATUS(p)&0x0080)
#define IS_EOBIRQ(p)      (READ_AUDIO_STATUS(p)&0x0100)	/* interrupt status */
#define IS_EOSIRQ(p)      (READ_AUDIO_STATUS(p)&0x0200)
#define IS_EOCIRQ(p)      (READ_AUDIO_STATUS(p)&0x0400)
#define IS_UNSLIRQ(p)     (READ_AUDIO_STATUS(p)&0x0800)
#define IS_SBIRQ(p)       (READ_AUDIO_STATUS(p)&0x1000)
#define IS_MPUIRQ(p)      (READ_AUDIO_STATUS(p)&0x2000)

#define RESP 0x00000001		/* command flags */
#define PARM 0x00000002
#define CMDA 0x00000004
#define CMDB 0x00000008
#define NILL 0x00000000

#define LONG0(a)   ((u32)a)	/* shifts and masks */
#define BYTE0(a)   (LONG0(a)&0xff)
#define BYTE1(a)   (BYTE0(a)<<8)
#define BYTE2(a)   (BYTE0(a)<<16)
#define BYTE3(a)   (BYTE0(a)<<24)
#define WORD0(a)   (LONG0(a)&0xffff)
#define WORD1(a)   (WORD0(a)<<8)
#define WORD2(a)   (WORD0(a)<<16)
#define TRINIB0(a) (LONG0(a)&0xffffff)
#define TRINIB1(a) (TRINIB0(a)<<8)

#define RET(a)     ((union cmdret *)(a))

#define SEND_GETV(p,b)             sendcmd(p,RESP,GETV,0,RET(b))	/* get version */
#define SEND_GETC(p,b,c)           sendcmd(p,PARM|RESP,GETC,c,RET(b))
#define SEND_GUNS(p,b)             sendcmd(p,RESP,GUNS,0,RET(b))
#define SEND_SCID(p,b)             sendcmd(p,RESP,SCID,0,RET(b))
#define SEND_RMEM(p,b,c,d)         sendcmd(p,PARM|RESP,RMEM|BYTE1(b),LONG0(c),RET(d))	/* memory access for firmware write */
#define SEND_SMEM(p,b,c)           sendcmd(p,PARM,SMEM|BYTE1(b),LONG0(c),RET(0))	/* memory access for firmware write */
#define SEND_WMEM(p,b,c)           sendcmd(p,PARM,WMEM|BYTE1(b),LONG0(c),RET(0))	/* memory access for firmware write */
#define SEND_SDTM(p,b,c)           sendcmd(p,PARM|RESP,SDTM|TRINIB1(b),0,RET(c))	/* memory access for firmware write */
#define SEND_GOTO(p,b)             sendcmd(p,PARM,GOTO,LONG0(b),RET(0))	/* memory access for firmware write */
#define SEND_SETDPLL(p)	           sendcmd(p,0,ARM_SETDPLL,0,RET(0))
#define SEND_SSTR(p,b,c)           sendcmd(p,PARM,SSTR|BYTE3(b),LONG0(c),RET(0))	/* start stream */
#define SEND_PSTR(p,b)             sendcmd(p,PARM,PSTR,BYTE3(b),RET(0))	/* pause stream */
#define SEND_KSTR(p,b)             sendcmd(p,PARM,KSTR,BYTE3(b),RET(0))	/* stop stream */
#define SEND_KDMA(p)               sendcmd(p,0,KDMA,0,RET(0))	/* stop all dma */
#define SEND_GPOS(p,b,c,d)         sendcmd(p,PARM|RESP,GPOS,BYTE3(c)|BYTE2(b),RET(d))	/* get position in dma */
#define SEND_SETF(p,b,c,d,e,f,g)   sendcmd(p,PARM,SETF|WORD1(b)|BYTE3(c),d|BYTE1(e)|BYTE2(f)|BYTE3(g),RET(0))	/* set sample format at mixer */
#define SEND_GSTS(p,b,c,d)         sendcmd(p,PARM|RESP,GSTS,BYTE3(c)|BYTE2(b),RET(d))
#define SEND_NGPOS(p,b,c,d)        sendcmd(p,PARM|RESP,NGPOS,BYTE3(c)|BYTE2(b),RET(d))
#define SEND_PSEL(p,b,c)           sendcmd(p,PARM,PSEL,BYTE2(b)|BYTE3(c),RET(0))	/* activate lbus path */
#define SEND_PCLR(p,b,c)           sendcmd(p,PARM,PCLR,BYTE2(b)|BYTE3(c),RET(0))	/* deactivate lbus path */
#define SEND_PLST(p,b)             sendcmd(p,PARM,PLST,BYTE3(b),RET(0))
#define SEND_RSSV(p,b,c,d)         sendcmd(p,PARM|RESP,RSSV,BYTE2(b)|BYTE3(c),RET(d))
#define SEND_LSEL(p,b,c,d,e,f,g,h) sendcmd(p,PARM,LSEL|BYTE1(b)|BYTE2(c)|BYTE3(d),BYTE0(e)|BYTE1(f)|BYTE2(g)|BYTE3(h),RET(0))	/* select paths for internal connections */
#define SEND_SSRC(p,b,c,d,e)       sendcmd(p,PARM,SSRC|BYTE1(b)|WORD2(c),WORD0(d)|WORD2(e),RET(0))	/* configure source */
#define SEND_SLST(p,b)             sendcmd(p,PARM,SLST,BYTE3(b),RET(0))
#define SEND_RSRC(p,b,c)           sendcmd(p,RESP,RSRC|BYTE1(b),0,RET(c))	/* read source config */
#define SEND_SSRB(p,b,c)           sendcmd(p,PARM,SSRB|BYTE1(b),WORD2(c),RET(0))
#define SEND_SDGV(p,b,c,d,e)       sendcmd(p,PARM,SDGV|BYTE2(b)|BYTE3(c),WORD0(d)|WORD2(e),RET(0))	/* set digital mixer */
#define SEND_RDGV(p,b,c,d)         sendcmd(p,PARM|RESP,RDGV|BYTE2(b)|BYTE3(c),0,RET(d))	/* read digital mixer */
#define SEND_DLST(p,b)             sendcmd(p,PARM,DLST,BYTE3(b),RET(0))
#define SEND_SACR(p,b,c)           sendcmd(p,PARM,SACR,WORD0(b)|WORD2(c),RET(0))	/* set AC97 register */
#define SEND_RACR(p,b,c)           sendcmd(p,PARM|RESP,RACR,WORD2(b),RET(c))	/* get AC97 register */
#define SEND_ALST(p,b)             sendcmd(p,PARM,ALST,BYTE3(b),RET(0))
#define SEND_TXAC(p,b,c,d,e,f)     sendcmd(p,PARM,TXAC|BYTE1(b)|WORD2(c),WORD0(d)|BYTE2(e)|BYTE3(f),RET(0))
#define SEND_RXAC(p,b,c,d)         sendcmd(p,PARM|RESP,RXAC,BYTE2(b)|BYTE3(c),RET(d))
#define SEND_SI2S(p,b)             sendcmd(p,PARM,SI2S,WORD2(b),RET(0))

#define EOB_STATUS         0x80000000	/* status flags : block boundary */
#define EOS_STATUS         0x40000000	/*              : stoppped */
#define EOC_STATUS         0x20000000	/*              : stream end */
#define ERR_STATUS         0x10000000
#define EMPTY_STATUS       0x08000000

#define IEOB_ENABLE        0x1	/* enable interrupts for status notification above */
#define IEOS_ENABLE        0x2
#define IEOC_ENABLE        0x4
#define RDONCE             0x8
#define DESC_MAX_MASK      0xff

#define ST_PLAY  0x1		/* stream states */
#define ST_STOP  0x2
#define ST_PAUSE 0x4

#define I2S_INTDEC     3	/* config for I2S link */
#define I2S_MERGER     0
#define I2S_SPLITTER   0
#define I2S_MIXER      7
#define I2S_RATE       44100

#define MODEM_INTDEC   4	/* config for modem link */
#define MODEM_MERGER   3
#define MODEM_SPLITTER 0
#define MODEM_MIXER    11

#define FM_INTDEC      3	/* config for FM/OPL3 link */
#define FM_MERGER      0
#define FM_SPLITTER    0
#define FM_MIXER       9

#define SPLIT_PATH  0x80	/* path splitting flag */

enum FIRMWARE {
	DATA_REC = 0, EXT_END_OF_FILE, EXT_SEG_ADDR_REC, EXT_GOTO_CMD_REC,
	EXT_LIN_ADDR_REC,
};

enum CMDS {
	GETV = 0x00, GETC, GUNS, SCID, RMEM =
	    0x10, SMEM, WMEM, SDTM, GOTO, SSTR =
	    0x20, PSTR, KSTR, KDMA, GPOS, SETF, GSTS, NGPOS, PSEL =
	    0x30, PCLR, PLST, RSSV, LSEL, SSRC = 0x40, SLST, RSRC, SSRB, SDGV =
	    0x50, RDGV, DLST, SACR = 0x60, RACR, ALST, TXAC, RXAC, SI2S =
	    0x70, ARM_SETDPLL = 0x72,
};

enum E1SOURCE {
	ARM2LBUS_FIFO0 = 0, ARM2LBUS_FIFO1, ARM2LBUS_FIFO2, ARM2LBUS_FIFO3,
	ARM2LBUS_FIFO4, ARM2LBUS_FIFO5, ARM2LBUS_FIFO6, ARM2LBUS_FIFO7,
	ARM2LBUS_FIFO8, ARM2LBUS_FIFO9, ARM2LBUS_FIFO10, ARM2LBUS_FIFO11,
	ARM2LBUS_FIFO12, ARM2LBUS_FIFO13, ARM2LBUS_FIFO14, ARM2LBUS_FIFO15,
	INTER0_OUT, INTER1_OUT, INTER2_OUT, INTER3_OUT, INTER4_OUT,
	INTERM0_OUT, INTERM1_OUT, INTERM2_OUT, INTERM3_OUT, INTERM4_OUT,
	INTERM5_OUT, INTERM6_OUT, DECIMM0_OUT, DECIMM1_OUT, DECIMM2_OUT,
	DECIMM3_OUT, DECIM0_OUT, SR3_4_OUT, OPL3_SAMPLE, ASRC0, ASRC1,
	ACLNK2PADC, ACLNK2MODEM0RX, ACLNK2MIC, ACLNK2MODEM1RX, ACLNK2HNDMIC,
	DIGITAL_MIXER_OUT0, GAINFUNC0_OUT, GAINFUNC1_OUT, GAINFUNC2_OUT,
	GAINFUNC3_OUT, GAINFUNC4_OUT, SOFTMODEMTX, SPLITTER0_OUTL,
	SPLITTER0_OUTR, SPLITTER1_OUTL, SPLITTER1_OUTR, SPLITTER2_OUTL,
	SPLITTER2_OUTR, SPLITTER3_OUTL, SPLITTER3_OUTR, MERGER0_OUT,
	MERGER1_OUT, MERGER2_OUT, MERGER3_OUT, ARM2LBUS_FIFO_DIRECT, NO_OUT
};

enum E2SINK {
	LBUS2ARM_FIFO0 = 0, LBUS2ARM_FIFO1, LBUS2ARM_FIFO2, LBUS2ARM_FIFO3,
	LBUS2ARM_FIFO4, LBUS2ARM_FIFO5, LBUS2ARM_FIFO6, LBUS2ARM_FIFO7,
	INTER0_IN, INTER1_IN, INTER2_IN, INTER3_IN, INTER4_IN, INTERM0_IN,
	INTERM1_IN, INTERM2_IN, INTERM3_IN, INTERM4_IN, INTERM5_IN, INTERM6_IN,
	DECIMM0_IN, DECIMM1_IN, DECIMM2_IN, DECIMM3_IN, DECIM0_IN, SR3_4_IN,
	PDAC2ACLNK, MODEM0TX2ACLNK, MODEM1TX2ACLNK, HNDSPK2ACLNK,
	DIGITAL_MIXER_IN0, DIGITAL_MIXER_IN1, DIGITAL_MIXER_IN2,
	DIGITAL_MIXER_IN3, DIGITAL_MIXER_IN4, DIGITAL_MIXER_IN5,
	DIGITAL_MIXER_IN6, DIGITAL_MIXER_IN7, DIGITAL_MIXER_IN8,
	DIGITAL_MIXER_IN9, DIGITAL_MIXER_IN10, DIGITAL_MIXER_IN11,
	GAINFUNC0_IN, GAINFUNC1_IN, GAINFUNC2_IN, GAINFUNC3_IN, GAINFUNC4_IN,
	SOFTMODEMRX, SPLITTER0_IN, SPLITTER1_IN, SPLITTER2_IN, SPLITTER3_IN,
	MERGER0_INL, MERGER0_INR, MERGER1_INL, MERGER1_INR, MERGER2_INL,
	MERGER2_INR, MERGER3_INL, MERGER3_INR, E2SINK_MAX
};

enum LBUS_SINK {
	LS_SRC_INTERPOLATOR = 0, LS_SRC_INTERPOLATORM, LS_SRC_DECIMATOR,
	LS_SRC_DECIMATORM, LS_MIXER_IN, LS_MIXER_GAIN_FUNCTION,
	LS_SRC_SPLITTER, LS_SRC_MERGER, LS_NONE1, LS_NONE2,
};

enum RT_CHANNEL_IDS {
	M0TX = 0, M1TX, TAMTX, HSSPKR, PDAC, DSNDTX0, DSNDTX1, DSNDTX2,
	DSNDTX3, DSNDTX4, DSNDTX5, DSNDTX6, DSNDTX7, WVSTRTX, COP3DTX, SPARE,
	M0RX, HSMIC, M1RX, CLEANRX, MICADC, PADC, COPRX1, COPRX2,
	CHANNEL_ID_COUNTER
};

enum { SB_CMD = 0, MODEM_CMD, I2S_CMD0, I2S_CMD1, FM_CMD, MAX_CMD };

struct lbuspath {
	unsigned char *noconv;
	unsigned char *stereo;
	unsigned char *mono;
};

struct cmdport {
	u32 data1;		/* cmd,param */
	u32 data2;		/* param */
	u32 stat;		/* status */
	u32 pad[5];
};

struct riptideport {
	u32 audio_control;	/* status registers */
	u32 audio_status;
	u32 pad[2];
	struct cmdport port[2];	/* command ports */
};

struct cmdif {
	struct riptideport *hwport;
	spinlock_t lock;
	unsigned int cmdcnt;	/* cmd statistics */
	unsigned int cmdtime;
	unsigned int cmdtimemax;
	unsigned int cmdtimemin;
	unsigned int errcnt;
	int is_reset;
};

struct riptide_firmware {
	u16 ASIC;
	u16 CODEC;
	u16 AUXDSP;
	u16 PROG;
};

union cmdret {
	u8 retbytes[8];
	u16 retwords[4];
	u32 retlongs[2];
};

union firmware_version {
	union cmdret ret;
	struct riptide_firmware firmware;
};

#define get_pcmhwdev(substream) (struct pcmhw *)(substream->runtime->private_data)

#define PLAYBACK_SUBSTREAMS 3
struct snd_riptide {
	struct snd_card *card;
	struct pci_dev *pci;
	const struct firmware *fw_entry;

	struct cmdif *cif;

	struct snd_pcm *pcm;
	struct snd_pcm *pcm_i2s;
	struct snd_rawmidi *rmidi;
	struct snd_opl3 *opl3;
	struct snd_ac97 *ac97;
	struct snd_ac97_bus *ac97_bus;

	struct snd_pcm_substream *playback_substream[PLAYBACK_SUBSTREAMS];
	struct snd_pcm_substream *capture_substream;

	int openstreams;

	int irq;
	unsigned long port;
	unsigned short mpuaddr;
	unsigned short opladdr;
#ifdef SUPPORT_JOYSTICK
	unsigned short gameaddr;
#endif
	struct resource *res_port;

	unsigned short device_id;

	union firmware_version firmware;

	spinlock_t lock;
	struct tasklet_struct riptide_tq;
	struct snd_info_entry *proc_entry;

	unsigned long received_irqs;
	unsigned long handled_irqs;
#ifdef CONFIG_PM_SLEEP
	int in_suspend;
#endif
};

struct sgd {			/* scatter gather desriptor */
	u32 dwNextLink;
	u32 dwSegPtrPhys;
	u32 dwSegLen;
	u32 dwStat_Ctl;
};

struct pcmhw {			/* pcm descriptor */
	struct lbuspath paths;
	unsigned char *lbuspath;
	unsigned char source;
	unsigned char intdec[2];
	unsigned char mixer;
	unsigned char id;
	unsigned char state;
	unsigned int rate;
	unsigned int channels;
	snd_pcm_format_t format;
	struct snd_dma_buffer sgdlist;
	struct sgd *sgdbuf;
	unsigned int size;
	unsigned int pages;
	unsigned int oldpos;
	unsigned int pointer;
};

#define CMDRET_ZERO (union cmdret){{(u32)0, (u32) 0}}

static int sendcmd(struct cmdif *cif, u32 flags, u32 cmd, u32 parm,
		   union cmdret *ret);
static int getsourcesink(struct cmdif *cif, unsigned char source,
			 unsigned char sink, unsigned char *a,
			 unsigned char *b);
static int snd_riptide_initialize(struct snd_riptide *chip);
static int riptide_reset(struct cmdif *cif, struct snd_riptide *chip);

/*
 */

static const struct pci_device_id snd_riptide_ids[] = {
	{ PCI_DEVICE(0x127a, 0x4310) },
	{ PCI_DEVICE(0x127a, 0x4320) },
	{ PCI_DEVICE(0x127a, 0x4330) },
	{ PCI_DEVICE(0x127a, 0x4340) },
	{0,},
};

#ifdef SUPPORT_JOYSTICK
static const struct pci_device_id snd_riptide_joystick_ids[] = {
	{ PCI_DEVICE(0x127a, 0x4312) },
	{ PCI_DEVICE(0x127a, 0x4322) },
	{ PCI_DEVICE(0x127a, 0x4332) },
	{ PCI_DEVICE(0x127a, 0x4342) },
	{0,},
};
#endif

MODULE_DEVICE_TABLE(pci, snd_riptide_ids);

/*
 */

static unsigned char lbusin2out[E2SINK_MAX + 1][2] = {
	{NO_OUT, LS_NONE1}, {NO_OUT, LS_NONE2}, {NO_OUT, LS_NONE1}, {NO_OUT,
								     LS_NONE2},
	{NO_OUT, LS_NONE1}, {NO_OUT, LS_NONE2}, {NO_OUT, LS_NONE1}, {NO_OUT,
								     LS_NONE2},
	{INTER0_OUT, LS_SRC_INTERPOLATOR}, {INTER1_OUT, LS_SRC_INTERPOLATOR},
	{INTER2_OUT, LS_SRC_INTERPOLATOR}, {INTER3_OUT, LS_SRC_INTERPOLATOR},
	{INTER4_OUT, LS_SRC_INTERPOLATOR}, {INTERM0_OUT, LS_SRC_INTERPOLATORM},
	{INTERM1_OUT, LS_SRC_INTERPOLATORM}, {INTERM2_OUT,
					      LS_SRC_INTERPOLATORM},
	{INTERM3_OUT, LS_SRC_INTERPOLATORM}, {INTERM4_OUT,
					      LS_SRC_INTERPOLATORM},
	{INTERM5_OUT, LS_SRC_INTERPOLATORM}, {INTERM6_OUT,
					      LS_SRC_INTERPOLATORM},
	{DECIMM0_OUT, LS_SRC_DECIMATORM}, {DECIMM1_OUT, LS_SRC_DECIMATORM},
	{DECIMM2_OUT, LS_SRC_DECIMATORM}, {DECIMM3_OUT, LS_SRC_DECIMATORM},
	{DECIM0_OUT, LS_SRC_DECIMATOR}, {SR3_4_OUT, LS_NONE1}, {NO_OUT,
								LS_NONE2},
	{NO_OUT, LS_NONE1}, {NO_OUT, LS_NONE2}, {NO_OUT, LS_NONE1},
	{DIGITAL_MIXER_OUT0, LS_MIXER_IN}, {DIGITAL_MIXER_OUT0, LS_MIXER_IN},
	{DIGITAL_MIXER_OUT0, LS_MIXER_IN}, {DIGITAL_MIXER_OUT0, LS_MIXER_IN},
	{DIGITAL_MIXER_OUT0, LS_MIXER_IN}, {DIGITAL_MIXER_OUT0, LS_MIXER_IN},
	{DIGITAL_MIXER_OUT0, LS_MIXER_IN}, {DIGITAL_MIXER_OUT0, LS_MIXER_IN},
	{DIGITAL_MIXER_OUT0, LS_MIXER_IN}, {DIGITAL_MIXER_OUT0, LS_MIXER_IN},
	{DIGITAL_MIXER_OUT0, LS_MIXER_IN}, {DIGITAL_MIXER_OUT0, LS_MIXER_IN},
	{GAINFUNC0_OUT, LS_MIXER_GAIN_FUNCTION}, {GAINFUNC1_OUT,
						  LS_MIXER_GAIN_FUNCTION},
	{GAINFUNC2_OUT, LS_MIXER_GAIN_FUNCTION}, {GAINFUNC3_OUT,
						  LS_MIXER_GAIN_FUNCTION},
	{GAINFUNC4_OUT, LS_MIXER_GAIN_FUNCTION}, {SOFTMODEMTX, LS_NONE1},
	{SPLITTER0_OUTL, LS_SRC_SPLITTER}, {SPLITTER1_OUTL, LS_SRC_SPLITTER},
	{SPLITTER2_OUTL, LS_SRC_SPLITTER}, {SPLITTER3_OUTL, LS_SRC_SPLITTER},
	{MERGER0_OUT, LS_SRC_MERGER}, {MERGER0_OUT, LS_SRC_MERGER},
	{MERGER1_OUT, LS_SRC_MERGER},
	{MERGER1_OUT, LS_SRC_MERGER}, {MERGER2_OUT, LS_SRC_MERGER},
	{MERGER2_OUT, LS_SRC_MERGER},
	{MERGER3_OUT, LS_SRC_MERGER}, {MERGER3_OUT, LS_SRC_MERGER}, {NO_OUT,
								     LS_NONE2},
};

static unsigned char lbus_play_opl3[] = {
	DIGITAL_MIXER_IN0 + FM_MIXER, 0xff
};
static unsigned char lbus_play_modem[] = {
	DIGITAL_MIXER_IN0 + MODEM_MIXER, 0xff
};
static unsigned char lbus_play_i2s[] = {
	INTER0_IN + I2S_INTDEC, DIGITAL_MIXER_IN0 + I2S_MIXER, 0xff
};
static unsigned char lbus_play_out[] = {
	PDAC2ACLNK, 0xff
};
static unsigned char lbus_play_outhp[] = {
	HNDSPK2ACLNK, 0xff
};
static unsigned char lbus_play_noconv1[] = {
	DIGITAL_MIXER_IN0, 0xff
};
static unsigned char lbus_play_stereo1[] = {
	INTER0_IN, DIGITAL_MIXER_IN0, 0xff
};
static unsigned char lbus_play_mono1[] = {
	INTERM0_IN, DIGITAL_MIXER_IN0, 0xff
};
static unsigned char lbus_play_noconv2[] = {
	DIGITAL_MIXER_IN1, 0xff
};
static unsigned char lbus_play_stereo2[] = {
	INTER1_IN, DIGITAL_MIXER_IN1, 0xff
};
static unsigned char lbus_play_mono2[] = {
	INTERM1_IN, DIGITAL_MIXER_IN1, 0xff
};
static unsigned char lbus_play_noconv3[] = {
	DIGITAL_MIXER_IN2, 0xff
};
static unsigned char lbus_play_stereo3[] = {
	INTER2_IN, DIGITAL_MIXER_IN2, 0xff
};
static unsigned char lbus_play_mono3[] = {
	INTERM2_IN, DIGITAL_MIXER_IN2, 0xff
};
static unsigned char lbus_rec_noconv1[] = {
	LBUS2ARM_FIFO5, 0xff
};
static unsigned char lbus_rec_stereo1[] = {
	DECIM0_IN, LBUS2ARM_FIFO5, 0xff
};
static unsigned char lbus_rec_mono1[] = {
	DECIMM3_IN, LBUS2ARM_FIFO5, 0xff
};

static unsigned char play_ids[] = { 4, 1, 2, };
static unsigned char play_sources[] = {
	ARM2LBUS_FIFO4, ARM2LBUS_FIFO1, ARM2LBUS_FIFO2,
};
static struct lbuspath lbus_play_paths[] = {
	{
	 .noconv = lbus_play_noconv1,
	 .stereo = lbus_play_stereo1,
	 .mono = lbus_play_mono1,
	 },
	{
	 .noconv = lbus_play_noconv2,
	 .stereo = lbus_play_stereo2,
	 .mono = lbus_play_mono2,
	 },
	{
	 .noconv = lbus_play_noconv3,
	 .stereo = lbus_play_stereo3,
	 .mono = lbus_play_mono3,
	 },
};
static const struct lbuspath lbus_rec_path = {
	.noconv = lbus_rec_noconv1,
	.stereo = lbus_rec_stereo1,
	.mono = lbus_rec_mono1,
};

#define FIRMWARE_VERSIONS 1
static union firmware_version firmware_versions[] = {
	{
		.firmware = {
			.ASIC = 3,
			.CODEC = 2,
			.AUXDSP = 3,
			.PROG = 773,
		},
	},
};

static u32 atoh(const unsigned char *in, unsigned int len)
{
	u32 sum = 0;
	unsigned int mult = 1;
	unsigned char c;

	while (len) {
		int value;

		c = in[len - 1];
		value = hex_to_bin(c);
		if (value >= 0)
			sum += mult * value;
		mult *= 16;
		--len;
	}
	return sum;
}

static int senddata(struct cmdif *cif, const unsigned char *in, u32 offset)
{
	u32 addr;
	u32 data;
	u32 i;
	const unsigned char *p;

	i = atoh(&in[1], 2);
	addr = offset + atoh(&in[3], 4);
	if (SEND_SMEM(cif, 0, addr) != 0)
		return -EACCES;
	p = in + 9;
	while (i) {
		data = atoh(p, 8);
		if (SEND_WMEM(cif, 2,
			      ((data & 0x0f0f0f0f) << 4) | ((data & 0xf0f0f0f0)
							    >> 4)))
			return -EACCES;
		i -= 4;
		p += 8;
	}
	return 0;
}

static int loadfirmware(struct cmdif *cif, const unsigned char *img,
			unsigned int size)
{
	const unsigned char *in;
	u32 laddr, saddr, t, val;
	int err = 0;

	laddr = saddr = 0;
	while (size > 0 && err == 0) {
		in = img;
		if (in[0] == ':') {
			t = atoh(&in[7], 2);
			switch (t) {
			case DATA_REC:
				err = senddata(cif, in, laddr + saddr);
				break;
			case EXT_SEG_ADDR_REC:
				saddr = atoh(&in[9], 4) << 4;
				break;
			case EXT_LIN_ADDR_REC:
				laddr = atoh(&in[9], 4) << 16;
				break;
			case EXT_GOTO_CMD_REC:
				val = atoh(&in[9], 8);
				if (SEND_GOTO(cif, val) != 0)
					err = -EACCES;
				break;
			case EXT_END_OF_FILE:
				size = 0;
				break;
			default:
				break;
			}
			while (size > 0) {
				size--;
				if (*img++ == '\n')
					break;
			}
		}
	}
	snd_printdd("load firmware return %d\n", err);
	return err;
}

static void
alloclbuspath(struct cmdif *cif, unsigned char source,
	      unsigned char *path, unsigned char *mixer, unsigned char *s)
{
	while (*path != 0xff) {
		unsigned char sink, type;

		sink = *path & (~SPLIT_PATH);
		if (sink != E2SINK_MAX) {
			snd_printdd("alloc path 0x%x->0x%x\n", source, sink);
			SEND_PSEL(cif, source, sink);
			source = lbusin2out[sink][0];
			type = lbusin2out[sink][1];
			if (type == LS_MIXER_IN) {
				if (mixer)
					*mixer = sink - DIGITAL_MIXER_IN0;
			}
			if (type == LS_SRC_DECIMATORM ||
			    type == LS_SRC_DECIMATOR ||
			    type == LS_SRC_INTERPOLATORM ||
			    type == LS_SRC_INTERPOLATOR) {
				if (s) {
					if (s[0] != 0xff)
						s[1] = sink;
					else
						s[0] = sink;
				}
			}
		}
		if (*path++ & SPLIT_PATH) {
			unsigned char *npath = path;

			while (*npath != 0xff)
				npath++;
			alloclbuspath(cif, source + 1, ++npath, mixer, s);
		}
	}
}

static void
freelbuspath(struct cmdif *cif, unsigned char source, unsigned char *path)
{
	while (*path != 0xff) {
		unsigned char sink;

		sink = *path & (~SPLIT_PATH);
		if (sink != E2SINK_MAX) {
			snd_printdd("free path 0x%x->0x%x\n", source, sink);
			SEND_PCLR(cif, source, sink);
			source = lbusin2out[sink][0];
		}
		if (*path++ & SPLIT_PATH) {
			unsigned char *npath = path;

			while (*npath != 0xff)
				npath++;
			freelbuspath(cif, source + 1, ++npath);
		}
	}
}

static int writearm(struct cmdif *cif, u32 addr, u32 data, u32 mask)
{
	union cmdret rptr = CMDRET_ZERO;
	unsigned int i = MAX_WRITE_RETRY;
	int flag = 1;

	SEND_RMEM(cif, 0x02, addr, &rptr);
	rptr.retlongs[0] &= (~mask);

	while (--i) {
		SEND_SMEM(cif, 0x01, addr);
		SEND_WMEM(cif, 0x02, (rptr.retlongs[0] | data));
		SEND_RMEM(cif, 0x02, addr, &rptr);
		if ((rptr.retlongs[0] & data) == data) {
			flag = 0;
			break;
		} else
			rptr.retlongs[0] &= ~mask;
	}
	snd_printdd("send arm 0x%x 0x%x 0x%x return %d\n", addr, data, mask,
		    flag);
	return flag;
}

static int sendcmd(struct cmdif *cif, u32 flags, u32 cmd, u32 parm,
		   union cmdret *ret)
{
	int i, j;
	int err;
	unsigned int time = 0;
	unsigned long irqflags;
	struct riptideport *hwport;
	struct cmdport *cmdport = NULL;

	if (snd_BUG_ON(!cif))
		return -EINVAL;

	hwport = cif->hwport;
	if (cif->errcnt > MAX_ERROR_COUNT) {
		if (cif->is_reset) {
			snd_printk(KERN_ERR
				   "Riptide: Too many failed cmds, reinitializing\n");
			if (riptide_reset(cif, NULL) == 0) {
				cif->errcnt = 0;
				return -EIO;
			}
		}
		snd_printk(KERN_ERR "Riptide: Initialization failed.\n");
		return -EINVAL;
	}
	if (ret) {
		ret->retlongs[0] = 0;
		ret->retlongs[1] = 0;
	}
	i = 0;
	spin_lock_irqsave(&cif->lock, irqflags);
	while (i++ < CMDIF_TIMEOUT && !IS_READY(cif->hwport))
		udelay(10);
	if (i > CMDIF_TIMEOUT) {
		err = -EBUSY;
		goto errout;
	}

	err = 0;
	for (j = 0, time = 0; time < CMDIF_TIMEOUT; j++, time += 2) {
		cmdport = &(hwport->port[j % 2]);
		if (IS_DATF(cmdport)) {	/* free pending data */
			READ_PORT_ULONG(cmdport->data1);
			READ_PORT_ULONG(cmdport->data2);
		}
		if (IS_CMDE(cmdport)) {
			if (flags & PARM)	/* put data */
				WRITE_PORT_ULONG(cmdport->data2, parm);
			WRITE_PORT_ULONG(cmdport->data1, cmd);	/* write cmd */
			if ((flags & RESP) && ret) {
				while (!IS_DATF(cmdport) &&
				       time < CMDIF_TIMEOUT) {
					udelay(10);
					time++;
				}
				if (time < CMDIF_TIMEOUT) {	/* read response */
					ret->retlongs[0] =
					    READ_PORT_ULONG(cmdport->data1);
					ret->retlongs[1] =
					    READ_PORT_ULONG(cmdport->data2);
				} else {
					err = -ENOSYS;
					goto errout;
				}
			}
			break;
		}
		udelay(20);
	}
	if (time == CMDIF_TIMEOUT) {
		err = -ENODATA;
		goto errout;
	}
	spin_unlock_irqrestore(&cif->lock, irqflags);

	cif->cmdcnt++;		/* update command statistics */
	cif->cmdtime += time;
	if (time > cif->cmdtimemax)
		cif->cmdtimemax = time;
	if (time < cif->cmdtimemin)
		cif->cmdtimemin = time;
	if ((cif->cmdcnt) % 1000 == 0)
		snd_printdd
		    ("send cmd %d time: %d mintime: %d maxtime %d err: %d\n",
		     cif->cmdcnt, cif->cmdtime, cif->cmdtimemin,
		     cif->cmdtimemax, cif->errcnt);
	return 0;

      errout:
	cif->errcnt++;
	spin_unlock_irqrestore(&cif->lock, irqflags);
	snd_printdd
	    ("send cmd %d hw: 0x%x flag: 0x%x cmd: 0x%x parm: 0x%x ret: 0x%x 0x%x CMDE: %d DATF: %d failed %d\n",
	     cif->cmdcnt, (int)((void *)&(cmdport->stat) - (void *)hwport),
	     flags, cmd, parm, ret ? ret->retlongs[0] : 0,
	     ret ? ret->retlongs[1] : 0, IS_CMDE(cmdport), IS_DATF(cmdport),
	     err);
	return err;
}

static int
setmixer(struct cmdif *cif, short num, unsigned short rval, unsigned short lval)
{
	union cmdret rptr = CMDRET_ZERO;
	int i = 0;

	snd_printdd("sent mixer %d: 0x%x 0x%x\n", num, rval, lval);
	do {
		SEND_SDGV(cif, num, num, rval, lval);
		SEND_RDGV(cif, num, num, &rptr);
		if (rptr.retwords[0] == lval && rptr.retwords[1] == rval)
			return 0;
	} while (i++ < MAX_WRITE_RETRY);
	snd_printdd("sent mixer failed\n");
	return -EIO;
}

static int getpaths(struct cmdif *cif, unsigned char *o)
{
	unsigned char src[E2SINK_MAX];
	unsigned char sink[E2SINK_MAX];
	int i, j = 0;

	for (i = 0; i < E2SINK_MAX; i++) {
		getsourcesink(cif, i, i, &src[i], &sink[i]);
		if (sink[i] < E2SINK_MAX) {
			o[j++] = sink[i];
			o[j++] = i;
		}
	}
	return j;
}

static int
getsourcesink(struct cmdif *cif, unsigned char source, unsigned char sink,
	      unsigned char *a, unsigned char *b)
{
	union cmdret rptr = CMDRET_ZERO;

	if (SEND_RSSV(cif, source, sink, &rptr) &&
	    SEND_RSSV(cif, source, sink, &rptr))
		return -EIO;
	*a = rptr.retbytes[0];
	*b = rptr.retbytes[1];
	snd_printdd("getsourcesink 0x%x 0x%x\n", *a, *b);
	return 0;
}

static int
getsamplerate(struct cmdif *cif, unsigned char *intdec, unsigned int *rate)
{
	unsigned char *s;
	unsigned int p[2] = { 0, 0 };
	int i;
	union cmdret rptr = CMDRET_ZERO;

	s = intdec;
	for (i = 0; i < 2; i++) {
		if (*s != 0xff) {
			if (SEND_RSRC(cif, *s, &rptr) &&
			    SEND_RSRC(cif, *s, &rptr))
				return -EIO;
			p[i] += rptr.retwords[1];
			p[i] *= rptr.retwords[2];
			p[i] += rptr.retwords[3];
			p[i] /= 65536;
		}
		s++;
	}
	if (p[0]) {
		if (p[1] != p[0])
			snd_printdd("rates differ %d %d\n", p[0], p[1]);
		*rate = (unsigned int)p[0];
	} else
		*rate = (unsigned int)p[1];
	snd_printdd("getsampleformat %d %d %d\n", intdec[0], intdec[1], *rate);
	return 0;
}

static int
setsampleformat(struct cmdif *cif,
		unsigned char mixer, unsigned char id,
		unsigned char channels, unsigned char format)
{
	unsigned char w, ch, sig, order;

	snd_printdd
	    ("setsampleformat mixer: %d id: %d channels: %d format: %d\n",
	     mixer, id, channels, format);
	ch = channels == 1;
	w = snd_pcm_format_width(format) == 8;
	sig = snd_pcm_format_unsigned(format) != 0;
	order = snd_pcm_format_big_endian(format) != 0;

	if (SEND_SETF(cif, mixer, w, ch, order, sig, id) &&
	    SEND_SETF(cif, mixer, w, ch, order, sig, id)) {
		snd_printdd("setsampleformat failed\n");
		return -EIO;
	}
	return 0;
}

static int
setsamplerate(struct cmdif *cif, unsigned char *intdec, unsigned int rate)
{
	u32 D, M, N;
	union cmdret rptr = CMDRET_ZERO;
	int i;

	snd_printdd("setsamplerate intdec: %d,%d rate: %d\n", intdec[0],
		    intdec[1], rate);
	D = 48000;
	M = ((rate == 48000) ? 47999 : rate) * 65536;
	N = M % D;
	M /= D;
	for (i = 0; i < 2; i++) {
		if (*intdec != 0xff) {
			do {
				SEND_SSRC(cif, *intdec, D, M, N);
				SEND_RSRC(cif, *intdec, &rptr);
			} while (rptr.retwords[1] != D &&
				 rptr.retwords[2] != M &&
				 rptr.retwords[3] != N &&
				 i++ < MAX_WRITE_RETRY);
			if (i > MAX_WRITE_RETRY) {
				snd_printdd("sent samplerate %d: %d failed\n",
					    *intdec, rate);
				return -EIO;
			}
		}
		intdec++;
	}
	return 0;
}

static int
getmixer(struct cmdif *cif, short num, unsigned short *rval,
	 unsigned short *lval)
{
	union cmdret rptr = CMDRET_ZERO;

	if (SEND_RDGV(cif, num, num, &rptr) && SEND_RDGV(cif, num, num, &rptr))
		return -EIO;
	*rval = rptr.retwords[0];
	*lval = rptr.retwords[1];
	snd_printdd("got mixer %d: 0x%x 0x%x\n", num, *rval, *lval);
	return 0;
}

static void riptide_handleirq(unsigned long dev_id)
{
	struct snd_riptide *chip = (void *)dev_id;
	struct cmdif *cif = chip->cif;
	struct snd_pcm_substream *substream[PLAYBACK_SUBSTREAMS + 1];
	struct snd_pcm_runtime *runtime;
	struct pcmhw *data = NULL;
	unsigned int pos, period_bytes;
	struct sgd *c;
	int i, j;
	unsigned int flag;

	if (!cif)
		return;

	for (i = 0; i < PLAYBACK_SUBSTREAMS; i++)
		substream[i] = chip->playback_substream[i];
	substream[i] = chip->capture_substream;
	for (i = 0; i < PLAYBACK_SUBSTREAMS + 1; i++) {
		if (substream[i] &&
		    (runtime = substream[i]->runtime) &&
		    (data = runtime->private_data) && data->state != ST_STOP) {
			pos = 0;
			for (j = 0; j < data->pages; j++) {
				c = &data->sgdbuf[j];
				flag = le32_to_cpu(c->dwStat_Ctl);
				if (flag & EOB_STATUS)
					pos += le32_to_cpu(c->dwSegLen);
				if (flag & EOC_STATUS)
					pos += le32_to_cpu(c->dwSegLen);
				if ((flag & EOS_STATUS)
				    && (data->state == ST_PLAY)) {
					data->state = ST_STOP;
					snd_printk(KERN_ERR
						   "Riptide: DMA stopped unexpectedly\n");
				}
				c->dwStat_Ctl =
				    cpu_to_le32(flag &
						~(EOS_STATUS | EOB_STATUS |
						  EOC_STATUS));
			}
			data->pointer += pos;
			pos += data->oldpos;
			if (data->state != ST_STOP) {
				period_bytes =
				    frames_to_bytes(runtime,
						    runtime->period_size);
				snd_printdd
				    ("interrupt 0x%x after 0x%lx of 0x%lx frames in period\n",
				     READ_AUDIO_STATUS(cif->hwport),
				     bytes_to_frames(runtime, pos),
				     runtime->period_size);
				j = 0;
				if (pos >= period_bytes) {
					j++;
					while (pos >= period_bytes)
						pos -= period_bytes;
				}
				data->oldpos = pos;
				if (j > 0)
					snd_pcm_period_elapsed(substream[i]);
			}
		}
	}
}

#ifdef CONFIG_PM_SLEEP
static int riptide_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_riptide *chip = card->private_data;

	chip->in_suspend = 1;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_ac97_suspend(chip->ac97);
	return 0;
}

static int riptide_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_riptide *chip = card->private_data;

	snd_riptide_initialize(chip);
	snd_ac97_resume(chip->ac97);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	chip->in_suspend = 0;
	return 0;
}

static SIMPLE_DEV_PM_OPS(riptide_pm, riptide_suspend, riptide_resume);
#define RIPTIDE_PM_OPS	&riptide_pm
#else
#define RIPTIDE_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static int try_to_load_firmware(struct cmdif *cif, struct snd_riptide *chip)
{
	union firmware_version firmware = { .ret = CMDRET_ZERO };
	int i, timeout, err;

	for (i = 0; i < 2; i++) {
		WRITE_PORT_ULONG(cif->hwport->port[i].data1, 0);
		WRITE_PORT_ULONG(cif->hwport->port[i].data2, 0);
	}
	SET_GRESET(cif->hwport);
	udelay(100);
	UNSET_GRESET(cif->hwport);
	udelay(100);

	for (timeout = 100000; --timeout; udelay(10)) {
		if (IS_READY(cif->hwport) && !IS_GERR(cif->hwport))
			break;
	}
	if (!timeout) {
		snd_printk(KERN_ERR
			   "Riptide: device not ready, audio status: 0x%x "
			   "ready: %d gerr: %d\n",
			   READ_AUDIO_STATUS(cif->hwport),
			   IS_READY(cif->hwport), IS_GERR(cif->hwport));
		return -EIO;
	} else {
		snd_printdd
			("Riptide: audio status: 0x%x ready: %d gerr: %d\n",
			 READ_AUDIO_STATUS(cif->hwport),
			 IS_READY(cif->hwport), IS_GERR(cif->hwport));
	}

	SEND_GETV(cif, &firmware.ret);
	snd_printdd("Firmware version: ASIC: %d CODEC %d AUXDSP %d PROG %d\n",
		    firmware.firmware.ASIC, firmware.firmware.CODEC,
		    firmware.firmware.AUXDSP, firmware.firmware.PROG);

	if (!chip)
		return 1;

	for (i = 0; i < FIRMWARE_VERSIONS; i++) {
		if (!memcmp(&firmware_versions[i], &firmware, sizeof(firmware)))
			return 1; /* OK */

	}

	snd_printdd("Writing Firmware\n");
	if (!chip->fw_entry) {
		err = request_firmware(&chip->fw_entry, "riptide.hex",
				       &chip->pci->dev);
		if (err) {
			snd_printk(KERN_ERR
				   "Riptide: Firmware not available %d\n", err);
			return -EIO;
		}
	}
	err = loadfirmware(cif, chip->fw_entry->data, chip->fw_entry->size);
	if (err) {
		snd_printk(KERN_ERR
			   "Riptide: Could not load firmware %d\n", err);
		return err;
	}

	chip->firmware = firmware;

	return 1; /* OK */
}

static int riptide_reset(struct cmdif *cif, struct snd_riptide *chip)
{
	union cmdret rptr = CMDRET_ZERO;
	int err, tries;

	if (!cif)
		return -EINVAL;

	cif->cmdcnt = 0;
	cif->cmdtime = 0;
	cif->cmdtimemax = 0;
	cif->cmdtimemin = 0xffffffff;
	cif->errcnt = 0;
	cif->is_reset = 0;

	tries = RESET_TRIES;
	do {
		err = try_to_load_firmware(cif, chip);
		if (err < 0)
			return err;
	} while (!err && --tries);

	SEND_SACR(cif, 0, AC97_RESET);
	SEND_RACR(cif, AC97_RESET, &rptr);
	snd_printdd("AC97: 0x%x 0x%x\n", rptr.retlongs[0], rptr.retlongs[1]);

	SEND_PLST(cif, 0);
	SEND_SLST(cif, 0);
	SEND_DLST(cif, 0);
	SEND_ALST(cif, 0);
	SEND_KDMA(cif);

	writearm(cif, 0x301F8, 1, 1);
	writearm(cif, 0x301F4, 1, 1);

	SEND_LSEL(cif, MODEM_CMD, 0, 0, MODEM_INTDEC, MODEM_MERGER,
		  MODEM_SPLITTER, MODEM_MIXER);
	setmixer(cif, MODEM_MIXER, 0x7fff, 0x7fff);
	alloclbuspath(cif, ARM2LBUS_FIFO13, lbus_play_modem, NULL, NULL);

	SEND_LSEL(cif, FM_CMD, 0, 0, FM_INTDEC, FM_MERGER, FM_SPLITTER,
		  FM_MIXER);
	setmixer(cif, FM_MIXER, 0x7fff, 0x7fff);
	writearm(cif, 0x30648 + FM_MIXER * 4, 0x01, 0x00000005);
	writearm(cif, 0x301A8, 0x02, 0x00000002);
	writearm(cif, 0x30264, 0x08, 0xffffffff);
	alloclbuspath(cif, OPL3_SAMPLE, lbus_play_opl3, NULL, NULL);

	SEND_SSRC(cif, I2S_INTDEC, 48000,
		  ((u32) I2S_RATE * 65536) / 48000,
		  ((u32) I2S_RATE * 65536) % 48000);
	SEND_LSEL(cif, I2S_CMD0, 0, 0, I2S_INTDEC, I2S_MERGER, I2S_SPLITTER,
		  I2S_MIXER);
	SEND_SI2S(cif, 1);
	alloclbuspath(cif, ARM2LBUS_FIFO0, lbus_play_i2s, NULL, NULL);
	alloclbuspath(cif, DIGITAL_MIXER_OUT0, lbus_play_out, NULL, NULL);
	alloclbuspath(cif, DIGITAL_MIXER_OUT0, lbus_play_outhp, NULL, NULL);

	SET_AIACK(cif->hwport);
	SET_AIE(cif->hwport);
	SET_AIACK(cif->hwport);
	cif->is_reset = 1;

	return 0;
}

static struct snd_pcm_hardware snd_riptide_playback = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =
	    SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8
	    | SNDRV_PCM_FMTBIT_U16_LE,
	.rates = SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min = 5500,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = (64 * 1024),
	.period_bytes_min = PAGE_SIZE >> 1,
	.period_bytes_max = PAGE_SIZE << 8,
	.periods_min = 2,
	.periods_max = 64,
	.fifo_size = 0,
};
static struct snd_pcm_hardware snd_riptide_capture = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_MMAP_VALID),
	.formats =
	    SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8
	    | SNDRV_PCM_FMTBIT_U16_LE,
	.rates = SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min = 5500,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = (64 * 1024),
	.period_bytes_min = PAGE_SIZE >> 1,
	.period_bytes_max = PAGE_SIZE << 3,
	.periods_min = 2,
	.periods_max = 64,
	.fifo_size = 0,
};

static snd_pcm_uframes_t snd_riptide_pointer(struct snd_pcm_substream
					     *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcmhw *data = get_pcmhwdev(substream);
	struct cmdif *cif = chip->cif;
	union cmdret rptr = CMDRET_ZERO;
	snd_pcm_uframes_t ret;

	SEND_GPOS(cif, 0, data->id, &rptr);
	if (data->size && runtime->period_size) {
		snd_printdd
		    ("pointer stream %d position 0x%x(0x%x in buffer) bytes 0x%lx(0x%lx in period) frames\n",
		     data->id, rptr.retlongs[1], rptr.retlongs[1] % data->size,
		     bytes_to_frames(runtime, rptr.retlongs[1]),
		     bytes_to_frames(runtime,
				     rptr.retlongs[1]) % runtime->period_size);
		if (rptr.retlongs[1] > data->pointer)
			ret =
			    bytes_to_frames(runtime,
					    rptr.retlongs[1] % data->size);
		else
			ret =
			    bytes_to_frames(runtime,
					    data->pointer % data->size);
	} else {
		snd_printdd("stream not started or strange parms (%d %ld)\n",
			    data->size, runtime->period_size);
		ret = bytes_to_frames(runtime, 0);
	}
	return ret;
}

static int snd_riptide_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int i, j;
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct pcmhw *data = get_pcmhwdev(substream);
	struct cmdif *cif = chip->cif;
	union cmdret rptr = CMDRET_ZERO;

	spin_lock(&chip->lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (!(data->state & ST_PLAY)) {
			SEND_SSTR(cif, data->id, data->sgdlist.addr);
			SET_AIE(cif->hwport);
			data->state = ST_PLAY;
			if (data->mixer != 0xff)
				setmixer(cif, data->mixer, 0x7fff, 0x7fff);
			chip->openstreams++;
			data->oldpos = 0;
			data->pointer = 0;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (data->mixer != 0xff)
			setmixer(cif, data->mixer, 0, 0);
		setmixer(cif, data->mixer, 0, 0);
		SEND_KSTR(cif, data->id);
		data->state = ST_STOP;
		chip->openstreams--;
		j = 0;
		do {
			i = rptr.retlongs[1];
			SEND_GPOS(cif, 0, data->id, &rptr);
			udelay(1);
		} while (i != rptr.retlongs[1] && j++ < MAX_WRITE_RETRY);
		if (j > MAX_WRITE_RETRY)
			snd_printk(KERN_ERR "Riptide: Could not stop stream!");
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!(data->state & ST_PAUSE)) {
			SEND_PSTR(cif, data->id);
			data->state |= ST_PAUSE;
			chip->openstreams--;
		}
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (data->state & ST_PAUSE) {
			SEND_SSTR(cif, data->id, data->sgdlist.addr);
			data->state &= ~ST_PAUSE;
			chip->openstreams++;
		}
		break;
	default:
		spin_unlock(&chip->lock);
		return -EINVAL;
	}
	spin_unlock(&chip->lock);
	return 0;
}

static int snd_riptide_prepare(struct snd_pcm_substream *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcmhw *data = get_pcmhwdev(substream);
	struct cmdif *cif = chip->cif;
	unsigned char *lbuspath = NULL;
	unsigned int rate, channels;
	int err = 0;
	snd_pcm_format_t format;

	if (snd_BUG_ON(!cif || !data))
		return -EINVAL;

	snd_printdd("prepare id %d ch: %d f:0x%x r:%d\n", data->id,
		    runtime->channels, runtime->format, runtime->rate);

	spin_lock_irq(&chip->lock);
	channels = runtime->channels;
	format = runtime->format;
	rate = runtime->rate;
	switch (channels) {
	case 1:
		if (rate == 48000 && format == SNDRV_PCM_FORMAT_S16_LE)
			lbuspath = data->paths.noconv;
		else
			lbuspath = data->paths.mono;
		break;
	case 2:
		if (rate == 48000 && format == SNDRV_PCM_FORMAT_S16_LE)
			lbuspath = data->paths.noconv;
		else
			lbuspath = data->paths.stereo;
		break;
	}
	snd_printdd("use sgdlist at 0x%p\n",
		    data->sgdlist.area);
	if (data->sgdlist.area) {
		unsigned int i, j, size, pages, f, pt, period;
		struct sgd *c, *p = NULL;

		size = frames_to_bytes(runtime, runtime->buffer_size);
		period = frames_to_bytes(runtime, runtime->period_size);
		f = PAGE_SIZE;
		while ((size + (f >> 1) - 1) <= (f << 7) && (f << 1) > period)
			f = f >> 1;
		pages = DIV_ROUND_UP(size, f);
		data->size = size;
		data->pages = pages;
		snd_printdd
		    ("create sgd size: 0x%x pages %d of size 0x%x for period 0x%x\n",
		     size, pages, f, period);
		pt = 0;
		j = 0;
		for (i = 0; i < pages; i++) {
			unsigned int ofs, addr;
			c = &data->sgdbuf[i];
			if (p)
				p->dwNextLink = cpu_to_le32(data->sgdlist.addr +
							    (i *
							     sizeof(struct
								    sgd)));
			c->dwNextLink = cpu_to_le32(data->sgdlist.addr);
			ofs = j << PAGE_SHIFT;
			addr = snd_pcm_sgbuf_get_addr(substream, ofs) + pt;
			c->dwSegPtrPhys = cpu_to_le32(addr);
			pt = (pt + f) % PAGE_SIZE;
			if (pt == 0)
				j++;
			c->dwSegLen = cpu_to_le32(f);
			c->dwStat_Ctl =
			    cpu_to_le32(IEOB_ENABLE | IEOS_ENABLE |
					IEOC_ENABLE);
			p = c;
			size -= f;
		}
		data->sgdbuf[i].dwSegLen = cpu_to_le32(size);
	}
	if (lbuspath && lbuspath != data->lbuspath) {
		if (data->lbuspath)
			freelbuspath(cif, data->source, data->lbuspath);
		alloclbuspath(cif, data->source, lbuspath,
			      &data->mixer, data->intdec);
		data->lbuspath = lbuspath;
		data->rate = 0;
	}
	if (data->rate != rate || data->format != format ||
	    data->channels != channels) {
		data->rate = rate;
		data->format = format;
		data->channels = channels;
		if (setsampleformat
		    (cif, data->mixer, data->id, channels, format)
		    || setsamplerate(cif, data->intdec, rate))
			err = -EIO;
	}
	spin_unlock_irq(&chip->lock);
	return err;
}

static int
snd_riptide_hw_params(struct snd_pcm_substream *substream,
		      struct snd_pcm_hw_params *hw_params)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct pcmhw *data = get_pcmhwdev(substream);
	struct snd_dma_buffer *sgdlist = &data->sgdlist;
	int err;

	snd_printdd("hw params id %d (sgdlist: 0x%p 0x%lx %d)\n", data->id,
		    sgdlist->area, (unsigned long)sgdlist->addr,
		    (int)sgdlist->bytes);
	if (sgdlist->area)
		snd_dma_free_pages(sgdlist);
	if ((err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
				       snd_dma_pci_data(chip->pci),
				       sizeof(struct sgd) * (DESC_MAX_MASK + 1),
				       sgdlist)) < 0) {
		snd_printk(KERN_ERR "Riptide: failed to alloc %d dma bytes\n",
			   (int)sizeof(struct sgd) * (DESC_MAX_MASK + 1));
		return err;
	}
	data->sgdbuf = (struct sgd *)sgdlist->area;
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int snd_riptide_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct pcmhw *data = get_pcmhwdev(substream);
	struct cmdif *cif = chip->cif;

	if (cif && data) {
		if (data->lbuspath)
			freelbuspath(cif, data->source, data->lbuspath);
		data->lbuspath = NULL;
		data->source = 0xff;
		data->intdec[0] = 0xff;
		data->intdec[1] = 0xff;

		if (data->sgdlist.area) {
			snd_dma_free_pages(&data->sgdlist);
			data->sgdlist.area = NULL;
		}
	}
	return snd_pcm_lib_free_pages(substream);
}

static int snd_riptide_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcmhw *data;
	int sub_num = substream->number;

	chip->playback_substream[sub_num] = substream;
	runtime->hw = snd_riptide_playback;

	data = kzalloc(sizeof(struct pcmhw), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	data->paths = lbus_play_paths[sub_num];
	data->id = play_ids[sub_num];
	data->source = play_sources[sub_num];
	data->intdec[0] = 0xff;
	data->intdec[1] = 0xff;
	data->state = ST_STOP;
	runtime->private_data = data;
	return snd_pcm_hw_constraint_integer(runtime,
					     SNDRV_PCM_HW_PARAM_PERIODS);
}

static int snd_riptide_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pcmhw *data;

	chip->capture_substream = substream;
	runtime->hw = snd_riptide_capture;

	data = kzalloc(sizeof(struct pcmhw), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	data->paths = lbus_rec_path;
	data->id = PADC;
	data->source = ACLNK2PADC;
	data->intdec[0] = 0xff;
	data->intdec[1] = 0xff;
	data->state = ST_STOP;
	runtime->private_data = data;
	return snd_pcm_hw_constraint_integer(runtime,
					     SNDRV_PCM_HW_PARAM_PERIODS);
}

static int snd_riptide_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct pcmhw *data = get_pcmhwdev(substream);
	int sub_num = substream->number;

	substream->runtime->private_data = NULL;
	chip->playback_substream[sub_num] = NULL;
	kfree(data);
	return 0;
}

static int snd_riptide_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_riptide *chip = snd_pcm_substream_chip(substream);
	struct pcmhw *data = get_pcmhwdev(substream);

	substream->runtime->private_data = NULL;
	chip->capture_substream = NULL;
	kfree(data);
	return 0;
}

static const struct snd_pcm_ops snd_riptide_playback_ops = {
	.open = snd_riptide_playback_open,
	.close = snd_riptide_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_riptide_hw_params,
	.hw_free = snd_riptide_hw_free,
	.prepare = snd_riptide_prepare,
	.page = snd_pcm_sgbuf_ops_page,
	.trigger = snd_riptide_trigger,
	.pointer = snd_riptide_pointer,
};
static const struct snd_pcm_ops snd_riptide_capture_ops = {
	.open = snd_riptide_capture_open,
	.close = snd_riptide_capture_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_riptide_hw_params,
	.hw_free = snd_riptide_hw_free,
	.prepare = snd_riptide_prepare,
	.page = snd_pcm_sgbuf_ops_page,
	.trigger = snd_riptide_trigger,
	.pointer = snd_riptide_pointer,
};

static int snd_riptide_pcm(struct snd_riptide *chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err =
	     snd_pcm_new(chip->card, "RIPTIDE", device, PLAYBACK_SUBSTREAMS, 1,
			 &pcm)) < 0)
		return err;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_riptide_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_riptide_capture_ops);
	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, "RIPTIDE");
	chip->pcm = pcm;
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
					      snd_dma_pci_data(chip->pci),
					      64 * 1024, 128 * 1024);
	return 0;
}

static irqreturn_t
snd_riptide_interrupt(int irq, void *dev_id)
{
	struct snd_riptide *chip = dev_id;
	struct cmdif *cif = chip->cif;

	if (cif) {
		chip->received_irqs++;
		if (IS_EOBIRQ(cif->hwport) || IS_EOSIRQ(cif->hwport) ||
		    IS_EOCIRQ(cif->hwport)) {
			chip->handled_irqs++;
			tasklet_schedule(&chip->riptide_tq);
		}
		if (chip->rmidi && IS_MPUIRQ(cif->hwport)) {
			chip->handled_irqs++;
			snd_mpu401_uart_interrupt(irq,
						  chip->rmidi->private_data);
		}
		SET_AIACK(cif->hwport);
	}
	return IRQ_HANDLED;
}

static void
snd_riptide_codec_write(struct snd_ac97 *ac97, unsigned short reg,
			unsigned short val)
{
	struct snd_riptide *chip = ac97->private_data;
	struct cmdif *cif = chip->cif;
	union cmdret rptr = CMDRET_ZERO;
	int i = 0;

	if (snd_BUG_ON(!cif))
		return;

	snd_printdd("Write AC97 reg 0x%x 0x%x\n", reg, val);
	do {
		SEND_SACR(cif, val, reg);
		SEND_RACR(cif, reg, &rptr);
	} while (rptr.retwords[1] != val && i++ < MAX_WRITE_RETRY);
	if (i > MAX_WRITE_RETRY)
		snd_printdd("Write AC97 reg failed\n");
}

static unsigned short snd_riptide_codec_read(struct snd_ac97 *ac97,
					     unsigned short reg)
{
	struct snd_riptide *chip = ac97->private_data;
	struct cmdif *cif = chip->cif;
	union cmdret rptr = CMDRET_ZERO;

	if (snd_BUG_ON(!cif))
		return 0;

	if (SEND_RACR(cif, reg, &rptr) != 0)
		SEND_RACR(cif, reg, &rptr);
	snd_printdd("Read AC97 reg 0x%x got 0x%x\n", reg, rptr.retwords[1]);
	return rptr.retwords[1];
}

static int snd_riptide_initialize(struct snd_riptide *chip)
{
	struct cmdif *cif;
	unsigned int device_id;
	int err;

	if (snd_BUG_ON(!chip))
		return -EINVAL;

	cif = chip->cif;
	if (!cif) {
		if ((cif = kzalloc(sizeof(struct cmdif), GFP_KERNEL)) == NULL)
			return -ENOMEM;
		cif->hwport = (struct riptideport *)chip->port;
		spin_lock_init(&cif->lock);
		chip->cif = cif;
	}
	cif->is_reset = 0;
	if ((err = riptide_reset(cif, chip)) != 0)
		return err;
	device_id = chip->device_id;
	switch (device_id) {
	case 0x4310:
	case 0x4320:
	case 0x4330:
		snd_printdd("Modem enable?\n");
		SEND_SETDPLL(cif);
		break;
	}
	snd_printdd("Enabling MPU IRQs\n");
	if (chip->rmidi)
		SET_EMPUIRQ(cif->hwport);
	return err;
}

static int snd_riptide_free(struct snd_riptide *chip)
{
	struct cmdif *cif;

	if (!chip)
		return 0;

	if ((cif = chip->cif)) {
		SET_GRESET(cif->hwport);
		udelay(100);
		UNSET_GRESET(cif->hwport);
		kfree(chip->cif);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	release_firmware(chip->fw_entry);
	release_and_free_resource(chip->res_port);
	kfree(chip);
	return 0;
}

static int snd_riptide_dev_free(struct snd_device *device)
{
	struct snd_riptide *chip = device->device_data;

	return snd_riptide_free(chip);
}

static int
snd_riptide_create(struct snd_card *card, struct pci_dev *pci,
		   struct snd_riptide **rchip)
{
	struct snd_riptide *chip;
	struct riptideport *hwport;
	int err;
	static struct snd_device_ops ops = {
		.dev_free = snd_riptide_dev_free,
	};

	*rchip = NULL;
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	if (!(chip = kzalloc(sizeof(struct snd_riptide), GFP_KERNEL)))
		return -ENOMEM;

	spin_lock_init(&chip->lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->openstreams = 0;
	chip->port = pci_resource_start(pci, 0);
	chip->received_irqs = 0;
	chip->handled_irqs = 0;
	chip->cif = NULL;
	tasklet_init(&chip->riptide_tq, riptide_handleirq, (unsigned long)chip);

	if ((chip->res_port =
	     request_region(chip->port, 64, "RIPTIDE")) == NULL) {
		snd_printk(KERN_ERR
			   "Riptide: unable to grab region 0x%lx-0x%lx\n",
			   chip->port, chip->port + 64 - 1);
		snd_riptide_free(chip);
		return -EBUSY;
	}
	hwport = (struct riptideport *)chip->port;
	UNSET_AIE(hwport);

	if (request_irq(pci->irq, snd_riptide_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		snd_printk(KERN_ERR "Riptide: unable to grab IRQ %d\n",
			   pci->irq);
		snd_riptide_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	chip->device_id = pci->device;
	pci_set_master(pci);
	if ((err = snd_riptide_initialize(chip)) < 0) {
		snd_riptide_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_riptide_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

static void
snd_riptide_proc_read(struct snd_info_entry *entry,
		      struct snd_info_buffer *buffer)
{
	struct snd_riptide *chip = entry->private_data;
	struct pcmhw *data;
	int i;
	struct cmdif *cif = NULL;
	unsigned char p[256];
	unsigned short rval = 0, lval = 0;
	unsigned int rate;

	if (!chip)
		return;

	snd_iprintf(buffer, "%s\n\n", chip->card->longname);
	snd_iprintf(buffer, "Device ID: 0x%x\nReceived IRQs: (%ld)%ld\nPorts:",
		    chip->device_id, chip->handled_irqs, chip->received_irqs);
	for (i = 0; i < 64; i += 4)
		snd_iprintf(buffer, "%c%02x: %08x",
			    (i % 16) ? ' ' : '\n', i, inl(chip->port + i));
	if ((cif = chip->cif)) {
		snd_iprintf(buffer,
			    "\nVersion: ASIC: %d CODEC: %d AUXDSP: %d PROG: %d",
			    chip->firmware.firmware.ASIC,
			    chip->firmware.firmware.CODEC,
			    chip->firmware.firmware.AUXDSP,
			    chip->firmware.firmware.PROG);
		snd_iprintf(buffer, "\nDigital mixer:");
		for (i = 0; i < 12; i++) {
			getmixer(cif, i, &rval, &lval);
			snd_iprintf(buffer, "\n %d: %d %d", i, rval, lval);
		}
		snd_iprintf(buffer,
			    "\nARM Commands num: %d failed: %d time: %d max: %d min: %d",
			    cif->cmdcnt, cif->errcnt,
			    cif->cmdtime, cif->cmdtimemax, cif->cmdtimemin);
	}
	snd_iprintf(buffer, "\nOpen streams %d:\n", chip->openstreams);
	for (i = 0; i < PLAYBACK_SUBSTREAMS; i++) {
		if (chip->playback_substream[i]
		    && chip->playback_substream[i]->runtime
		    && (data =
			chip->playback_substream[i]->runtime->private_data)) {
			snd_iprintf(buffer,
				    "stream: %d mixer: %d source: %d (%d,%d)\n",
				    data->id, data->mixer, data->source,
				    data->intdec[0], data->intdec[1]);
			if (!(getsamplerate(cif, data->intdec, &rate)))
				snd_iprintf(buffer, "rate: %d\n", rate);
		}
	}
	if (chip->capture_substream
	    && chip->capture_substream->runtime
	    && (data = chip->capture_substream->runtime->private_data)) {
		snd_iprintf(buffer,
			    "stream: %d mixer: %d source: %d (%d,%d)\n",
			    data->id, data->mixer,
			    data->source, data->intdec[0], data->intdec[1]);
		if (!(getsamplerate(cif, data->intdec, &rate)))
			snd_iprintf(buffer, "rate: %d\n", rate);
	}
	snd_iprintf(buffer, "Paths:\n");
	i = getpaths(cif, p);
	while (i >= 2) {
		i -= 2;
		snd_iprintf(buffer, "%x->%x ", p[i], p[i + 1]);
	}
	snd_iprintf(buffer, "\n");
}

static void snd_riptide_proc_init(struct snd_riptide *chip)
{
	struct snd_info_entry *entry;

	if (!snd_card_proc_new(chip->card, "riptide", &entry))
		snd_info_set_text_ops(entry, chip, snd_riptide_proc_read);
}

static int snd_riptide_mixer(struct snd_riptide *chip)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err = 0;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_riptide_codec_write,
		.read = snd_riptide_codec_read,
	};

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.scaps = AC97_SCAP_SKIP_MODEM;

	if ((err = snd_ac97_bus(chip->card, 0, &ops, chip, &pbus)) < 0)
		return err;

	chip->ac97_bus = pbus;
	ac97.pci = chip->pci;
	if ((err = snd_ac97_mixer(pbus, &ac97, &chip->ac97)) < 0)
		return err;
	return err;
}

#ifdef SUPPORT_JOYSTICK

static int
snd_riptide_joystick_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	static int dev;
	struct gameport *gameport;
	int ret;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	if (!enable[dev]) {
		ret = -ENOENT;
		goto inc_dev;
	}

	if (!joystick_port[dev]) {
		ret = 0;
		goto inc_dev;
	}

	gameport = gameport_allocate_port();
	if (!gameport) {
		ret = -ENOMEM;
		goto inc_dev;
	}
	if (!request_region(joystick_port[dev], 8, "Riptide gameport")) {
		snd_printk(KERN_WARNING
			   "Riptide: cannot grab gameport 0x%x\n",
			   joystick_port[dev]);
		gameport_free_port(gameport);
		ret = -EBUSY;
		goto inc_dev;
	}

	gameport->io = joystick_port[dev];
	gameport_register_port(gameport);
	pci_set_drvdata(pci, gameport);

	ret = 0;
inc_dev:
	dev++;
	return ret;
}

static void snd_riptide_joystick_remove(struct pci_dev *pci)
{
	struct gameport *gameport = pci_get_drvdata(pci);
	if (gameport) {
		release_region(gameport->io, 8);
		gameport_unregister_port(gameport);
	}
}
#endif

static int
snd_card_riptide_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_riptide *chip;
	unsigned short val;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		return err;
	err = snd_riptide_create(card, pci, &chip);
	if (err < 0)
		goto error;
	card->private_data = chip;
	err = snd_riptide_pcm(chip, 0);
	if (err < 0)
		goto error;
	err = snd_riptide_mixer(chip);
	if (err < 0)
		goto error;

	val = LEGACY_ENABLE_ALL;
	if (opl3_port[dev])
		val |= LEGACY_ENABLE_FM;
#ifdef SUPPORT_JOYSTICK
	if (joystick_port[dev])
		val |= LEGACY_ENABLE_GAMEPORT;
#endif
	if (mpu_port[dev])
		val |= LEGACY_ENABLE_MPU_INT | LEGACY_ENABLE_MPU;
	val |= (chip->irq << 4) & 0xf0;
	pci_write_config_word(chip->pci, PCI_EXT_Legacy_Mask, val);
	if (mpu_port[dev]) {
		val = mpu_port[dev];
		pci_write_config_word(chip->pci, PCI_EXT_MPU_Base, val);
		err = snd_mpu401_uart_new(card, 0, MPU401_HW_RIPTIDE,
					  val, MPU401_INFO_IRQ_HOOK, -1,
					  &chip->rmidi);
		if (err < 0)
			snd_printk(KERN_WARNING
				   "Riptide: Can't Allocate MPU at 0x%x\n",
				   val);
		else
			chip->mpuaddr = val;
	}
	if (opl3_port[dev]) {
		val = opl3_port[dev];
		pci_write_config_word(chip->pci, PCI_EXT_FM_Base, val);
		err = snd_opl3_create(card, val, val + 2,
				      OPL3_HW_RIPTIDE, 0, &chip->opl3);
		if (err < 0)
			snd_printk(KERN_WARNING
				   "Riptide: Can't Allocate OPL3 at 0x%x\n",
				   val);
		else {
			chip->opladdr = val;
			err = snd_opl3_hwdep_new(chip->opl3, 0, 1, NULL);
			if (err < 0)
				snd_printk(KERN_WARNING
					   "Riptide: Can't Allocate OPL3-HWDEP\n");
		}
	}
#ifdef SUPPORT_JOYSTICK
	if (joystick_port[dev]) {
		val = joystick_port[dev];
		pci_write_config_word(chip->pci, PCI_EXT_Game_Base, val);
		chip->gameaddr = val;
	}
#endif

	strcpy(card->driver, "RIPTIDE");
	strcpy(card->shortname, "Riptide");
#ifdef SUPPORT_JOYSTICK
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx, irq %i mpu 0x%x opl3 0x%x gameport 0x%x",
		 card->shortname, chip->port, chip->irq, chip->mpuaddr,
		 chip->opladdr, chip->gameaddr);
#else
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx, irq %i mpu 0x%x opl3 0x%x",
		 card->shortname, chip->port, chip->irq, chip->mpuaddr,
		 chip->opladdr);
#endif
	snd_riptide_proc_init(chip);
	err = snd_card_register(card);
	if (err < 0)
		goto error;
	pci_set_drvdata(pci, card);
	dev++;
	return 0;

 error:
	snd_card_free(card);
	return err;
}

static void snd_card_riptide_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_riptide_ids,
	.probe = snd_card_riptide_probe,
	.remove = snd_card_riptide_remove,
	.driver = {
		.pm = RIPTIDE_PM_OPS,
	},
};

#ifdef SUPPORT_JOYSTICK
static struct pci_driver joystick_driver = {
	.name = KBUILD_MODNAME "-joystick",
	.id_table = snd_riptide_joystick_ids,
	.probe = snd_riptide_joystick_probe,
	.remove = snd_riptide_joystick_remove,
};
#endif

static int __init alsa_card_riptide_init(void)
{
	int err;
	err = pci_register_driver(&driver);
	if (err < 0)
		return err;
#if defined(SUPPORT_JOYSTICK)
	err = pci_register_driver(&joystick_driver);
	/* On failure unregister formerly registered audio driver */
	if (err < 0)
		pci_unregister_driver(&driver);
#endif
	return err;
}

static void __exit alsa_card_riptide_exit(void)
{
	pci_unregister_driver(&driver);
#if defined(SUPPORT_JOYSTICK)
	pci_unregister_driver(&joystick_driver);
#endif
}

module_init(alsa_card_riptide_init);
module_exit(alsa_card_riptide_exit);
