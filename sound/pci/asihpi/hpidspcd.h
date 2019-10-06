/* SPDX-License-Identifier: GPL-2.0-only */
/***********************************************************************/
/**

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


\file
Functions for reading DSP code to load into DSP

*/
/***********************************************************************/
#ifndef _HPIDSPCD_H_
#define _HPIDSPCD_H_

#include "hpi_internal.h"

/** Header structure for dsp firmware file
 This structure must match that used in s2bin.c for generation of asidsp.bin
 */
/*#ifndef DISABLE_PRAGMA_PACK1 */
/*#pragma pack(push, 1) */
/*#endif */
struct code_header {
	/** Size in bytes including header */
	u32 size;
	/** File type tag "CODE" == 0x45444F43 */
	u32 type;
	/** Adapter model number */
	u32 adapter;
	/** Firmware version*/
	u32 version;
	/** Data checksum */
	u32 checksum;
};
/*#ifndef DISABLE_PRAGMA_PACK1 */
/*#pragma pack(pop) */
/*#endif */

/*? Don't need the pragmas? */
compile_time_assert((sizeof(struct code_header) == 20), code_header_size);

/** Descriptor for dspcode from firmware loader */
struct dsp_code {
	/** copy of  file header */
	struct code_header header;
	/** Expected number of words in the whole dsp code,INCL header */
	u32 block_length;
	/** Number of words read so far */
	u32 word_count;

	/** internal state of DSP code reader */
	struct dsp_code_private *pvt;
};

/** Prepare *psDspCode to refer to the requested adapter's firmware.
Code file name is obtained from HpiOs_GetDspCodePath

\return 0 for success, or error code if requested code is not available
*/
short hpi_dsp_code_open(
	/** Code identifier, usually adapter family */
	u32 adapter, void *pci_dev,
	/** Pointer to DSP code control structure */
	struct dsp_code *ps_dsp_code,
	/** Pointer to dword to receive OS specific error code */
	u32 *pos_error_code);

/** Close the DSP code file */
void hpi_dsp_code_close(struct dsp_code *ps_dsp_code);

/** Rewind to the beginning of the DSP code file (for verify) */
void hpi_dsp_code_rewind(struct dsp_code *ps_dsp_code);

/** Read one word from the dsp code file
	\return 0 for success, or error code if eof, or block length exceeded
*/
short hpi_dsp_code_read_word(struct dsp_code *ps_dsp_code,
				      /**< DSP code descriptor */
	u32 *pword /**< Where to store the read word */
	);

/** Get a block of dsp code into an internal buffer, and provide a pointer to
that buffer. (If dsp code is already an array in memory, it is referenced,
not copied.)

\return Error if requested number of words are not available
*/
short hpi_dsp_code_read_block(size_t words_requested,
	struct dsp_code *ps_dsp_code,
	/* Pointer to store (Pointer to code buffer) */
	u32 **ppblock);

#endif
