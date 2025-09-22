/*	$OpenBSD: bktr_audio.h,v 1.2 2004/03/19 09:14:15 mickey Exp $	*/
/* $FreeBSD: src/sys/dev/bktr/bktr_audio.h,v 1.2 1999/10/28 13:58:14 roger Exp $ */

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_audio : This deals with controlling the audio on TV cards,
 *                controlling the Audio Multiplexer (audio source selector).
 *                controlling any MSP34xx stereo audio decoders.
 *                controlling any DPL35xx dolby surround sound audio decoders.
 *                initialising TDA98xx audio devices.
 *
 */

/*
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

/*
 * Select Audio source, and allow muting
 */
int	set_audio( bktr_ptr_t bktr, int mode );
void	temp_mute( bktr_ptr_t bktr, int flag );


/*
 * Initialise any MSP or TDA devices
 */
void	init_audio_devices( bktr_ptr_t bktr );


/*
 * MSP34xx Audio Chip functions.
 */
void	msp_autodetect( bktr_ptr_t bktr );
void	msp_read_id( bktr_ptr_t bktr );


/*
 * DPL35xx Audio Chip functions.
 */
void	dpl_autodetect( bktr_ptr_t bktr );
void	dpl_read_id( bktr_ptr_t bktr );


/*
 * TDA98xx Audio Chip functions.
 */
void	init_BTSC( bktr_ptr_t bktr ); 
int	set_BTSC( bktr_ptr_t bktr, int control );



