/* $FreeBSD$ */

/*
 * This is prt of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman.
 *
 * bktr_mem : This kernel module allows us to keep our allocated
 *            contiguous memory for the video buffer, DMA programs and VBI data
 *            while the main bktr driver is unloaded and reloaded.
 *            This avoids the problem of trying to allocate contiguous each
 *            time the bktr driver is loaded.
 */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * 1. Redistributions of source code must retain the
 * Copyright (c) 2000 Roger Hardiman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


/* Support this number of devices */
#define BKTR_MEM_MAX_DEVICES	8

/* Define a name for each block of memory we need to keep hold of */
#define BKTR_MEM_DMA_PROG       1
#define BKTR_MEM_ODD_DMA_PROG   2
#define BKTR_MEM_VBIDATA        3
#define BKTR_MEM_VBIBUFFER      4
#define BKTR_MEM_BUF            5

/* Prototypes */
int         bktr_has_stored_addresses(int unit);
void        bktr_store_address(int unit, int type, vm_offset_t addr);
vm_offset_t bktr_retrieve_address(int unit, int type);

