/*	$OpenBSD: nec86hwvar.h,v 1.5 2022/10/28 15:09:45 kn Exp $	*/
/*	$NecBSD: nec86hwvar.h,v 1.10 1998/03/14 07:04:55 kmatsuda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 * nec86hwvar.h
 *
 * NEC PC-9801-86 SoundBoard PCM driver for NetBSD/pc98.
 * Written by NAGAO Tadaaki, Feb 10, 1996.
 */

#ifndef	_NEC86HWVAR_H_
#define	_NEC86HWVAR_H_

/* types of function which writes to/reads from FIFO buffer */
struct nec86hw_softc;	/* dummy declaration */
typedef int (*func_fifo_output_t) (struct nec86hw_softc *, int);
typedef void (*func_fifo_input_t) (struct nec86hw_softc *, int);


struct nec86hw_softc {
	struct	device sc_dev;		/* base device */

	bus_space_tag_t sc_iot;		/* bus space tag */
	bus_space_handle_t sc_ioh;	/* nec86 core chip space handle */

	u_int	sc_cfgflags;		/* config flags */
#define NEC86HW_RATE_TYPE(flags)	((flags) & 1)
#define NEC86HW_NRATE_TYPE		2

	u_short	sc_open;		/* reference count of open calls */

	u_long	sc_irate;		/* sample rate for input */
	u_long	sc_orate;		/* sample rate for output */

	u_long	hw_irate;		/* hardware rate for input */
	u_char	hw_irate_bits;
	u_long	hw_orate;		/* hardware rate for output */
	u_char	hw_orate_bits;

	u_int	encoding;		/* ulaw / linear */
	u_int	precision;		/* 8/16 bits */
	int	channels;		/* monoral(1) / stereo(2) */

	int	in_port;		/* Just keep track of it */
#define NEC86HW_INPUT_MIXER	0
	int	out_port;		/* Just keep track of it */
#define NEC86HW_OUTPUT_MIXER	0

	int 	model;			/* board identification number */

	/* linear interpolation */
	u_long	conv_acc;
	u_short	conv_last0;
	u_short	conv_last0_l;
	u_short	conv_last0_r;
	u_short	conv_last1;
	u_short	conv_last1_l;
	u_short	conv_last1_r;

	void	(*sc_intr)(void *);	/* DMA completion intr handler */
	void	*sc_arg;		/* arg for sc_intr() */

	char	intr_busy;		/* whether the hardware running */

	/* pseudo-DMA */
	u_char	*pdma_ptr;		/* pointer to the data buffer */
	int	pdma_count;		/* size of the data in frames */
	int	pdma_nchunk;		/* number of chunks to do with */
	int	pdma_watermark;		/* interrupt trigger level */
	char	pdma_padded;		/* the buffer is padded out with
					   dummy zero's */
	char	pdma_mode;		/* input/output indicator */
#define PDMA_MODE_NONE		0
#define PDMA_MODE_OUTPUT	1
#define PDMA_MODE_INPUT		2

	/* function which writes to/reads from FIFO buffer */
	func_fifo_output_t	func_fifo_output;
	func_fifo_input_t	func_fifo_input;
};

struct nec86hw_functable_entry {
	int	precision;
	int	channels;

	func_fifo_output_t 	func_fifo_output_direct;
	func_fifo_input_t	func_fifo_input_direct;
	func_fifo_output_t	func_fifo_output_resamp;
	func_fifo_input_t	func_fifo_input_resamp;
};

/*
 * Interrupt watermarks for the FIFO ring buffer on the hardware.
 *
 * These values must satisfy:
 *	0 < WATERMARK_RATIO_OUT <= WATERMARK_MAX_RATIO
 *	0 < WATERMARK_RATIO_IN  <= WATERMARK_MAX_RATIO.
 * (For more details, see also nec86hw_pdma_output() and nec86hw_pdma_input()
 *  in nec86hw.c)
 */
#define WATERMARK_MAX_RATIO	100
#define WATERMARK_RATIO_OUT	50	/* 50% of the blocksize */
#define WATERMARK_RATIO_IN	50	/* 50% of NEC86_BUFFSIZE */

/*
 * Declarations of prototypes.
 */
