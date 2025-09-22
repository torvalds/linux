/*	$OpenBSD: i2svar.h,v 1.10 2022/10/26 20:19:07 kn Exp $	*/

/*-
 * Copyright (c) 2001,2003 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if !defined(I2S_H_INCLUDE)
#define I2S_H_INCLUDE

#define I2S_DMALIST_MAX		32
#define I2S_DMASEG_MAX		NBPG

struct i2s_dma {
	bus_dmamap_t map;
	caddr_t addr;
	bus_dma_segment_t segs[I2S_DMALIST_MAX];
	int nsegs;
	size_t size;
	struct i2s_dma *next;
};

struct i2s_softc {
	struct device sc_dev;
	int sc_flags;
	int sc_node;
	u_int sc_baseaddr;

	void (*sc_ointr)(void *);	/* dma completion intr handler */
	void *sc_oarg;			/* arg for sc_ointr() */
	int sc_opages;			/* # of output pages */

	void (*sc_iintr)(void *);	/* dma completion intr handler */
	void *sc_iarg;			/* arg for sc_iintr() */

	u_int sc_record_source;		/* recording source mask */
	u_int sc_output_mask;		/* output source mask */

	void (*sc_setvolume)(struct i2s_softc *, int, int);
	void (*sc_setbass)(struct i2s_softc *, int);
	void (*sc_settreble)(struct i2s_softc *, int);
	void (*sc_setinput)(struct i2s_softc *, int);

	u_char *sc_reg;
	void *sc_i2c;

	u_int sc_rate;
	u_int sc_vol_l;
	u_int sc_vol_r;
	u_int sc_bass;
	u_int sc_treble;
	u_int sc_mute;

	u_int sc_spkr;			/* amp mute gpio offset */
	u_int sc_hp;			/* headphone mute gpio offset */
	u_int sc_hp_detect;		/* headphone detect gpio */
	u_int sc_hp_active;
	u_int sc_line;			/* line out mute gpio offset */
	u_int sc_line_detect;		/* line detect gpio */
	u_int sc_line_active;
	u_int sc_hw_reset;		/* hw reset gpio */


	bus_dma_tag_t sc_dmat;
	dbdma_regmap_t *sc_odma;
	dbdma_regmap_t *sc_idma;
	struct dbdma_command *sc_odmacmd, *sc_odmap;
	struct dbdma_command *sc_idmacmd, *sc_idmap;
	dbdma_t sc_odbdma, sc_idbdma;

	struct i2s_dma *sc_dmas;
};

void i2s_attach(struct device *, struct i2s_softc *, struct confargs *);
int i2s_intr(void *);
int i2s_open(void *, int);
void i2s_close(void *);
int i2s_set_params(void *, int, int, struct audio_params *, struct audio_params *);
int i2s_round_blocksize(void *, int);
int i2s_halt_output(void *);
int i2s_halt_input(void *);
int i2s_set_port(void *, mixer_ctrl_t *);
int i2s_get_port(void *, mixer_ctrl_t *);
int i2s_query_devinfo(void *, mixer_devinfo_t *);
size_t i2s_round_buffersize(void *, int, size_t);
int i2s_trigger_output(void *, void *, void *, int, void (*)(void *),
    void *, struct audio_params *);
int i2s_trigger_input(void *, void *, void *, int, void (*)(void *),
    void *, struct audio_params *);
int i2s_set_rate(struct i2s_softc *, int);
void i2s_gpio_init(struct i2s_softc *, int, struct device *);
void *i2s_allocm(void *, int, size_t, int, int);
int deq_reset(struct i2s_softc *);

#endif
