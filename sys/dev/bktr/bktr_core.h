/* $FreeBSD$ */

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_core : This deals with the Bt848/849/878/879 PCI Frame Grabber,
 *               Handles all the open, close, ioctl and read userland calls.
 *               Sets the Bt848 registers and generates RISC pograms.
 *               Controls the i2c bus and GPIO interface.
 *               Contains the interface to the kernel.
 *               (eg probe/attach and open/close/ioctl)
 *
 */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * 1. Redistributions of source code must retain the
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
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
 *      This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
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


int		i2cWrite( bktr_ptr_t bktr, int addr, int byte1, int byte2 );
int		i2cRead( bktr_ptr_t bktr, int addr );

void            msp_dpl_reset( bktr_ptr_t bktr, int i2d_addr );
unsigned int    msp_dpl_read( bktr_ptr_t bktr, int i2c_addr, unsigned char dev, unsigned int addr );
void            msp_dpl_write( bktr_ptr_t bktr, int i2c_addr, unsigned char dev,
			       unsigned int addr, unsigned int data );


/*
 * Defines for userland processes blocked in this driver
 *   For /dev/bktr[n] use memory address of bktr structure
 *   For /dev/vbi[n] use memory address of bktr structure + 1
 *                   this is ok as the bktr structure is > 1 byte
 */                 
#define BKTR_SLEEP  ((caddr_t)bktr    )
#define VBI_SLEEP   ((caddr_t)bktr + 1)


/* device name for printf */
const char *bktr_name(bktr_ptr_t bktr);

/* Prototypes for attatch and interrupt functions */
void	common_bktr_attach( bktr_ptr_t bktr, int unit,
			u_long pci_id, u_int rev ); 
int	common_bktr_intr( void *arg );


/* Prototypes for open, close, read, mmap and ioctl calls */
int	video_open( bktr_ptr_t bktr ); 
int	video_close( bktr_ptr_t bktr );
int	video_read( bktr_ptr_t bktr, int unit, struct cdev *dev, struct uio *uio );
int	video_ioctl( bktr_ptr_t bktr, int unit,
			ioctl_cmd_t cmd, caddr_t arg, struct thread* pr );


int	tuner_open( bktr_ptr_t bktr );
int	tuner_close( bktr_ptr_t bktr );
int	tuner_ioctl( bktr_ptr_t bktr, int unit,
			ioctl_cmd_t cmd, caddr_t arg, struct thread* pr );

int	vbi_open( bktr_ptr_t bktr );
int	vbi_close( bktr_ptr_t bktr );
int	vbi_read( bktr_ptr_t bktr, struct uio *uio, int ioflag );

