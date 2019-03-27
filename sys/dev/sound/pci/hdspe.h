/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2016 Ruslan Bukin <br@bsdpad.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define	PCI_VENDOR_XILINX		0x10ee
#define	PCI_DEVICE_XILINX_HDSPE		0x3fc6 /* AIO, MADI, AES, RayDAT */
#define	PCI_CLASS_REVISION		0x08
#define	PCI_REVISION_AIO		212
#define	PCI_REVISION_RAYDAT		211

#define	AIO				0
#define	RAYDAT				1

/* Hardware mixer */
#define	HDSPE_OUT_ENABLE_BASE		512
#define	HDSPE_IN_ENABLE_BASE		768
#define	HDSPE_MIXER_BASE		32768
#define	HDSPE_MAX_GAIN			32768

/* Buffer */
#define	HDSPE_PAGE_ADDR_BUF_OUT		8192
#define	HDSPE_PAGE_ADDR_BUF_IN		(HDSPE_PAGE_ADDR_BUF_OUT + 64 * 16 * 4)
#define	HDSPE_BUF_POSITION_MASK		0x000FFC0

/* Frequency */
#define	HDSPE_FREQ_0			(1 << 6)
#define	HDSPE_FREQ_1			(1 << 7)
#define	HDSPE_FREQ_DOUBLE		(1 << 8)
#define	HDSPE_FREQ_QUAD			(1 << 31)

#define	HDSPE_FREQ_32000		HDSPE_FREQ_0
#define	HDSPE_FREQ_44100		HDSPE_FREQ_1
#define	HDSPE_FREQ_48000		(HDSPE_FREQ_0 | HDSPE_FREQ_1)
#define	HDSPE_FREQ_MASK			(HDSPE_FREQ_0 | HDSPE_FREQ_1 |	\
					HDSPE_FREQ_DOUBLE | HDSPE_FREQ_QUAD)
#define	HDSPE_FREQ_MASK_DEFAULT		HDSPE_FREQ_48000
#define	HDSPE_FREQ_REG			256
#define	HDSPE_FREQ_AIO			104857600000000ULL

#define	HDSPE_SPEED_DEFAULT		48000

/* Latency */
#define	HDSPE_LAT_0			(1 << 1)
#define	HDSPE_LAT_1			(1 << 2)
#define	HDSPE_LAT_2			(1 << 3)
#define	HDSPE_LAT_MASK			(HDSPE_LAT_0 | HDSPE_LAT_1 | HDSPE_LAT_2)
#define	HDSPE_LAT_BYTES_MAX		(4096 * 4)
#define	HDSPE_LAT_BYTES_MIN		(32 * 4)
#define	hdspe_encode_latency(x)		(((x)<<1) & HDSPE_LAT_MASK)

/* Gain */
#define	HDSP_ADGain0			(1 << 25)
#define	HDSP_ADGain1			(1 << 26)
#define	HDSP_DAGain0			(1 << 27)
#define	HDSP_DAGain1			(1 << 28)
#define	HDSP_PhoneGain0			(1 << 29)
#define	HDSP_PhoneGain1			(1 << 30)

#define	HDSP_ADGainMask			(HDSP_ADGain0 | HDSP_ADGain1)
#define	HDSP_ADGainMinus10dBV		(HDSP_ADGainMask)
#define	HDSP_ADGainPlus4dBu		(HDSP_ADGain0)
#define	HDSP_ADGainLowGain		0

#define	HDSP_DAGainMask			(HDSP_DAGain0 | HDSP_DAGain1)
#define	HDSP_DAGainHighGain		(HDSP_DAGainMask)
#define	HDSP_DAGainPlus4dBu		(HDSP_DAGain0)
#define	HDSP_DAGainMinus10dBV		0

