/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_EMU8000_REG_H
#define __SOUND_EMU8000_REG_H
/*
 *  Register operations for the EMU8000
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *
 *  Based on awe_wave.c by Takashi Iwai
 */

/*
 * Data port addresses relative to the EMU base.
 */
#define EMU8000_DATA0(e)    ((e)->port1)
#define EMU8000_DATA1(e)    ((e)->port2)
#define EMU8000_DATA2(e)    ((e)->port2+2)
#define EMU8000_DATA3(e)    ((e)->port3)
#define EMU8000_PTR(e)      ((e)->port3+2)

/*
 * Make a command from a register and channel.
 */
#define EMU8000_CMD(reg, chan) ((reg)<<5 | (chan))

/*
 * Commands to read and write the EMU8000 registers.
 * These macros should be used for all register accesses.
 */
#define EMU8000_CPF_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(0, (chan)))
#define EMU8000_PTRX_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(1, (chan)))
#define EMU8000_CVCF_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(2, (chan)))
#define EMU8000_VTFT_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(3, (chan)))
#define EMU8000_PSST_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(6, (chan)))
#define EMU8000_CSL_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(7, (chan)))
#define EMU8000_CCCA_READ(emu, chan) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(0, (chan)))
#define EMU8000_HWCF4_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 9))
#define EMU8000_HWCF5_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 10))
#define EMU8000_HWCF6_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 13))
#define EMU8000_SMALR_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 20))
#define EMU8000_SMARR_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 21))
#define EMU8000_SMALW_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 22))
#define EMU8000_SMARW_READ(emu) \
	snd_emu8000_peek_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 23))
#define EMU8000_SMLD_READ(emu) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 26))
#define EMU8000_SMRD_READ(emu) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(1, 26))
#define EMU8000_WC_READ(emu) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(1, 27))
#define EMU8000_HWCF1_READ(emu) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 29))
#define EMU8000_HWCF2_READ(emu) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 30))
#define EMU8000_HWCF3_READ(emu) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 31))
#define EMU8000_INIT1_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(2, (chan)))
#define EMU8000_INIT2_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(2, (chan)))
#define EMU8000_INIT3_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(3, (chan)))
#define EMU8000_INIT4_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(3, (chan)))
#define EMU8000_ENVVOL_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(4, (chan)))
#define EMU8000_DCYSUSV_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(5, (chan)))
#define EMU8000_ENVVAL_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(6, (chan)))
#define EMU8000_DCYSUS_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA1(emu), EMU8000_CMD(7, (chan)))
#define EMU8000_ATKHLDV_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(4, (chan)))
#define EMU8000_LFO1VAL_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(5, (chan)))
#define EMU8000_ATKHLD_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(6, (chan)))
#define EMU8000_LFO2VAL_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA2(emu), EMU8000_CMD(7, (chan)))
#define EMU8000_IP_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA3(emu), EMU8000_CMD(0, (chan)))
#define EMU8000_IFATN_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA3(emu), EMU8000_CMD(1, (chan)))
#define EMU8000_PEFE_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA3(emu), EMU8000_CMD(2, (chan)))
#define EMU8000_FMMOD_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA3(emu), EMU8000_CMD(3, (chan)))
#define EMU8000_TREMFRQ_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA3(emu), EMU8000_CMD(4, (chan)))
#define EMU8000_FM2FRQ2_READ(emu, chan) \
	snd_emu8000_peek((emu), EMU8000_DATA3(emu), EMU8000_CMD(5, (chan)))


#define EMU8000_CPF_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(0, (chan)), (val))
#define EMU8000_PTRX_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(1, (chan)), (val))
#define EMU8000_CVCF_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(2, (chan)), (val))
#define EMU8000_VTFT_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(3, (chan)), (val))
#define EMU8000_PSST_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(6, (chan)), (val))
#define EMU8000_CSL_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(7, (chan)), (val))
#define EMU8000_CCCA_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(0, (chan)), (val))
#define EMU8000_HWCF4_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 9), (val))
#define EMU8000_HWCF5_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 10), (val))
#define EMU8000_HWCF6_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 13), (val))
/* this register is not documented */
#define EMU8000_HWCF7_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 14), (val))
#define EMU8000_SMALR_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 20), (val))
#define EMU8000_SMARR_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 21), (val))
#define EMU8000_SMALW_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 22), (val))
#define EMU8000_SMARW_WRITE(emu, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 23), (val))
#define EMU8000_SMLD_WRITE(emu, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 26), (val))
#define EMU8000_SMRD_WRITE(emu, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(1, 26), (val))
#define EMU8000_WC_WRITE(emu, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(1, 27), (val))
#define EMU8000_HWCF1_WRITE(emu, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 29), (val))
#define EMU8000_HWCF2_WRITE(emu, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 30), (val))
#define EMU8000_HWCF3_WRITE(emu, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(1, 31), (val))
#define EMU8000_INIT1_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(2, (chan)), (val))
#define EMU8000_INIT2_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(2, (chan)), (val))
#define EMU8000_INIT3_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(3, (chan)), (val))
#define EMU8000_INIT4_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(3, (chan)), (val))
#define EMU8000_ENVVOL_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(4, (chan)), (val))
#define EMU8000_DCYSUSV_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(5, (chan)), (val))
#define EMU8000_ENVVAL_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(6, (chan)), (val))
#define EMU8000_DCYSUS_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA1(emu), EMU8000_CMD(7, (chan)), (val))
#define EMU8000_ATKHLDV_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(4, (chan)), (val))
#define EMU8000_LFO1VAL_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(5, (chan)), (val))
#define EMU8000_ATKHLD_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(6, (chan)), (val))
#define EMU8000_LFO2VAL_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA2(emu), EMU8000_CMD(7, (chan)), (val))
#define EMU8000_IP_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA3(emu), EMU8000_CMD(0, (chan)), (val))
#define EMU8000_IFATN_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA3(emu), EMU8000_CMD(1, (chan)), (val))
#define EMU8000_PEFE_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA3(emu), EMU8000_CMD(2, (chan)), (val))
#define EMU8000_FMMOD_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA3(emu), EMU8000_CMD(3, (chan)), (val))
#define EMU8000_TREMFRQ_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA3(emu), EMU8000_CMD(4, (chan)), (val))
#define EMU8000_FM2FRQ2_WRITE(emu, chan, val) \
	snd_emu8000_poke((emu), EMU8000_DATA3(emu), EMU8000_CMD(5, (chan)), (val))

#define EMU8000_0080_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(4, (chan)), (val))
#define EMU8000_00A0_WRITE(emu, chan, val) \
	snd_emu8000_poke_dw((emu), EMU8000_DATA0(emu), EMU8000_CMD(5, (chan)), (val))

#endif /* __SOUND_EMU8000_REG_H */