#ifdef _KERNEL
void	nec86hw_attach(struct nec86hw_softc *);

int	nec86hw_open(void *, int);
void	nec86hw_close(void *);

int	nec86hw_set_params(void *, int, int, struct audio_params *,
	    struct audio_params *);

int	nec86hw_round_blocksize(void *, int);

int	nec86hw_set_out_port(void *, int);
int	nec86hw_get_out_port(void *);
int	nec86hw_set_in_port(void *, int);
int	nec86hw_get_in_port(void *);

int	nec86hw_commit_settings(void *);

int	nec86hw_mixer_set_port(void *, mixer_ctrl_t *);
int	nec86hw_mixer_get_port(void *, mixer_ctrl_t *);
int	nec86hw_mixer_query_devinfo(void *, mixer_devinfo_t *);

int	nec86hw_pdma_init_output(void *, void *, int);
int	nec86hw_pdma_init_input(void *, void *, int);
int	nec86hw_pdma_output(void *, void *, int, void (*)(void *), void *);
int	nec86hw_pdma_input(void *, void *, int, void (*)(void *), void *);

int	nec86hw_speaker_ctl(void *, int);

int	nec86hw_halt_pdma(void *);
int	nec86hw_cont_pdma(void *);

u_char	nec86hw_rate_bits(struct nec86hw_softc *, u_long);
int	nec86hw_round_watermark(int);

int	nec86hw_reset(struct nec86hw_softc *);
void	nec86hw_set_mode_playing(struct nec86hw_softc *);
void	nec86hw_set_mode_recording(struct nec86hw_softc *);

void	nec86hw_set_volume(struct nec86hw_softc *, int, u_char);

void	nec86hw_start_fifo(struct nec86hw_softc *);
void	nec86hw_stop_fifo(struct nec86hw_softc *);
void	nec86hw_enable_fifointr(struct nec86hw_softc *);
void	nec86hw_disable_fifointr(struct nec86hw_softc *);
int	nec86hw_seeif_intrflg(struct nec86hw_softc *);
void	nec86hw_clear_intrflg(struct nec86hw_softc *);
void	nec86hw_reset_fifo(struct nec86hw_softc *);
void	nec86hw_set_watermark(struct nec86hw_softc *, int);
void	nec86hw_set_precision_real(struct nec86hw_softc *, u_int);
void	nec86hw_set_rate_real(struct nec86hw_softc *, u_char);

void	nec86hw_output_chunk(struct nec86hw_softc *);
void	nec86hw_input_chunk(struct nec86hw_softc *);

int	nec86fifo_output_mono_8_direct(struct nec86hw_softc *, int);
int	nec86fifo_output_mono_16_direct(struct nec86hw_softc *, int);
int	nec86fifo_output_stereo_8_direct(struct nec86hw_softc *, int);
int	nec86fifo_output_stereo_16_direct(struct nec86hw_softc *, int);
int	nec86fifo_output_mono_8_resamp(struct nec86hw_softc *, int);
int	nec86fifo_output_mono_16_resamp(struct nec86hw_softc *, int);
int	nec86fifo_output_stereo_8_resamp(struct nec86hw_softc *, int);
int	nec86fifo_output_stereo_16_resamp(struct nec86hw_softc *, int);
void	nec86fifo_input_mono_8_direct(struct nec86hw_softc *, int);
void	nec86fifo_input_mono_16_direct(struct nec86hw_softc *, int);
void	nec86fifo_input_stereo_8_direct(struct nec86hw_softc *, int);
void	nec86fifo_input_stereo_16_direct(struct nec86hw_softc *, int);
void	nec86fifo_input_mono_8_resamp(struct nec86hw_softc *, int);
void	nec86fifo_input_mono_16_resamp(struct nec86hw_softc *, int);
void	nec86fifo_input_stereo_8_resamp(struct nec86hw_softc *, int);
void	nec86fifo_input_stereo_16_resamp(struct nec86hw_softc *, int);

void	nec86fifo_padding(struct nec86hw_softc *, int);

int	nec86hw_intr(void *);

#endif	/* _KERNEL */
#endif	/* !_NEC86HWVAR_H_ */