#define	HDSP_PhoneGainMask		(HDSP_PhoneGain0|HDSP_PhoneGain1)
#define	HDSP_PhoneGain0dB		HDSP_PhoneGainMask
#define	HDSP_PhoneGainMinus6dB		(HDSP_PhoneGain0)
#define	HDSP_PhoneGainMinus12dB		0

#define	HDSPM_statusRegister		0
#define	HDSPM_statusRegister2		192

/* Settings */
#define	HDSPE_SETTINGS_REG		0
#define	HDSPE_CONTROL_REG		64
#define	HDSPE_STATUS_REG		0
#define	HDSPE_ENABLE			(1 << 0)
#define	HDSPM_CLOCK_MODE_MASTER		(1 << 4)

/* Interrupts */
#define	HDSPE_AUDIO_IRQ_PENDING		(1 << 0)
#define	HDSPE_AUDIO_INT_ENABLE		(1 << 5)
#define	HDSPE_INTERRUPT_ACK		96

/* Channels */
#define	HDSPE_MAX_SLOTS			64 /* Mono channels */
#define	HDSPE_MAX_CHANS			(HDSPE_MAX_SLOTS / 2) /* Stereo pairs */

#define	HDSPE_CHANBUF_SAMPLES		(16 * 1024)
#define	HDSPE_CHANBUF_SIZE		(4 * HDSPE_CHANBUF_SAMPLES)
#define	HDSPE_DMASEGSIZE		(HDSPE_CHANBUF_SIZE * HDSPE_MAX_SLOTS)

struct hdspe_channel {
	uint32_t	left;
	uint32_t	right;
	char		*descr;
	uint32_t	play;
	uint32_t	rec;
};

static MALLOC_DEFINE(M_HDSPE, "hdspe", "hdspe audio");

/* Channel registers */
struct sc_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;
	struct sc_pcminfo	*parent;

	/* Channel information */
	uint32_t	dir;
	uint32_t	format;
	uint32_t	lslot;
	uint32_t	rslot;
	uint32_t	lvol;
	uint32_t	rvol;

	/* Buffer */
	uint32_t	*data;
	uint32_t	size;

	/* Flags */
	uint32_t	run;
};

/* PCM device private data */
struct sc_pcminfo {
	device_t		dev;
	uint32_t		(*ih) (struct sc_pcminfo *scp);
	uint32_t		chnum;
	struct sc_chinfo	chan[HDSPE_MAX_CHANS];
	struct sc_info		*sc;
	struct hdspe_channel	*hc;
};

/* HDSPe device private data */
struct sc_info {
	device_t		dev;
	struct mtx		*lock;

	uint32_t		ctrl_register;
	uint32_t		settings_register;
	uint32_t		type;

	/* Control/Status register */
	struct resource		*cs;
	int			csid;
	bus_space_tag_t		cst;
	bus_space_handle_t	csh;

	struct resource		*irq;
	int			irqid;
	void			*ih;
	bus_dma_tag_t		dmat;

	/* Play/Record DMA buffers */
	uint32_t		*pbuf;
	uint32_t		*rbuf;
	uint32_t		bufsize;
	bus_dmamap_t		pmap;
	bus_dmamap_t		rmap;
	uint32_t		period;
	uint32_t		speed;
};

#define	hdspe_read_1(sc, regno)						\
	bus_space_read_1((sc)->cst, (sc)->csh, (regno))
#define	hdspe_read_2(sc, regno)						\
	bus_space_read_2((sc)->cst, (sc)->csh, (regno))
#define	hdspe_read_4(sc, regno)						\
	bus_space_read_4((sc)->cst, (sc)->csh, (regno))

#define	hdspe_write_1(sc, regno, data)					\
	bus_space_write_1((sc)->cst, (sc)->csh, (regno), (data))
#define	hdspe_write_2(sc, regno, data)					\
	bus_space_write_2((sc)->cst, (sc)->csh, (regno), (data))
#define	hdspe_write_4(sc, regno, data)					\
	bus_space_write_4((sc)->cst, (sc)->csh, (regno), (data))
