/*	$OpenBSD: harmonyvar.h,v 1.9 2016/09/19 06:46:43 ratchov Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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

#define	HARMONY_PORT_INPUT_LVL		0
#define	HARMONY_PORT_INPUT_OV		1
#define	HARMONY_PORT_OUTPUT_LVL		2
#define	HARMONY_PORT_OUTPUT_GAIN	3
#define	HARMONY_PORT_MONITOR_LVL	4
#define	HARMONY_PORT_RECORD_SOURCE	5
#define	HARMONY_PORT_OUTPUT_SOURCE	6
#define	HARMONY_PORT_INPUT_CLASS	7
#define	HARMONY_PORT_OUTPUT_CLASS	8
#define	HARMONY_PORT_MONITOR_CLASS	9
#define	HARMONY_PORT_RECORD_CLASS	10

#define	HARMONY_IN_MIC			0
#define	HARMONY_IN_LINE			1

#define	HARMONY_OUT_LINE		0
#define	HARMONY_OUT_SPEAKER		1
#define	HARMONY_OUT_HEADPHONE		2

#define	PLAYBACK_EMPTYS			3	/* playback empty buffers */
#define	CAPTURE_EMPTYS			3	/* capture empty buffers */

struct harmony_volume {
	u_char left, right;
};

struct harmony_empty {
	u_int8_t	playback[PLAYBACK_EMPTYS][HARMONY_BUFSIZE];
	u_int8_t	capture[CAPTURE_EMPTYS][HARMONY_BUFSIZE];
};

struct harmony_dma {
	struct harmony_dma *d_next;
	bus_dmamap_t d_map;
	bus_dma_segment_t d_seg;
	caddr_t d_kva;
	size_t d_size;
};

struct harmony_channel {
	struct harmony_dma *c_current;
	bus_size_t c_segsz;
	bus_size_t c_cnt;
	bus_size_t c_blksz;
	bus_addr_t c_lastaddr;
	void (*c_intr)(void *);
	void *c_intrarg;
	bus_addr_t c_theaddr;
};

struct harmony_softc {
	struct device sc_dv;

	bus_dma_tag_t sc_dmat;
	bus_space_tag_t sc_bt;
	bus_space_handle_t sc_bh;
	int sc_open;
	u_int32_t sc_cntlbits;
	int sc_need_commit;
	int sc_playback_empty;
	bus_addr_t sc_playback_paddrs[PLAYBACK_EMPTYS];
	int sc_capture_empty;
	bus_addr_t sc_capture_paddrs[CAPTURE_EMPTYS];
	bus_dmamap_t sc_empty_map;
	bus_dma_segment_t sc_empty_seg;
	int sc_empty_rseg;
	struct harmony_empty *sc_empty_kva;
	struct harmony_dma *sc_dmas;
	int sc_playing, sc_capturing;
	struct harmony_channel sc_playback, sc_capture;
	struct harmony_volume sc_monitor_lvl, sc_input_lvl, sc_output_lvl;
	int sc_in_port, sc_out_port, sc_hasulinear8;
	int sc_micpreamp, sc_ov, sc_outputgain;
	int sc_teleshare;

	struct timeout sc_acc_tmo;
	u_int32_t sc_acc, sc_acc_num, sc_acc_cnt;
};

#define	READ_REG(sc, reg)		\
    bus_space_read_4((sc)->sc_bt, (sc)->sc_bh, (reg))
#define	WRITE_REG(sc, reg, val)		\
    bus_space_write_4((sc)->sc_bt, (sc)->sc_bh, (reg), (val))
#define	SYNC_REG(sc, reg, flags)	\
    bus_space_barrier((sc)->sc_bt, (sc)->sc_bh, (reg), sizeof(u_int32_t), \
	(flags))
