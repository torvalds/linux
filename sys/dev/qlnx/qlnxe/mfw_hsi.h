/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

/****************************************************************************
 *
 * Name:        mfw_hsi.h
 *
 * Description: Global definitions
 *
 ****************************************************************************/

#ifndef MFW_HSI_H
#define MFW_HSI_H

#define MFW_TRACE_SIGNATURE	0x25071946

/* The trace in the buffer */
#define MFW_TRACE_EVENTID_MASK		0x00ffff
#define MFW_TRACE_PRM_SIZE_MASK		0x0f0000
#define MFW_TRACE_PRM_SIZE_SHIFT	16
#define MFW_TRACE_ENTRY_SIZE		3

struct mcp_trace {
	u32	signature;	/* Help to identify that the trace is valid */
	u32	size;		/* the size of the trace buffer in bytes*/
	u32	curr_level;	/* 2 - all will be written to the buffer
				 * 1 - debug trace will not be written
				 * 0 - just errors will be written to the buffer
				 */
	u32	modules_mask[2];/* a bit per module, 1 means write it, 0 means mask it */

	/* Warning: the following pointers are assumed to be 32bits as they are used only in the MFW */
	u32	trace_prod;	/* The next trace will be written to this offset */
	u32	trace_oldest;	/* The oldest valid trace starts at this offset (usually very close after the current producer) */
};

#endif /* MFW_HSI_H */


