/*	$OpenBSD: am7930var.h,v 1.6 2022/10/26 20:19:07 kn Exp $	*/
/*	$NetBSD: am7930var.h,v 1.10 2005/01/15 15:19:52 kent Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bsd_audiovar.h	8.1 (Berkeley) 6/11/93
 */

struct am7930_softc;

struct am7930_glue {
	uint8_t	(*codec_iread)(struct am7930_softc *sc, int);
	void	(*codec_iwrite)(struct am7930_softc *sc, int, uint8_t);
	uint16_t	(*codec_iread16)(struct am7930_softc *sc, int);
	void	(*codec_iwrite16)(struct am7930_softc *sc, int, uint16_t);
	void	(*onopen)(struct am7930_softc *sc);
	void	(*onclose)(struct am7930_softc *sc);
	int	precision;
};

struct am7930_softc {
	struct device sc_dev;	/* base device */
	int	sc_open;
	int	sc_locked;

	uint8_t	sc_rlevel;	/* record level */
	uint8_t	sc_plevel;	/* play level */
	uint8_t	sc_mlevel;	/* monitor level */
	uint8_t	sc_out_port;	/* output port */
	uint8_t	sc_mic_mute;

	struct am7930_glue *sc_glue;
};

extern int     am7930debug;

void	am7930_init(struct am7930_softc *, int);

#define AM7930_IWRITE(x,y,z)	(*(x)->sc_glue->codec_iwrite)((x),(y),(z))
#define AM7930_IREAD(x,y)	(*(x)->sc_glue->codec_iread)((x),(y))
#define AM7930_IWRITE16(x,y,z)	(*(x)->sc_glue->codec_iwrite16)((x),(y),(z))
#define AM7930_IREAD16(x,y)	(*(x)->sc_glue->codec_iread16)((x),(y))

#define AUDIOAMD_POLL_MODE	0
#define AUDIOAMD_DMA_MODE	1

/*
 * audio channel definitions.
 */

#define AUDIOAMD_SPEAKER_VOL	0	/* speaker volume */
#define AUDIOAMD_HEADPHONES_VOL	1	/* headphones volume */
#define AUDIOAMD_OUTPUT_CLASS	2

#define AUDIOAMD_MONITOR_VOL	3	/* monitor input volume */
#define AUDIOAMD_MONITOR_OUTPUT	4	/* output selector */
#define AUDIOAMD_MONITOR_CLASS	5

#define AUDIOAMD_MIC_VOL	6	/* microphone volume */
#define AUDIOAMD_MIC_MUTE	7
#define AUDIOAMD_INPUT_CLASS	8

#define AUDIOAMD_RECORD_SOURCE	9	/* source selector */
#define AUDIOAMD_RECORD_CLASS	10

/*
 * audio(9) MI callbacks from upper-level audio layer.
 */

struct audio_params;

int	am7930_open(void *, int);
void	am7930_close(void *);
int	am7930_set_params(void *, int, int, struct audio_params *,
	    struct audio_params *);
int	am7930_commit_settings(void *);
int	am7930_round_blocksize(void *, int);
int	am7930_halt_output(void *);
int	am7930_halt_input(void *);
int	am7930_set_port(void *, mixer_ctrl_t *);
int	am7930_get_port(void *, mixer_ctrl_t *);
int	am7930_query_devinfo(void *, mixer_devinfo_t *);
