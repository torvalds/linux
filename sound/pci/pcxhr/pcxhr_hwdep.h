/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Digigram pcxhr compatible soundcards
 *
 * definitions and makros for basic card access
 *
 * Copyright (c) 2004 by Digigram <alsa@digigram.com>
 */

#ifndef __SOUND_PCXHR_HWDEP_H
#define __SOUND_PCXHR_HWDEP_H


/* firmware status codes  */
#define PCXHR_FIRMWARE_XLX_INT_INDEX   0
#define PCXHR_FIRMWARE_XLX_COM_INDEX   1
#define PCXHR_FIRMWARE_DSP_EPRM_INDEX  2
#define PCXHR_FIRMWARE_DSP_BOOT_INDEX  3
#define PCXHR_FIRMWARE_DSP_MAIN_INDEX  4
#define PCXHR_FIRMWARE_FILES_MAX_INDEX 5


/* exported */
int  pcxhr_setup_firmware(struct pcxhr_mgr *mgr);
void pcxhr_reset_board(struct pcxhr_mgr *mgr);

#endif /* __SOUND_PCXHR_HWDEP_H */
