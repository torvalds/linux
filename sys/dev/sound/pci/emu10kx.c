/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * Copyright (c) 2003-2007 Yuriy Tsibizov <yuriy.tsibizov@gfk.ru>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/kdb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/clock.h>	/* for DELAY */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/chip.h>
#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>

#include <dev/sound/pci/emuxkireg.h>
#include <dev/sound/pci/emu10kx.h>

/* hw flags */
#define	HAS_51		0x0001
#define	HAS_71		0x0002
#define	HAS_AC97	0x0004

#define	IS_EMU10K1	0x0008
#define	IS_EMU10K2	0x0010
#define	IS_CA0102	0x0020
#define	IS_CA0108	0x0040
#define	IS_UNKNOWN	0x0080

#define	BROKEN_DIGITAL	0x0100
#define	DIGITAL_ONLY	0x0200

#define	IS_CARDBUS	0x0400

#define	MODE_ANALOG	1
#define	MODE_DIGITAL	2
#define	SPDIF_MODE_PCM	1
#define	SPDIF_MODE_AC3	2

#define	MACS	0x0
#define	MACS1	0x1
#define	MACW	0x2
#define	MACW1	0x3
#define	MACINTS	0x4
#define	MACINTW	0x5
#define	ACC3	0x6
#define	MACMV	0x7
#define	ANDXOR	0x8
#define	TSTNEG	0x9
#define	LIMIT	0xA
#define	LIMIT1	0xB
#define	LOG	0xC
#define	EXP	0xD
#define	INTERP	0xE
#define	SKIP	0xF

#define	GPR(i)	(sc->gpr_base+(i))
#define	INP(i)	(sc->input_base+(i))
#define	OUTP(i)	(sc->output_base+(i))
#define	FX(i)	(i)
#define	FX2(i)	(sc->efxc_base+(i))
#define	DSP_CONST(i) (sc->dsp_zero+(i))

#define	COND_NORMALIZED	DSP_CONST(0x1)
#define	COND_BORROW	DSP_CONST(0x2)
#define	COND_MINUS	DSP_CONST(0x3)
#define	COND_LESS_ZERO	DSP_CONST(0x4)
#define	COND_EQ_ZERO	DSP_CONST(0x5)
#define	COND_SATURATION	DSP_CONST(0x6)
#define	COND_NEQ_ZERO	DSP_CONST(0x8)

#define	DSP_ACCUM	DSP_CONST(0x16)
#define	DSP_CCR		DSP_CONST(0x17)

/* Live! Inputs */
#define	IN_AC97_L 	0x00
#define	IN_AC97_R 	0x01
#define	IN_AC97		IN_AC97_L
#define	IN_SPDIF_CD_L	0x02
#define	IN_SPDIF_CD_R	0x03
#define	IN_SPDIF_CD	IN_SPDIF_CD_L
#define	IN_ZOOM_L 	0x04
#define	IN_ZOOM_R 	0x05
#define	IN_ZOOM		IN_ZOOM_L
#define	IN_TOSLINK_L	0x06
#define	IN_TOSLINK_R	0x07
#define	IN_TOSLINK	IN_TOSLINK_L
#define	IN_LINE1_L	0x08
#define	IN_LINE1_R	0x09
#define	IN_LINE1	IN_LINE1_L
#define	IN_COAX_SPDIF_L	0x0a
#define	IN_COAX_SPDIF_R	0x0b
#define	IN_COAX_SPDIF	IN_COAX_SPDIF_L
#define	IN_LINE2_L	0x0c
#define	IN_LINE2_R	0x0d
#define	IN_LINE2	IN_LINE2_L
#define	IN_0E		0x0e
#define	IN_0F		0x0f

/* Outputs */
#define	OUT_AC97_L	0x00
#define	OUT_AC97_R	0x01
#define	OUT_AC97	OUT_AC97_L
#define	OUT_A_FRONT	OUT_AC97
#define	OUT_TOSLINK_L 	0x02
#define	OUT_TOSLINK_R 	0x03
#define	OUT_TOSLINK	OUT_TOSLINK_L
#define	OUT_D_CENTER	0x04
#define	OUT_D_SUB	0x05
#define	OUT_HEADPHONE_L	0x06
#define	OUT_HEADPHONE_R	0x07
#define	OUT_HEADPHONE	OUT_HEADPHONE_L
#define	OUT_REAR_L	0x08
#define	OUT_REAR_R	0x09
#define	OUT_REAR	OUT_REAR_L
#define	OUT_ADC_REC_L 	0x0a
#define	OUT_ADC_REC_R	0x0b
#define	OUT_ADC_REC	OUT_ADC_REC_L
#define	OUT_MIC_CAP	0x0c

/* Live! 5.1 Digital, non-standard 5.1 (center & sub) outputs */
#define	OUT_A_CENTER	0x11
#define	OUT_A_SUB	0x12

/* Audigy Inputs */
#define	A_IN_AC97_L	0x00
#define	A_IN_AC97_R	0x01
#define	A_IN_AC97	A_IN_AC97_L
#define	A_IN_SPDIF_CD_L	0x02
#define	A_IN_SPDIF_CD_R	0x03
#define	A_IN_SPDIF_CD	A_IN_SPDIF_CD_L
#define	A_IN_O_SPDIF_L	0x04
#define	A_IN_O_SPDIF_R	0x05
#define	A_IN_O_SPDIF	A_IN_O_SPDIF_L
#define	A_IN_LINE2_L	0x08
#define	A_IN_LINE2_R	0x09
#define	A_IN_LINE2	A_IN_LINE2_L
#define	A_IN_R_SPDIF_L	0x0a
#define	A_IN_R_SPDIF_R	0x0b
#define	A_IN_R_SPDIF	A_IN_R_SPDIF_L
#define	A_IN_AUX2_L	0x0c
#define	A_IN_AUX2_R	0x0d
#define	A_IN_AUX2	A_IN_AUX2_L

/* Audigy Outputs */
#define	A_OUT_D_FRONT_L	0x00
#define	A_OUT_D_FRONT_R	0x01
#define	A_OUT_D_FRONT	A_OUT_D_FRONT_L
#define	A_OUT_D_CENTER	0x02
#define	A_OUT_D_SUB	0x03
#define	A_OUT_D_SIDE_L	0x04
#define	A_OUT_D_SIDE_R	0x05
#define	A_OUT_D_SIDE	A_OUT_D_SIDE_L
#define	A_OUT_D_REAR_L	0x06
#define	A_OUT_D_REAR_R	0x07
#define	A_OUT_D_REAR	A_OUT_D_REAR_L

/* on Audigy Platinum only */
#define	A_OUT_HPHONE_L	0x04
#define	A_OUT_HPHONE_R	0x05
#define	A_OUT_HPHONE	A_OUT_HPHONE_L

#define	A_OUT_A_FRONT_L	0x08
#define	A_OUT_A_FRONT_R	0x09
#define	A_OUT_A_FRONT	A_OUT_A_FRONT_L
#define	A_OUT_A_CENTER	0x0a
#define	A_OUT_A_SUB	0x0b
#define	A_OUT_A_SIDE_L	0x0c
#define	A_OUT_A_SIDE_R	0x0d
#define	A_OUT_A_SIDE	A_OUT_A_SIDE_L
#define	A_OUT_A_REAR_L	0x0e
#define	A_OUT_A_REAR_R	0x0f
#define	A_OUT_A_REAR	A_OUT_A_REAR_L
#define	A_OUT_AC97_L	0x10
#define	A_OUT_AC97_R	0x11
#define	A_OUT_AC97	A_OUT_AC97_L
#define	A_OUT_ADC_REC_L	0x16
#define	A_OUT_ADC_REC_R	0x17
#define	A_OUT_ADC_REC	A_OUT_ADC_REC_L

#define EMU_DATA2	0x24
#define EMU_IPR2	0x28
#define EMU_INTE2	0x2c
#define EMU_IPR3	0x38
#define EMU_INTE3	0x3c

#define EMU_A2_SRCSel		0x60
#define EMU_A2_SRCMULTI_ENABLE	0x6e

#define EMU_A_I2S_CAPTURE_96000	0x00000400

#define EMU_A2_MIXER_I2S_ENABLE           0x7B
#define EMU_A2_MIXER_SPDIF_ENABLE         0x7A

#define	C_FRONT_L	0
#define	C_FRONT_R	1
#define	C_REC_L		2
#define	C_REC_R		3
#define	C_REAR_L	4
#define	C_REAR_R	5
#define	C_CENTER	6
#define	C_SUB		7
#define	C_SIDE_L	8
#define	C_SIDE_R	9
#define	NUM_CACHES	10

#define	CDSPDIFMUTE	0
#define	ANALOGMUTE	1
#define	NUM_MUTE	2

#define	EMU_MAX_GPR	512
#define	EMU_MAX_IRQ_CONSUMERS 32

struct emu_voice {
	int	vnum;
	unsigned int	b16:1, stereo:1, busy:1, running:1, ismaster:1;
	int	speed;
	int	start;
	int	end;
	int	vol;
	uint32_t buf;
	void	*vbuf;
	struct emu_voice *slave;
	uint32_t sa;
	uint32_t ea;
	uint32_t routing[8];
	uint32_t amounts[8];
};

struct emu_memblk {
	SLIST_ENTRY(emu_memblk)	link;
	void		*buf;
	char		owner[16];
	bus_addr_t	buf_addr;
	uint32_t	pte_start, pte_size;
	bus_dmamap_t	buf_map;
};

struct emu_mem {
	uint8_t		bmap[EMU_MAXPAGES / 8];
	uint32_t	*ptb_pages;
	void		*silent_page;
	bus_addr_t	ptb_pages_addr;
	bus_addr_t	silent_page_addr;
	bus_dmamap_t	ptb_map;
	bus_dmamap_t	silent_map;
	bus_dma_tag_t	dmat;
	struct emu_sc_info *card;
	SLIST_HEAD(, emu_memblk) blocks;
};

/* rm */
struct emu_rm {
	struct emu_sc_info *card;
	struct mtx	gpr_lock;
	signed int	allocmap[EMU_MAX_GPR];
	int		num_gprs;
	int		last_free_gpr;
	int 		num_used;
};

struct emu_intr_handler {
	void*		softc;
	uint32_t	intr_mask;
	uint32_t	inte_mask;
	uint32_t(*irq_func) (void *softc, uint32_t irq);
};

struct emu_sc_info {
	struct mtx	lock;
	struct mtx	rw;		/* Hardware exclusive access lock */

	/* Hardware and subdevices */
	device_t	dev;
	device_t	pcm[RT_COUNT];
	device_t	midi[2];
	uint32_t	type;
	uint32_t	rev;

	bus_space_tag_t	st;
	bus_space_handle_t sh;

	struct cdev	*cdev;		/* /dev/emu10k character device */
	struct mtx	emu10kx_lock;
	int		emu10kx_isopen;
	struct sbuf	emu10kx_sbuf;
	int		emu10kx_bufptr;


	/* Resources */
	struct resource	*reg;
	struct resource	*irq;
	void 		*ih;

	/* IRQ handlers */
	struct emu_intr_handler ihandler[EMU_MAX_IRQ_CONSUMERS];

	/* Card HW configuration */
	unsigned int	mode;	/* analog / digital */
	unsigned int	mchannel_fx;
	unsigned int	dsp_zero;
	unsigned int	code_base;
	unsigned int	code_size;
	unsigned int	gpr_base;
	unsigned int	num_gprs;
	unsigned int	input_base;
	unsigned int	output_base;
	unsigned int	efxc_base;
	unsigned int	opcode_shift;
	unsigned int	high_operand_shift;
	unsigned int	address_mask;
	uint32_t 	is_emu10k1:1, is_emu10k2, is_ca0102, is_ca0108:1,
			has_ac97:1, has_51:1, has_71:1,
			enable_ir:1,
			broken_digital:1, is_cardbus:1;

	signed int	mch_disabled, mch_rec, dbg_level;
	signed int 	num_inputs;
	unsigned int 	num_outputs;
	unsigned int 	num_fxbuses;
	unsigned int 	routing_code_start;
	unsigned int	routing_code_end;

	/* HW resources */
	struct emu_voice voice[NUM_G];			/* Hardware voices */
	uint32_t	irq_mask[EMU_MAX_IRQ_CONSUMERS]; /* IRQ manager data */
	int 		timer[EMU_MAX_IRQ_CONSUMERS];	/* timer */
	int		timerinterval;
	struct		emu_rm *rm;
	struct		emu_mem mem;			/* memory */

	/* Mixer */
	int		mixer_gpr[NUM_MIXERS];
	int		mixer_volcache[NUM_MIXERS];
	int		cache_gpr[NUM_CACHES];
	int		dummy_gpr;
	int		mute_gpr[NUM_MUTE];
	struct sysctl_ctx_list	*ctx;
	struct sysctl_oid	*root;
};

static void	emu_setmap(void *arg, bus_dma_segment_t * segs, int nseg, int error);
static void*	emu_malloc(struct emu_mem *mem, uint32_t sz, bus_addr_t * addr, bus_dmamap_t *map);
static void	emu_free(struct emu_mem *mem, void *dmabuf, bus_dmamap_t map);
static void*	emu_memalloc(struct emu_mem *mem, uint32_t sz, bus_addr_t * addr, const char * owner);
static int	emu_memfree(struct emu_mem *mem, void *membuf);
static int	emu_memstart(struct emu_mem *mem, void *membuf);

/* /dev */
static int	emu10kx_dev_init(struct emu_sc_info *sc);
static int	emu10kx_dev_uninit(struct emu_sc_info *sc);
static int	emu10kx_prepare(struct emu_sc_info *sc, struct sbuf *s);

static void	emumix_set_mode(struct emu_sc_info *sc, int mode);
static void	emumix_set_spdif_mode(struct emu_sc_info *sc, int mode);
static void	emumix_set_fxvol(struct emu_sc_info *sc, unsigned gpr, int32_t vol);
static void	emumix_set_gpr(struct emu_sc_info *sc, unsigned gpr, int32_t val);
static int	sysctl_emu_mixer_control(SYSCTL_HANDLER_ARGS);

static int	emu_rm_init(struct emu_sc_info *sc);
static int	emu_rm_uninit(struct emu_sc_info *sc);
static int	emu_rm_gpr_alloc(struct emu_rm *rm, int count);

static unsigned int emu_getcard(device_t dev);
static uint32_t	emu_rd_nolock(struct emu_sc_info *sc, unsigned int regno, unsigned int size);
static void	emu_wr_nolock(struct emu_sc_info *sc, unsigned int regno, uint32_t data, unsigned int size);
static void	emu_wr_cbptr(struct emu_sc_info *sc, uint32_t data);

static void	emu_vstop(struct emu_sc_info *sc, char channel, int enable);

static void	emu_intr(void *p);
static void	emu_wrefx(struct emu_sc_info *sc, unsigned int pc, unsigned int data);
static void	emu_addefxop(struct emu_sc_info *sc, unsigned int op, unsigned int z, unsigned int w, unsigned int x, unsigned int y, uint32_t * pc);
static void	emu_initefx(struct emu_sc_info *sc);

static int	emu_cardbus_init(struct emu_sc_info *sc);
static int	emu_init(struct emu_sc_info *sc);
static int	emu_uninit(struct emu_sc_info *sc);

static int	emu_read_ivar(device_t bus __unused, device_t dev, int ivar_index, uintptr_t * result);
static int	emu_write_ivar(device_t bus __unused, device_t dev __unused,
    int ivar_index, uintptr_t value __unused);

static int	emu_pci_probe(device_t dev);
static int	emu_pci_attach(device_t dev);
static int	emu_pci_detach(device_t dev);
static int	emu_modevent(module_t mod __unused, int cmd, void *data __unused);

#ifdef	SND_EMU10KX_DEBUG

#define EMU_MTX_DEBUG() do { 						\
		if (mtx_owned(&sc->rw)) {				\
		printf("RW owned in %s line %d for %s\n", __func__,	\
			__LINE__ , device_get_nameunit(sc->dev));	\
		printf("rw lock owned: %d\n", mtx_owned(&sc->rw));	\
		printf("rw lock: value %x thread %x\n",			\
			((&sc->rw)->mtx_lock & ~MTX_FLAGMASK), 		\
			(uintptr_t)curthread);				\
		printf("rw lock: recursed %d\n", mtx_recursed(&sc->rw));\
		db_show_mtx(&sc->rw);					\
		}							\
	} while (0)
#else
#define EMU_MTX_DEBUG() do { 						\
	} while (0)
#endif

#define EMU_RWLOCK() do {		\
	EMU_MTX_DEBUG();		\
	mtx_lock(&(sc->rw));		\
	} while (0)

#define EMU_RWUNLOCK() do {		\
	mtx_unlock(&(sc->rw));		\
	EMU_MTX_DEBUG();		\
	} while (0)

/* Supported cards */
struct emu_hwinfo {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subvendor;
	uint16_t	subdevice;
	char		SBcode[8];
	char		desc[32];
	int		flags;
};

static struct emu_hwinfo emu_cards[] = {
	{0xffff, 0xffff, 0xffff, 0xffff, "BADCRD", "Not a compatible card", 0},
	/* 0x0020..0x002f 4.0 EMU10K1 cards */
	{0x1102, 0x0002, 0x1102, 0x0020, "CT4850", "SBLive! Value", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x0021, "CT4620", "SBLive!", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x002f, "CT????", "SBLive! mainboard implementation", HAS_AC97 | IS_EMU10K1},

	/* (range unknown) 5.1 EMU10K1 cards */
	{0x1102, 0x0002, 0x1102, 0x100a, "CT????", "SBLive! 5.1", HAS_AC97 | HAS_51 | IS_EMU10K1},

	/* 0x80??..0x805? 4.0 EMU10K1 cards */
	{0x1102, 0x0002, 0x1102, 0x8022, "CT4780", "SBLive! Value", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8023, "CT4790", "SB PCI512", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8024, "CT4760", "SBLive!", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8025, "CT????", "SBLive! Mainboard Implementation", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8026, "CT4830", "SBLive! Value", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8027, "CT4832", "SBLive! Value", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8028, "CT4760", "SBLive! OEM version", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8031, "CT4831", "SBLive! Value", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8040, "CT4760", "SBLive!", HAS_AC97 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8051, "CT4850", "SBLive! Value", HAS_AC97 | IS_EMU10K1},

	/* 0x8061..0x???? 5.1 EMU10K1  cards */
	{0x1102, 0x0002, 0x1102, 0x8061, "SB????", "SBLive! Player 5.1", HAS_AC97 | HAS_51 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8062, "CT4830", "SBLive! 1024", HAS_AC97 | HAS_51 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8064, "SB????", "SBLive! 5.1", HAS_AC97 | HAS_51 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8065, "SB0220", "SBLive! 5.1 Digital", HAS_AC97 | HAS_51 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8066, "CT4780", "SBLive! 5.1 Digital", HAS_AC97 | HAS_51 | IS_EMU10K1},
	{0x1102, 0x0002, 0x1102, 0x8067, "SB????", "SBLive!", HAS_AC97 | HAS_51 | IS_EMU10K1},

	/* Generic SB Live! */
	{0x1102, 0x0002, 0x1102, 0x0000, "SB????", "SBLive! (Unknown model)", HAS_AC97 | IS_EMU10K1},

	/* 0x0041..0x0043 EMU10K2 (some kind of Audigy) cards */

	/* 0x0051..0x0051 5.1 CA0100-IAF cards */
	{0x1102, 0x0004, 0x1102, 0x0051, "SB0090", "Audigy", HAS_AC97 | HAS_51 | IS_EMU10K2},
	/* ES is CA0100-IDF chip that don't work in digital mode */
	{0x1102, 0x0004, 0x1102, 0x0052, "SB0160", "Audigy ES", HAS_AC97 | HAS_71 | IS_EMU10K2 | BROKEN_DIGITAL},
	/* 0x0053..0x005C 5.1 CA0101-NAF cards */
	{0x1102, 0x0004, 0x1102, 0x0053, "SB0090", "Audigy Player/OEM", HAS_AC97 | HAS_51 | IS_EMU10K2},
	{0x1102, 0x0004, 0x1102, 0x0058, "SB0090", "Audigy Player/OEM", HAS_AC97 | HAS_51 | IS_EMU10K2},

	/* 0x1002..0x1009 5.1 CA0102-IAT cards */
	{0x1102, 0x0004, 0x1102, 0x1002, "SB????", "Audigy 2 Platinum", HAS_51 | IS_CA0102},
	{0x1102, 0x0004, 0x1102, 0x1005, "SB????", "Audigy 2 Platinum EX", HAS_51 | IS_CA0102},
	{0x1102, 0x0004, 0x1102, 0x1007, "SB0240", "Audigy 2", HAS_AC97 | HAS_51 | IS_CA0102},

	/* 0x2001..0x2003 7.1 CA0102-ICT cards */
	{0x1102, 0x0004, 0x1102, 0x2001, "SB0350", "Audigy 2 ZS", HAS_AC97 | HAS_71 | IS_CA0102},
	{0x1102, 0x0004, 0x1102, 0x2002, "SB0350", "Audigy 2 ZS", HAS_AC97 | HAS_71 | IS_CA0102},
	/* XXX No reports about 0x2003 & 0x2004 cards */
	{0x1102, 0x0004, 0x1102, 0x2003, "SB0350", "Audigy 2 ZS", HAS_AC97 | HAS_71 | IS_CA0102},
	{0x1102, 0x0004, 0x1102, 0x2004, "SB0350", "Audigy 2 ZS", HAS_AC97 | HAS_71 | IS_CA0102},
	{0x1102, 0x0004, 0x1102, 0x2005, "SB0350", "Audigy 2 ZS", HAS_AC97 | HAS_71 | IS_CA0102},

	/* (range unknown) 7.1 CA0102-xxx Audigy 4 cards */
	{0x1102, 0x0004, 0x1102, 0x2007, "SB0380", "Audigy 4 Pro", HAS_AC97 | HAS_71 | IS_CA0102},

	/* Generic Audigy or Audigy 2 */
	{0x1102, 0x0004, 0x1102, 0x0000, "SB????", "Audigy (Unknown model)", HAS_AC97 | HAS_51 | IS_EMU10K2},

	/* We don't support CA0103-DAT (Audigy LS) cards */
	/* There is NO CA0104-xxx cards */
	/* There is NO CA0105-xxx cards */
	/* We don't support CA0106-DAT (SB Live! 24 bit) cards */
	/* There is NO CA0107-xxx cards */

	/* 0x1000..0x1001 7.1 CA0108-IAT cards */
	{0x1102, 0x0008, 0x1102, 0x1000, "SB????", "Audigy 2 LS", HAS_AC97 | HAS_51 | IS_CA0108 | DIGITAL_ONLY},
	{0x1102, 0x0008, 0x1102, 0x1001, "SB0400", "Audigy 2 Value", HAS_AC97 | HAS_71 | IS_CA0108 | DIGITAL_ONLY},
	{0x1102, 0x0008, 0x1102, 0x1021, "SB0610", "Audigy 4", HAS_AC97 | HAS_71 | IS_CA0108 | DIGITAL_ONLY},

	{0x1102, 0x0008, 0x1102, 0x2001, "SB0530", "Audigy 2 ZS CardBus", HAS_AC97 | HAS_71 | IS_CA0108 | IS_CARDBUS},

	{0x1102, 0x0008, 0x0000, 0x0000, "SB????", "Audigy 2 Value (Unknown model)", HAS_AC97 | HAS_51 | IS_CA0108},
};
/* Unsupported cards */

static struct emu_hwinfo emu_bad_cards[] = {
	/* APS cards should be possible to support */
	{0x1102, 0x0002, 0x1102, 0x4001, "EMUAPS", "E-mu APS", 0},
	{0x1102, 0x0002, 0x1102, 0x4002, "EMUAPS", "E-mu APS", 0},
	{0x1102, 0x0004, 0x1102, 0x4001, "EMU???", "E-mu 1212m [4001]", 0},
	/* Similar-named ("Live!" or "Audigy") cards on different chipsets */
	{0x1102, 0x8064, 0x0000, 0x0000, "SB0100", "SBLive! 5.1 OEM", 0},
	{0x1102, 0x0006, 0x0000, 0x0000, "SB0200", "DELL OEM SBLive! Value", 0},
	{0x1102, 0x0007, 0x0000, 0x0000, "SB0310", "Audigy LS", 0},
};

/*
 * Get best known information about device.
 */
static unsigned int
emu_getcard(device_t dev)
{
	uint16_t device;
	uint16_t subdevice;
	unsigned int thiscard;
	int i;

	device = pci_read_config(dev, PCIR_DEVICE, /* bytes */ 2);
	subdevice = pci_read_config(dev, PCIR_SUBDEV_0, /* bytes */ 2);

	thiscard = 0;
	for (i = 1; i < nitems(emu_cards); i++) {
		if (device == emu_cards[i].device) {
			if (subdevice == emu_cards[i].subdevice) {
				thiscard = i;
				break;
			}
			if (0x0000 == emu_cards[i].subdevice) {
				thiscard = i;
				/*
				 * don't break, we can get more specific card
				 * later in the list.
				 */
			}
		}
	}

	for (i = 0; i < nitems(emu_bad_cards); i++) {
		if (device == emu_bad_cards[i].device) {
			if (subdevice == emu_bad_cards[i].subdevice) {
				thiscard = 0;
				break;
			}
			if (0x0000 == emu_bad_cards[i].subdevice) {
				thiscard = 0;
				break;	/* we avoid all this cards */
			}
		}
	}
	return (thiscard);
}


/*
 * Base hardware interface are 32 (Audigy) or 64 (Audigy2) registers.
 * Some of them are used directly, some of them provide pointer / data pairs.
 */
static uint32_t
emu_rd_nolock(struct emu_sc_info *sc, unsigned int regno, unsigned int size)
{

	KASSERT(sc != NULL, ("emu_rd: NULL sc"));
	switch (size) {
	case 1:
		return (bus_space_read_1(sc->st, sc->sh, regno));
	case 2:
		return (bus_space_read_2(sc->st, sc->sh, regno));
	case 4:
		return (bus_space_read_4(sc->st, sc->sh, regno));
	}
	return (0xffffffff);
}

static void
emu_wr_nolock(struct emu_sc_info *sc, unsigned int regno, uint32_t data, unsigned int size)
{

	KASSERT(sc != NULL, ("emu_rd: NULL sc"));
	switch (size) {
	case 1:
		bus_space_write_1(sc->st, sc->sh, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->st, sc->sh, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->st, sc->sh, regno, data);
		break;
	}
}
/*
 * EMU_PTR / EMU_DATA interface. Access to EMU10Kx is made
 * via (channel, register) pair. Some registers are channel-specific,
 * some not.
 */
uint32_t
emu_rdptr(struct emu_sc_info *sc, unsigned int chn, unsigned int reg)
{
	uint32_t ptr, val, mask, size, offset;

	ptr = ((reg << 16) & sc->address_mask) | (chn & EMU_PTR_CHNO_MASK);

	EMU_RWLOCK();
	emu_wr_nolock(sc, EMU_PTR, ptr, 4);
	val = emu_rd_nolock(sc, EMU_DATA, 4);
	EMU_RWUNLOCK();

	/*
	 * XXX Some register numbers has data size and offset encoded in
	 * it to get only part of 32bit register. This use is not described
	 * in register name, be careful!
	 */
	if (reg & 0xff000000) {
		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		val &= mask;
		val >>= offset;
	}
	return (val);
}

void
emu_wrptr(struct emu_sc_info *sc, unsigned int chn, unsigned int reg, uint32_t data)
{
	uint32_t ptr, mask, size, offset;

	ptr = ((reg << 16) & sc->address_mask) | (chn & EMU_PTR_CHNO_MASK);

	EMU_RWLOCK();
	emu_wr_nolock(sc, EMU_PTR, ptr, 4);
	/*
	 * XXX Another kind of magic encoding in register number. This can
	 * give you side effect - it will read previous data from register
	 * and change only required bits.
	 */
	if (reg & 0xff000000) {
		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		data <<= offset;
		data &= mask;
		data |= emu_rd_nolock(sc, EMU_DATA, 4) & ~mask;
	}
	emu_wr_nolock(sc, EMU_DATA, data, 4);
	EMU_RWUNLOCK();
}
/*
 * EMU_A2_PTR / EMU_DATA2 interface. Access to P16v is made
 * via (channel, register) pair. Some registers are channel-specific,
 * some not. This interface is supported by CA0102 and CA0108 chips only.
 */
uint32_t
emu_rd_p16vptr(struct emu_sc_info *sc, uint16_t chn, uint16_t reg)
{
	uint32_t val;

	/* XXX separate lock? */
	EMU_RWLOCK();
	emu_wr_nolock(sc, EMU_A2_PTR, (reg << 16) | chn, 4);
	val = emu_rd_nolock(sc, EMU_DATA2, 4);

	EMU_RWUNLOCK();

	return (val);
}

void
emu_wr_p16vptr(struct emu_sc_info *sc, uint16_t chn, uint16_t reg, uint32_t data)
{

	EMU_RWLOCK();
	emu_wr_nolock(sc, EMU_A2_PTR, (reg << 16) | chn, 4);
	emu_wr_nolock(sc, EMU_DATA2, data, 4);
	EMU_RWUNLOCK();
}
/*
 * XXX CardBus interface. Not tested on any real hardware.
 */
static void
emu_wr_cbptr(struct emu_sc_info *sc, uint32_t data)
{
	uint32_t val;

	/*
	 * 0x38 is IPE3 (CD S/PDIF interrupt pending register) on CA0102. Seems
	 * to be some reg/value accessible kind of config register on CardBus
	 * CA0108, with value(?) in top 16 bit, address(?) in low 16
	 */

	val = emu_rd_nolock(sc, 0x38, 4);
	emu_wr_nolock(sc, 0x38, data, 4);
	val = emu_rd_nolock(sc, 0x38, 4);

}

/*
 * Direct hardware register access
 * Assume that it is never used to access EMU_PTR-based registers and can run unlocked.
 */
void
emu_wr(struct emu_sc_info *sc, unsigned int regno, uint32_t data, unsigned int size)
{
	KASSERT(regno != EMU_PTR, ("emu_wr: attempt to write to EMU_PTR"));
	KASSERT(regno != EMU_A2_PTR, ("emu_wr: attempt to write to EMU_A2_PTR"));

	emu_wr_nolock(sc, regno, data, size);
}

uint32_t
emu_rd(struct emu_sc_info *sc, unsigned int regno, unsigned int size)
{
	uint32_t rd;

	KASSERT(regno != EMU_DATA, ("emu_rd: attempt to read DATA"));
	KASSERT(regno != EMU_DATA2, ("emu_rd: attempt to read DATA2"));

	rd = emu_rd_nolock(sc, regno, size);
	return (rd);
}

/*
 * Enabling IR MIDI messages is another kind of black magic. It just
 * has to be made this way. It really do it.
 */
void
emu_enable_ir(struct emu_sc_info *sc)
{
	uint32_t iocfg;

	if (sc->is_emu10k2 || sc->is_ca0102) {
		iocfg = emu_rd_nolock(sc, EMU_A_IOCFG, 2);
		emu_wr_nolock(sc, EMU_A_IOCFG, iocfg | EMU_A_IOCFG_GPOUT2, 2);
		DELAY(500);
		emu_wr_nolock(sc, EMU_A_IOCFG, iocfg | EMU_A_IOCFG_GPOUT1 | EMU_A_IOCFG_GPOUT2, 2);
		DELAY(500);
		emu_wr_nolock(sc, EMU_A_IOCFG, iocfg | EMU_A_IOCFG_GPOUT1, 2);
		DELAY(100);
		emu_wr_nolock(sc, EMU_A_IOCFG, iocfg, 2);
		device_printf(sc->dev, "Audigy IR MIDI events enabled.\n");
		sc->enable_ir = 1;
	}
	if (sc->is_emu10k1) {
		iocfg = emu_rd_nolock(sc, EMU_HCFG, 4);
		emu_wr_nolock(sc, EMU_HCFG, iocfg | EMU_HCFG_GPOUT2, 4);
		DELAY(500);
		emu_wr_nolock(sc, EMU_HCFG, iocfg | EMU_HCFG_GPOUT1 | EMU_HCFG_GPOUT2, 4);
		DELAY(100);
		emu_wr_nolock(sc, EMU_HCFG, iocfg, 4);
		device_printf(sc->dev, "SB Live! IR MIDI events enabled.\n");
		sc->enable_ir = 1;
	}
}


/*
 * emu_timer_ - HW timer management
 */
int
emu_timer_create(struct emu_sc_info *sc)
{
	int i, timer;

	timer = -1;

	mtx_lock(&sc->lock);
	for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++)
		if (sc->timer[i] == 0) {
			sc->timer[i] = -1;	/* disable it */
			timer = i;
			mtx_unlock(&sc->lock);
			return (timer);
		}
	mtx_unlock(&sc->lock);

	return (-1);
}

int
emu_timer_set(struct emu_sc_info *sc, int timer, int delay)
{
	int i;

	if (timer < 0)
		return (-1);

	RANGE(delay, 16, 1024);
	RANGE(timer, 0, EMU_MAX_IRQ_CONSUMERS-1);

	mtx_lock(&sc->lock);
	sc->timer[timer] = delay;
	for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++)
		if (sc->timerinterval > sc->timer[i])
			sc->timerinterval = sc->timer[i];

	/* XXX */
	emu_wr(sc, EMU_TIMER, sc->timerinterval & 0x03ff, 2);
	mtx_unlock(&sc->lock);

	return (timer);
}

int
emu_timer_enable(struct emu_sc_info *sc, int timer, int go)
{
	uint32_t x;
	int ena_int;
	int i;

	if (timer < 0)
		return (-1);

	RANGE(timer, 0, EMU_MAX_IRQ_CONSUMERS-1);

	mtx_lock(&sc->lock);

	if ((go == 1) && (sc->timer[timer] < 0))
		sc->timer[timer] = -sc->timer[timer];
	if ((go == 0) && (sc->timer[timer] > 0))
		sc->timer[timer] = -sc->timer[timer];

	ena_int = 0;
	for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++) {
		if (sc->timerinterval > sc->timer[i])
			sc->timerinterval = sc->timer[i];
		if (sc->timer[i] > 0)
			ena_int = 1;
	}

	emu_wr(sc, EMU_TIMER, sc->timerinterval & 0x03ff, 2);

	if (ena_int == 1) {
		x = emu_rd(sc, EMU_INTE, 4);
		x |= EMU_INTE_INTERTIMERENB;
		emu_wr(sc, EMU_INTE, x, 4);
	} else {
		x = emu_rd(sc, EMU_INTE, 4);
		x &= ~EMU_INTE_INTERTIMERENB;
		emu_wr(sc, EMU_INTE, x, 4);
	}
	mtx_unlock(&sc->lock);
	return (0);
}

int
emu_timer_clear(struct emu_sc_info *sc, int timer)
{
	if (timer < 0)
		return (-1);

	RANGE(timer, 0, EMU_MAX_IRQ_CONSUMERS-1);

	emu_timer_enable(sc, timer, 0);

	mtx_lock(&sc->lock);
	if (sc->timer[timer] != 0)
		sc->timer[timer] = 0;
	mtx_unlock(&sc->lock);

	return (timer);
}

/*
 * emu_intr_ - HW interrupt handler management
 */
int
emu_intr_register(struct emu_sc_info *sc, uint32_t inte_mask, uint32_t intr_mask, uint32_t(*func) (void *softc, uint32_t irq), void *isc)
{
	int i;
	uint32_t x;

	mtx_lock(&sc->lock);
	for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++)
		if (sc->ihandler[i].inte_mask == 0) {
			sc->ihandler[i].inte_mask = inte_mask;
			sc->ihandler[i].intr_mask = intr_mask;
			sc->ihandler[i].softc = isc;
			sc->ihandler[i].irq_func = func;
			x = emu_rd(sc, EMU_INTE, 4);
			x |= inte_mask;
			emu_wr(sc, EMU_INTE, x, 4);
			mtx_unlock(&sc->lock);
			if (sc->dbg_level > 1)
				device_printf(sc->dev, "ihandle %d registered\n", i);

			return (i);
		}
	mtx_unlock(&sc->lock);
	if (sc->dbg_level > 1)
		device_printf(sc->dev, "ihandle not registered\n");

	return (-1);
}

int
emu_intr_unregister(struct emu_sc_info *sc, int hnumber)
{
	uint32_t x;
	int i;

	mtx_lock(&sc->lock);

	if (sc->ihandler[hnumber].inte_mask == 0) {
		mtx_unlock(&sc->lock);
		return (-1);
	}

	x = emu_rd(sc, EMU_INTE, 4);
	x &= ~sc->ihandler[hnumber].inte_mask;

	sc->ihandler[hnumber].inte_mask = 0;
	sc->ihandler[hnumber].intr_mask = 0;
	sc->ihandler[hnumber].softc = NULL;
	sc->ihandler[hnumber].irq_func = NULL;

	/* other interrupt handlers may use this EMU_INTE value */
	for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++)
		if (sc->ihandler[i].inte_mask != 0)
			x |= sc->ihandler[i].inte_mask;

	emu_wr(sc, EMU_INTE, x, 4);

	mtx_unlock(&sc->lock);
	return (hnumber);
}

static void
emu_intr(void *p)
{
	struct emu_sc_info *sc = (struct emu_sc_info *)p;
	uint32_t stat, ack;
	int i;

	for (;;) {
		stat = emu_rd(sc, EMU_IPR, 4);
		ack = 0;
		if (stat == 0)
			break;
		emu_wr(sc, EMU_IPR, stat, 4);
		for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++) {
			if ((((sc->ihandler[i].intr_mask) & stat) != 0) &&
			    (((void *)sc->ihandler[i].irq_func) != NULL)) {
				ack |= sc->ihandler[i].irq_func(sc->ihandler[i].softc,
				    (sc->ihandler[i].intr_mask) & stat);
			}
		}
	if (sc->dbg_level > 1)
		if (stat & (~ack))
			device_printf(sc->dev, "Unhandled interrupt: %08x\n", stat & (~ack));

	}

	if ((sc->is_ca0102) || (sc->is_ca0108))
		for (;;) {
			stat = emu_rd(sc, EMU_IPR2, 4);
			ack = 0;
			if (stat == 0)
				break;
			emu_wr(sc, EMU_IPR2, stat, 4);
			if (sc->dbg_level > 1)
				device_printf(sc->dev, "EMU_IPR2: %08x\n", stat);

			break;	/* to avoid infinite loop. should be removed
				 * after completion of P16V interface. */
		}

	if (sc->is_ca0102)
		for (;;) {
			stat = emu_rd(sc, EMU_IPR3, 4);
			ack = 0;
			if (stat == 0)
				break;
			emu_wr(sc, EMU_IPR3, stat, 4);
			if (sc->dbg_level > 1)
				device_printf(sc->dev, "EMU_IPR3: %08x\n", stat);

			break;	/* to avoid infinite loop. should be removed
				 * after completion of S/PDIF interface */
		}
}


/*
 * Get data from private emu10kx structure for PCM buffer allocation.
 * Used by PCM code only.
 */
bus_dma_tag_t
emu_gettag(struct emu_sc_info *sc)
{
	return (sc->mem.dmat);
}

static void
emu_setmap(void *arg, bus_dma_segment_t * segs, int nseg, int error)
{
	bus_addr_t *phys = (bus_addr_t *) arg;

	*phys = error ? 0 : (bus_addr_t) segs->ds_addr;

	if (bootverbose) {
		printf("emu10kx: setmap (%lx, %lx), nseg=%d, error=%d\n",
		    (unsigned long)segs->ds_addr, (unsigned long)segs->ds_len,
		    nseg, error);
	}
}

static void *
emu_malloc(struct emu_mem *mem, uint32_t sz, bus_addr_t * addr,
    bus_dmamap_t *map)
{
	void *dmabuf;
	int error;

	*addr = 0;
	if ((error = bus_dmamem_alloc(mem->dmat, &dmabuf, BUS_DMA_NOWAIT, map))) {
		if (mem->card->dbg_level > 2)
			device_printf(mem->card->dev, "emu_malloc: failed to alloc DMA map: %d\n", error);
		return (NULL);
		}
	if ((error = bus_dmamap_load(mem->dmat, *map, dmabuf, sz, emu_setmap, addr, 0)) || !*addr) {
		if (mem->card->dbg_level > 2)
			device_printf(mem->card->dev, "emu_malloc: failed to load DMA memory: %d\n", error);
		bus_dmamem_free(mem->dmat, dmabuf, *map);
		return (NULL);
		}
	return (dmabuf);
}

static void
emu_free(struct emu_mem *mem, void *dmabuf, bus_dmamap_t map)
{
	bus_dmamap_unload(mem->dmat, map);
	bus_dmamem_free(mem->dmat, dmabuf, map);
}

static void *
emu_memalloc(struct emu_mem *mem, uint32_t sz, bus_addr_t * addr, const char *owner)
{
	uint32_t blksz, start, idx, ofs, tmp, found;
	struct emu_memblk *blk;
	void *membuf;

	blksz = sz / EMUPAGESIZE;
	if (sz > (blksz * EMUPAGESIZE))
		blksz++;
	if (blksz > EMU_MAX_BUFSZ / EMUPAGESIZE) {
		if (mem->card->dbg_level > 2)
			device_printf(mem->card->dev, "emu_memalloc: memory request tool large\n");
		return (NULL);
		}
	/* find a free block in the bitmap */
	found = 0;
	start = 1;
	while (!found && start + blksz < EMU_MAXPAGES) {
		found = 1;
		for (idx = start; idx < start + blksz; idx++)
			if (mem->bmap[idx >> 3] & (1 << (idx & 7)))
				found = 0;
		if (!found)
			start++;
	}
	if (!found) {
		if (mem->card->dbg_level > 2)
			device_printf(mem->card->dev, "emu_memalloc: no free space in bitmap\n");
		return (NULL);
		}
	blk = malloc(sizeof(*blk), M_DEVBUF, M_NOWAIT);
	if (blk == NULL) {
		if (mem->card->dbg_level > 2)
			device_printf(mem->card->dev, "emu_memalloc: buffer allocation failed\n");
		return (NULL);
		}
	bzero(blk, sizeof(*blk));
	membuf = emu_malloc(mem, sz, &blk->buf_addr, &blk->buf_map);
	*addr = blk->buf_addr;
	if (membuf == NULL) {
		if (mem->card->dbg_level > 2)
			device_printf(mem->card->dev, "emu_memalloc: can't setup HW memory\n");
		free(blk, M_DEVBUF);
		return (NULL);
	}
	blk->buf = membuf;
	blk->pte_start = start;
	blk->pte_size = blksz;
	strncpy(blk->owner, owner, 15);
	blk->owner[15] = '\0';
	ofs = 0;
	for (idx = start; idx < start + blksz; idx++) {
		mem->bmap[idx >> 3] |= 1 << (idx & 7);
		tmp = (uint32_t) (blk->buf_addr + ofs);
		mem->ptb_pages[idx] = (tmp << 1) | idx;
		ofs += EMUPAGESIZE;
	}
	SLIST_INSERT_HEAD(&mem->blocks, blk, link);
	return (membuf);
}

static int
emu_memfree(struct emu_mem *mem, void *membuf)
{
	uint32_t idx, tmp;
	struct emu_memblk *blk, *i;

	blk = NULL;
	SLIST_FOREACH(i, &mem->blocks, link) {
		if (i->buf == membuf)
			blk = i;
	}
	if (blk == NULL)
		return (EINVAL);
	SLIST_REMOVE(&mem->blocks, blk, emu_memblk, link);
	emu_free(mem, membuf, blk->buf_map);
	tmp = (uint32_t) (mem->silent_page_addr) << 1;
	for (idx = blk->pte_start; idx < blk->pte_start + blk->pte_size; idx++) {
		mem->bmap[idx >> 3] &= ~(1 << (idx & 7));
		mem->ptb_pages[idx] = tmp | idx;
	}
	free(blk, M_DEVBUF);
	return (0);
}

static int
emu_memstart(struct emu_mem *mem, void *membuf)
{
	struct emu_memblk *blk, *i;

	blk = NULL;
	SLIST_FOREACH(i, &mem->blocks, link) {
		if (i->buf == membuf)
			blk = i;
	}
	if (blk == NULL)
		return (-1);
	return (blk->pte_start);
}


static uint32_t
emu_rate_to_pitch(uint32_t rate)
{
	static uint32_t logMagTable[128] = {
		0x00000, 0x02dfc, 0x05b9e, 0x088e6, 0x0b5d6, 0x0e26f, 0x10eb3, 0x13aa2,
		0x1663f, 0x1918a, 0x1bc84, 0x1e72e, 0x2118b, 0x23b9a, 0x2655d, 0x28ed5,
		0x2b803, 0x2e0e8, 0x30985, 0x331db, 0x359eb, 0x381b6, 0x3a93d, 0x3d081,
		0x3f782, 0x41e42, 0x444c1, 0x46b01, 0x49101, 0x4b6c4, 0x4dc49, 0x50191,
		0x5269e, 0x54b6f, 0x57006, 0x59463, 0x5b888, 0x5dc74, 0x60029, 0x623a7,
		0x646ee, 0x66a00, 0x68cdd, 0x6af86, 0x6d1fa, 0x6f43c, 0x7164b, 0x73829,
		0x759d4, 0x77b4f, 0x79c9a, 0x7bdb5, 0x7dea1, 0x7ff5e, 0x81fed, 0x8404e,
		0x86082, 0x88089, 0x8a064, 0x8c014, 0x8df98, 0x8fef1, 0x91e20, 0x93d26,
		0x95c01, 0x97ab4, 0x9993e, 0x9b79f, 0x9d5d9, 0x9f3ec, 0xa11d8, 0xa2f9d,
		0xa4d3c, 0xa6ab5, 0xa8808, 0xaa537, 0xac241, 0xadf26, 0xafbe7, 0xb1885,
		0xb3500, 0xb5157, 0xb6d8c, 0xb899f, 0xba58f, 0xbc15e, 0xbdd0c, 0xbf899,
		0xc1404, 0xc2f50, 0xc4a7b, 0xc6587, 0xc8073, 0xc9b3f, 0xcb5ed, 0xcd07c,
		0xceaec, 0xd053f, 0xd1f73, 0xd398a, 0xd5384, 0xd6d60, 0xd8720, 0xda0c3,
		0xdba4a, 0xdd3b4, 0xded03, 0xe0636, 0xe1f4e, 0xe384a, 0xe512c, 0xe69f3,
		0xe829f, 0xe9b31, 0xeb3a9, 0xecc08, 0xee44c, 0xefc78, 0xf148a, 0xf2c83,
		0xf4463, 0xf5c2a, 0xf73da, 0xf8b71, 0xfa2f0, 0xfba57, 0xfd1a7, 0xfe8df
	};
	static char logSlopeTable[128] = {
		0x5c, 0x5c, 0x5b, 0x5a, 0x5a, 0x59, 0x58, 0x58,
		0x57, 0x56, 0x56, 0x55, 0x55, 0x54, 0x53, 0x53,
		0x52, 0x52, 0x51, 0x51, 0x50, 0x50, 0x4f, 0x4f,
		0x4e, 0x4d, 0x4d, 0x4d, 0x4c, 0x4c, 0x4b, 0x4b,
		0x4a, 0x4a, 0x49, 0x49, 0x48, 0x48, 0x47, 0x47,
		0x47, 0x46, 0x46, 0x45, 0x45, 0x45, 0x44, 0x44,
		0x43, 0x43, 0x43, 0x42, 0x42, 0x42, 0x41, 0x41,
		0x41, 0x40, 0x40, 0x40, 0x3f, 0x3f, 0x3f, 0x3e,
		0x3e, 0x3e, 0x3d, 0x3d, 0x3d, 0x3c, 0x3c, 0x3c,
		0x3b, 0x3b, 0x3b, 0x3b, 0x3a, 0x3a, 0x3a, 0x39,
		0x39, 0x39, 0x39, 0x38, 0x38, 0x38, 0x38, 0x37,
		0x37, 0x37, 0x37, 0x36, 0x36, 0x36, 0x36, 0x35,
		0x35, 0x35, 0x35, 0x34, 0x34, 0x34, 0x34, 0x34,
		0x33, 0x33, 0x33, 0x33, 0x32, 0x32, 0x32, 0x32,
		0x32, 0x31, 0x31, 0x31, 0x31, 0x31, 0x30, 0x30,
		0x30, 0x30, 0x30, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f
	};
	int i;

	if (rate == 0)
		return (0);
	rate *= 11185;		/* Scale 48000 to 0x20002380 */
	for (i = 31; i > 0; i--) {
		if (rate & 0x80000000) {	/* Detect leading "1" */
			return (((uint32_t) (i - 15) << 20) +
			    logMagTable[0x7f & (rate >> 24)] +
			    (0x7f & (rate >> 17)) *
			    logSlopeTable[0x7f & (rate >> 24)]);
		}
		rate <<= 1;
	}
	/* NOTREACHED */
	return (0);
}

static uint32_t
emu_rate_to_linearpitch(uint32_t rate)
{
	rate = (rate << 8) / 375;
	return ((rate >> 1) + (rate & 1));
}

struct emu_voice *
emu_valloc(struct emu_sc_info *sc)
{
	struct emu_voice *v;
	int i;

	v = NULL;
	mtx_lock(&sc->lock);
	for (i = 0; i < NUM_G && sc->voice[i].busy; i++);
	if (i < NUM_G) {
		v = &sc->voice[i];
		v->busy = 1;
	}
	mtx_unlock(&sc->lock);
	return (v);
}

void
emu_vfree(struct emu_sc_info *sc, struct emu_voice *v)
{
	int i, r;

	mtx_lock(&sc->lock);
	for (i = 0; i < NUM_G; i++) {
		if (v == &sc->voice[i] && sc->voice[i].busy) {
			v->busy = 0;
			/*
			 * XXX What we should do with mono channels?
			 * See -pcm.c emupchan_init for other side of
			 * this problem
			 */
			if (v->slave != NULL)
				r = emu_memfree(&sc->mem, v->vbuf);
		}
	}
	mtx_unlock(&sc->lock);
}

int
emu_vinit(struct emu_sc_info *sc, struct emu_voice *m, struct emu_voice *s,
    uint32_t sz, struct snd_dbuf *b)
{
	void *vbuf;
	bus_addr_t tmp_addr;

	vbuf = emu_memalloc(&sc->mem, sz, &tmp_addr, "vinit");
	if (vbuf == NULL) {
		if(sc->dbg_level > 2)
			device_printf(sc->dev, "emu_memalloc returns NULL in enu_vinit\n");
		return (ENOMEM);
		}
	if (b != NULL)
		sndbuf_setup(b, vbuf, sz);
	m->start = emu_memstart(&sc->mem, vbuf) * EMUPAGESIZE;
	if (m->start < 0) {
		if(sc->dbg_level > 2)
			device_printf(sc->dev, "emu_memstart returns (-1) in enu_vinit\n");
		emu_memfree(&sc->mem, vbuf);
		return (ENOMEM);
	}
	m->end = m->start + sz;
	m->speed = 0;
	m->b16 = 0;
	m->stereo = 0;
	m->running = 0;
	m->ismaster = 1;
	m->vol = 0xff;
	m->buf = tmp_addr;
	m->vbuf = vbuf;
	m->slave = s;
	if (s != NULL) {
		s->start = m->start;
		s->end = m->end;
		s->speed = 0;
		s->b16 = 0;
		s->stereo = 0;
		s->running = 0;
		s->ismaster = 0;
		s->vol = m->vol;
		s->buf = m->buf;
		s->vbuf = NULL;
		s->slave = NULL;
	}
	return (0);
}

void
emu_vsetup(struct emu_voice *v, int fmt, int spd)
{
	if (fmt) {
		v->b16 = (fmt & AFMT_16BIT) ? 1 : 0;
		v->stereo = (AFMT_CHANNEL(fmt) > 1) ? 1 : 0;
		if (v->slave != NULL) {
			v->slave->b16 = v->b16;
			v->slave->stereo = v->stereo;
		}
	}
	if (spd) {
		v->speed = spd;
		if (v->slave != NULL)
			v->slave->speed = v->speed;
	}
}

void
emu_vroute(struct emu_sc_info *sc, struct emu_route *rt,  struct emu_voice *v)
{
	int i;

	for (i = 0; i < 8; i++) {
		v->routing[i] = rt->routing_left[i];
		v->amounts[i] = rt->amounts_left[i];
	}
	if ((v->stereo) && (v->ismaster == 0))
		for (i = 0; i < 8; i++) {
			v->routing[i] = rt->routing_right[i];
			v->amounts[i] = rt->amounts_right[i];
		}

	if ((v->stereo) && (v->slave != NULL))
		emu_vroute(sc, rt, v->slave);
}

void
emu_vwrite(struct emu_sc_info *sc, struct emu_voice *v)
{
	int s;
	uint32_t start, val, silent_page;

	s = (v->stereo ? 1 : 0) + (v->b16 ? 1 : 0);

	v->sa = v->start >> s;
	v->ea = v->end >> s;


	if (v->stereo) {
		emu_wrptr(sc, v->vnum, EMU_CHAN_CPF, EMU_CHAN_CPF_STEREO_MASK);
	} else {
		emu_wrptr(sc, v->vnum, EMU_CHAN_CPF, 0);
	}
	val = v->stereo ? 28 : 30;
	val *= v->b16 ? 1 : 2;
	start = v->sa + val;

	if (sc->is_emu10k1) {
		emu_wrptr(sc, v->vnum, EMU_CHAN_FXRT, ((v->routing[3] << 12) |
		    (v->routing[2] << 8) |
		    (v->routing[1] << 4) |
		    (v->routing[0] << 0)) << 16);
	} else {
		emu_wrptr(sc, v->vnum, EMU_A_CHAN_FXRT1, (v->routing[3] << 24) |
		    (v->routing[2] << 16) |
		    (v->routing[1] << 8) |
		    (v->routing[0] << 0));
		emu_wrptr(sc, v->vnum, EMU_A_CHAN_FXRT2, (v->routing[7] << 24) |
		    (v->routing[6] << 16) |
		    (v->routing[5] << 8) |
		    (v->routing[4] << 0));
		emu_wrptr(sc, v->vnum, EMU_A_CHAN_SENDAMOUNTS, (v->amounts[7] << 24) |
		    (v->amounts[6] << 26) |
		    (v->amounts[5] << 8) |
		    (v->amounts[4] << 0));
	}
	emu_wrptr(sc, v->vnum, EMU_CHAN_PTRX, (v->amounts[0] << 8) | (v->amounts[1] << 0));
	emu_wrptr(sc, v->vnum, EMU_CHAN_DSL, v->ea | (v->amounts[3] << 24));
	emu_wrptr(sc, v->vnum, EMU_CHAN_PSST, v->sa | (v->amounts[2] << 24));

	emu_wrptr(sc, v->vnum, EMU_CHAN_CCCA, start | (v->b16 ? 0 : EMU_CHAN_CCCA_8BITSELECT));
	emu_wrptr(sc, v->vnum, EMU_CHAN_Z1, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_Z2, 0);

	silent_page = ((uint32_t) (sc->mem.silent_page_addr) << 1) | EMU_CHAN_MAP_PTI_MASK;
	emu_wrptr(sc, v->vnum, EMU_CHAN_MAPA, silent_page);
	emu_wrptr(sc, v->vnum, EMU_CHAN_MAPB, silent_page);

	emu_wrptr(sc, v->vnum, EMU_CHAN_CVCF, EMU_CHAN_CVCF_CURRFILTER_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_VTFT, EMU_CHAN_VTFT_FILTERTARGET_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_ATKHLDM, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_DCYSUSM, EMU_CHAN_DCYSUSM_DECAYTIME_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_LFOVAL1, 0x8000);
	emu_wrptr(sc, v->vnum, EMU_CHAN_LFOVAL2, 0x8000);
	emu_wrptr(sc, v->vnum, EMU_CHAN_FMMOD, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_TREMFRQ, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_FM2FRQ2, 0);
	emu_wrptr(sc, v->vnum, EMU_CHAN_ENVVAL, 0x8000);

	emu_wrptr(sc, v->vnum, EMU_CHAN_ATKHLDV, EMU_CHAN_ATKHLDV_HOLDTIME_MASK | EMU_CHAN_ATKHLDV_ATTACKTIME_MASK);
	emu_wrptr(sc, v->vnum, EMU_CHAN_ENVVOL, 0x8000);

	emu_wrptr(sc, v->vnum, EMU_CHAN_PEFE_FILTERAMOUNT, 0x7f);
	emu_wrptr(sc, v->vnum, EMU_CHAN_PEFE_PITCHAMOUNT, 0);
	if ((v->stereo) && (v->slave != NULL))
		emu_vwrite(sc, v->slave);
}

static void
emu_vstop(struct emu_sc_info *sc, char channel, int enable)
{
	int reg;

	reg = (channel & 0x20) ? EMU_SOLEH : EMU_SOLEL;
	channel &= 0x1f;
	reg |= 1 << 24;
	reg |= channel << 16;
	emu_wrptr(sc, 0, reg, enable);
}

void
emu_vtrigger(struct emu_sc_info *sc, struct emu_voice *v, int go)
{
	uint32_t pitch_target, initial_pitch;
	uint32_t cra, cs, ccis;
	uint32_t sample, i;

	if (go) {
		cra = 64;
		cs = v->stereo ? 4 : 2;
		ccis = v->stereo ? 28 : 30;
		ccis *= v->b16 ? 1 : 2;
		sample = v->b16 ? 0x00000000 : 0x80808080;
		for (i = 0; i < cs; i++)
			emu_wrptr(sc, v->vnum, EMU_CHAN_CD0 + i, sample);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CCR_CACHEINVALIDSIZE, 0);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CCR_READADDRESS, cra);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CCR_CACHEINVALIDSIZE, ccis);

		emu_wrptr(sc, v->vnum, EMU_CHAN_IFATN, 0xff00);
		emu_wrptr(sc, v->vnum, EMU_CHAN_VTFT, 0xffffffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CVCF, 0xffffffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_DCYSUSV, 0x00007f7f);
		emu_vstop(sc, v->vnum, 0);

		pitch_target = emu_rate_to_linearpitch(v->speed);
		initial_pitch = emu_rate_to_pitch(v->speed) >> 8;
		emu_wrptr(sc, v->vnum, EMU_CHAN_PTRX_PITCHTARGET, pitch_target);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CPF_PITCH, pitch_target);
		emu_wrptr(sc, v->vnum, EMU_CHAN_IP, initial_pitch);
	} else {
		emu_wrptr(sc, v->vnum, EMU_CHAN_PTRX_PITCHTARGET, 0);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CPF_PITCH, 0);
		emu_wrptr(sc, v->vnum, EMU_CHAN_IFATN, 0xffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_VTFT, 0x0000ffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_CVCF, 0x0000ffff);
		emu_wrptr(sc, v->vnum, EMU_CHAN_IP, 0);
		emu_vstop(sc, v->vnum, 1);
	}
	if ((v->stereo) && (v->slave != NULL))
		emu_vtrigger(sc, v->slave, go);
}

int
emu_vpos(struct emu_sc_info *sc, struct emu_voice *v)
{
	int s, ptr;

	s = (v->b16 ? 1 : 0) + (v->stereo ? 1 : 0);
	ptr = (emu_rdptr(sc, v->vnum, EMU_CHAN_CCCA_CURRADDR) - (v->start >> s)) << s;
	return (ptr & ~0x0000001f);
}


/* fx */
static void
emu_wrefx(struct emu_sc_info *sc, unsigned int pc, unsigned int data)
{
	emu_wrptr(sc, 0, sc->code_base + pc, data);
}


static void
emu_addefxop(struct emu_sc_info *sc, unsigned int op, unsigned int z, unsigned int w, unsigned int x, unsigned int y, uint32_t * pc)
{
	if ((*pc) + 1 > sc->code_size) {
		device_printf(sc->dev, "DSP CODE OVERRUN: attept to write past code_size (pc=%d)\n", (*pc));
		return;
	}
	emu_wrefx(sc, (*pc) * 2, (x << sc->high_operand_shift) | y);
	emu_wrefx(sc, (*pc) * 2 + 1, (op << sc->opcode_shift) | (z << sc->high_operand_shift) | w);
	(*pc)++;
}

static int
sysctl_emu_mixer_control(SYSCTL_HANDLER_ARGS)
{
	struct emu_sc_info *sc;
	int	mixer_id;
	int	new_vol;
	int	err;

	sc = arg1;
	mixer_id = arg2;

	new_vol = emumix_get_volume(sc, mixer_id);
	err = sysctl_handle_int(oidp, &new_vol, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (new_vol < 0 || new_vol > 100)
		return (EINVAL);
	emumix_set_volume(sc, mixer_id, new_vol);

	return (0);
}

static int
emu_addefxmixer(struct emu_sc_info *sc, const char *mix_name, const int mix_id, uint32_t defvolume)
{
	int volgpr;
	char	sysctl_name[32];

	volgpr = emu_rm_gpr_alloc(sc->rm, 1);
	emumix_set_fxvol(sc, volgpr, defvolume);
	/*
	 * Mixer controls with NULL mix_name are handled
	 * by AC97 emulation code or PCM mixer.
	 */
	if (mix_name != NULL) {
		/*
		 * Temporary sysctls should start with underscore,
		 * see freebsd-current mailing list, emu10kx driver
		 * discussion around 2006-05-24.
		 */
		snprintf(sysctl_name, 32, "_%s", mix_name);
		SYSCTL_ADD_PROC(sc->ctx,
			SYSCTL_CHILDREN(sc->root),
			OID_AUTO, sysctl_name,
			CTLTYPE_INT | CTLFLAG_RW, sc, mix_id,
			sysctl_emu_mixer_control, "I", "");
	}

	return (volgpr);
}

static int
sysctl_emu_digitalswitch_control(SYSCTL_HANDLER_ARGS)
{
	struct emu_sc_info *sc;
	int	new_val;
	int	err;

	sc = arg1;

	new_val = (sc->mode == MODE_DIGITAL) ? 1 : 0;	
	err = sysctl_handle_int(oidp, &new_val, 0, req);

	if (err || req->newptr == NULL)
		return (err);
	if (new_val < 0 || new_val > 1)
		return (EINVAL);

	switch (new_val) {
		case 0:
			emumix_set_mode(sc, MODE_ANALOG);
			break;
		case 1:
			emumix_set_mode(sc, MODE_DIGITAL);
			break;
	}
	return (0);
}

static void
emu_digitalswitch(struct emu_sc_info *sc)
{
	/* XXX temporary? */
	SYSCTL_ADD_PROC(sc->ctx,
		SYSCTL_CHILDREN(sc->root),
		OID_AUTO, "_digital",
		CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		sysctl_emu_digitalswitch_control, "I", "Enable digital output");

	return;
}

/*
 * Allocate cache GPRs that will hold mixed output channels
 * and clear it on every DSP run.
 */
#define	EFX_CACHE(CACHE_IDX) do {				\
	sc->cache_gpr[CACHE_IDX] = emu_rm_gpr_alloc(sc->rm, 1); \
	emu_addefxop(sc, ACC3, 					\
		GPR(sc->cache_gpr[CACHE_IDX]), 			\
		DSP_CONST(0), 					\
		DSP_CONST(0), 					\
		DSP_CONST(0), 					\
		&pc);						\
} while (0)

/* Allocate GPR for volume control and route sound: OUT = OUT + IN * VOL */
#define	EFX_ROUTE(TITLE, INP_NR, IN_GPR_IDX, OUT_CACHE_IDX, DEF) do { 	\
	sc->mixer_gpr[IN_GPR_IDX] = emu_addefxmixer(sc, TITLE, IN_GPR_IDX,  DEF); \
	sc->mixer_volcache[IN_GPR_IDX] = DEF; 			\
	emu_addefxop(sc, MACS, 					\
		GPR(sc->cache_gpr[OUT_CACHE_IDX]), 		\
		GPR(sc->cache_gpr[OUT_CACHE_IDX]),		\
		INP_NR,						\
		GPR(sc->mixer_gpr[IN_GPR_IDX]),			\
		&pc);						\
} while (0)

/* allocate GPR, OUT = IN * VOL */
#define	EFX_OUTPUT(TITLE, OUT_CACHE_IDX, OUT_GPR_IDX, OUTP_NR, DEF) do {	\
	sc->mixer_gpr[OUT_GPR_IDX] = emu_addefxmixer(sc, TITLE, OUT_GPR_IDX, DEF); \
	sc->mixer_volcache[OUT_GPR_IDX] = DEF;			\
	emu_addefxop(sc, MACS,					\
		OUTP(OUTP_NR),					\
		DSP_CONST(0),					\
		GPR(sc->cache_gpr[OUT_CACHE_IDX]),		\
		GPR(sc->mixer_gpr[OUT_GPR_IDX]),		\
		&pc);						\
} while (0)

/* like EFX_OUTPUT, but don't allocate mixer gpr */
#define	EFX_OUTPUTD(OUT_CACHE_IDX, OUT_GPR_IDX, OUTP_NR) do {	\
	emu_addefxop(sc, MACS,					\
		OUTP(OUTP_NR),					\
		DSP_CONST(0),					\
		GPR(sc->cache_gpr[OUT_CACHE_IDX]),		\
		GPR(sc->mixer_gpr[OUT_GPR_IDX]),		\
		&pc);						\
} while (0)

/* skip next OPCOUNT instructions if FLAG != 0 */
#define EFX_SKIP(OPCOUNT, FLAG_GPR) do {			\
	emu_addefxop(sc, MACS,					\
		DSP_CONST(0),					\
		GPR(sc->mute_gpr[FLAG_GPR]),					\
		DSP_CONST(0),					\
		DSP_CONST(0),					\
		&pc);						\
	emu_addefxop(sc, SKIP,					\
		DSP_CCR,					\
		DSP_CCR,					\
		COND_NEQ_ZERO,					\
		OPCOUNT,					\
		&pc);						\
} while (0)

#define EFX_COPY(TO, FROM) do {					\
	emu_addefxop(sc, ACC3,					\
		TO,						\
		DSP_CONST(0),					\
		DSP_CONST(0),					\
		FROM,						\
		&pc);						\
} while (0)


static void
emu_initefx(struct emu_sc_info *sc)
{
	unsigned int i;
	uint32_t pc;

	/* stop DSP */
	if (sc->is_emu10k1) {
		emu_wrptr(sc, 0, EMU_DBG, EMU_DBG_SINGLE_STEP);
	} else {
		emu_wrptr(sc, 0, EMU_A_DBG, EMU_A_DBG_SINGLE_STEP);
	}

	/* code size is in instructions */
	pc = 0;
	for (i = 0; i < sc->code_size; i++) {
		if (sc->is_emu10k1) {
			emu_addefxop(sc, ACC3, DSP_CONST(0x0), DSP_CONST(0x0), DSP_CONST(0x0), DSP_CONST(0x0), &pc);
		} else {
			emu_addefxop(sc, SKIP, DSP_CONST(0x0), DSP_CONST(0x0), DSP_CONST(0xf), DSP_CONST(0x0), &pc);
		}
	}

	/* allocate GPRs for mute switches (EFX_SKIP). Mute by default */
	for (i = 0; i < NUM_MUTE; i++) {
		sc->mute_gpr[i] = emu_rm_gpr_alloc(sc->rm, 1);
		emumix_set_gpr(sc, sc->mute_gpr[i], 1);
	}
	emu_digitalswitch(sc);

	pc = 0;

	/*
	 * DSP code below is not good, because:
	 * 1. It can be written smaller, if it can use DSP accumulator register
	 * instead of cache_gpr[].
	 * 2. It can be more careful when volume is 100%, because in DSP
	 * x*0x7fffffff may not be equal to x !
	 */

	/* clean outputs */
	for (i = 0; i < 16 ; i++) {
		emu_addefxop(sc, ACC3, OUTP(i), DSP_CONST(0), DSP_CONST(0), DSP_CONST(0), &pc);
	}


	if (sc->is_emu10k1) {
		EFX_CACHE(C_FRONT_L);
		EFX_CACHE(C_FRONT_R);
		EFX_CACHE(C_REC_L);
		EFX_CACHE(C_REC_R);

		/* fx0 to front/record, 100%/muted by default */
		EFX_ROUTE("pcm_front_l", FX(0), M_FX0_FRONT_L, C_FRONT_L, 100);
		EFX_ROUTE("pcm_front_r", FX(1), M_FX1_FRONT_R, C_FRONT_R, 100);
		EFX_ROUTE(NULL, FX(0), M_FX0_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, FX(1), M_FX1_REC_R, C_REC_R, 0);

		/* in0, from AC97 codec output */
		EFX_ROUTE("ac97_front_l", INP(IN_AC97_L), M_IN0_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE("ac97_front_r", INP(IN_AC97_R), M_IN0_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE("ac97_rec_l", INP(IN_AC97_L), M_IN0_REC_L, C_REC_L, 0);
		EFX_ROUTE("ac97_rec_r", INP(IN_AC97_R), M_IN0_REC_R, C_REC_R, 0);

		/* in1, from CD S/PDIF */
		/* XXX EFX_SKIP 4 assumes that each EFX_ROUTE is one DSP op */
		EFX_SKIP(4, CDSPDIFMUTE);
		EFX_ROUTE(NULL, INP(IN_SPDIF_CD_L), M_IN1_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(IN_SPDIF_CD_R), M_IN1_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(IN_SPDIF_CD_L), M_IN1_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(IN_SPDIF_CD_R), M_IN1_REC_R, C_REC_R, 0);
		
		if (sc->dbg_level > 0) {
			/* in2, ZoomVide (???) */
			EFX_ROUTE("zoom_front_l", INP(IN_ZOOM_L), M_IN2_FRONT_L, C_FRONT_L, 0);
			EFX_ROUTE("zoom_front_r", INP(IN_ZOOM_R), M_IN2_FRONT_R, C_FRONT_R, 0);
			EFX_ROUTE("zoom_rec_l", INP(IN_ZOOM_L), M_IN2_REC_L, C_REC_L, 0);
			EFX_ROUTE("zoom_rec_r", INP(IN_ZOOM_R), M_IN2_REC_R, C_REC_R, 0);
		}

		/* in3, TOSLink  */
		EFX_ROUTE(NULL, INP(IN_TOSLINK_L), M_IN3_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(IN_TOSLINK_R), M_IN3_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(IN_TOSLINK_L), M_IN3_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(IN_TOSLINK_R), M_IN3_REC_R, C_REC_R, 0);
		/* in4, LineIn  */
		EFX_ROUTE(NULL, INP(IN_LINE1_L), M_IN4_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(IN_LINE1_R), M_IN4_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(IN_LINE1_L), M_IN4_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(IN_LINE1_R), M_IN4_REC_R, C_REC_R, 0);

		/* in5, on-card S/PDIF */
		EFX_ROUTE(NULL, INP(IN_COAX_SPDIF_L), M_IN5_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(IN_COAX_SPDIF_R), M_IN5_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(IN_COAX_SPDIF_L), M_IN5_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(IN_COAX_SPDIF_R), M_IN5_REC_R, C_REC_R, 0);

		/* in6, Line2 on Live!Drive */
		EFX_ROUTE(NULL, INP(IN_LINE2_L), M_IN6_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(IN_LINE2_R), M_IN6_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(IN_LINE2_L), M_IN6_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(IN_LINE2_R), M_IN6_REC_R, C_REC_R, 0);

		if (sc->dbg_level > 0) {
			/* in7, unknown */
			EFX_ROUTE("in7_front_l", INP(0xE), M_IN7_FRONT_L, C_FRONT_L, 0);
			EFX_ROUTE("in7_front_r", INP(0xF), M_IN7_FRONT_R, C_FRONT_R, 0);
			EFX_ROUTE("in7_rec_l", INP(0xE), M_IN7_REC_L, C_REC_L, 0);
			EFX_ROUTE("in7_rec_r", INP(0xF), M_IN7_REC_R, C_REC_R, 0);
		}

		/* analog and digital */
		EFX_OUTPUT("master_front_l", C_FRONT_L, M_MASTER_FRONT_L, OUT_AC97_L, 100);
		EFX_OUTPUT("master_front_r", C_FRONT_R, M_MASTER_FRONT_R, OUT_AC97_R, 100);
		/* S/PDIF */
		EFX_OUTPUTD(C_FRONT_L, M_MASTER_FRONT_L, OUT_TOSLINK_L);
		EFX_OUTPUTD(C_FRONT_R, M_MASTER_FRONT_R, OUT_TOSLINK_R);
		/* Headphones */
		EFX_OUTPUTD(C_FRONT_L, M_MASTER_FRONT_L, OUT_HEADPHONE_L);
		EFX_OUTPUTD(C_FRONT_R, M_MASTER_FRONT_R, OUT_HEADPHONE_R);

		/* rec output to "ADC" */
		EFX_OUTPUT("master_rec_l", C_REC_L, M_MASTER_REC_L, OUT_ADC_REC_L, 100);
		EFX_OUTPUT("master_rec_r", C_REC_R, M_MASTER_REC_R, OUT_ADC_REC_R, 100);

		if (!(sc->mch_disabled)) {
			/*
			 * Additional channel volume is controlled by mixer in
			 * emu_dspmixer_set() in -pcm.c
			 */

			/* fx2/3 (pcm1) to rear */
			EFX_CACHE(C_REAR_L);
			EFX_CACHE(C_REAR_R);
			EFX_ROUTE(NULL, FX(2), M_FX2_REAR_L, C_REAR_L, 100);
			EFX_ROUTE(NULL, FX(3), M_FX3_REAR_R, C_REAR_R, 100);

			EFX_OUTPUT(NULL, C_REAR_L, M_MASTER_REAR_L, OUT_REAR_L, 100);
			EFX_OUTPUT(NULL, C_REAR_R, M_MASTER_REAR_R, OUT_REAR_R, 100);
			if (sc->has_51) {
				/* fx4 (pcm2) to center */
				EFX_CACHE(C_CENTER);
				EFX_ROUTE(NULL, FX(4), M_FX4_CENTER, C_CENTER, 100);
				EFX_OUTPUT(NULL, C_CENTER, M_MASTER_CENTER, OUT_D_CENTER, 100);

				/* XXX in digital mode (default) this should be muted because
				this output is shared with digital out */
				EFX_SKIP(1, ANALOGMUTE);
				EFX_OUTPUTD(C_CENTER, M_MASTER_CENTER, OUT_A_CENTER);

				/* fx5 (pcm3) to sub */
				EFX_CACHE(C_SUB);
				EFX_ROUTE(NULL, FX(5), M_FX5_SUBWOOFER, C_SUB, 100);
				EFX_OUTPUT(NULL, C_SUB, M_MASTER_SUBWOOFER, OUT_D_SUB, 100);

				/* XXX in digital mode (default) this should be muted because
				this output is shared with digital out */
				EFX_SKIP(1, ANALOGMUTE);
				EFX_OUTPUTD(C_SUB, M_MASTER_SUBWOOFER, OUT_A_SUB);

			}
		} else {
			/* SND_EMU10KX_MULTICHANNEL_DISABLED */
			EFX_OUTPUT(NULL, C_FRONT_L, M_MASTER_REAR_L, OUT_REAR_L, 57); /* 75%*75% */
			EFX_OUTPUT(NULL, C_FRONT_R, M_MASTER_REAR_R, OUT_REAR_R, 57); /* 75%*75% */

#if 0
			/* XXX 5.1 does not work */

			if (sc->has_51) {
				/* (fx0+fx1)/2 to center */
				EFX_CACHE(C_CENTER);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_CENTER]),
					GPR(sc->cache_gpr[C_CENTER]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_L]),
					&pc);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_CENTER]),
					GPR(sc->cache_gpr[C_CENTER]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_R]),
					&pc);
				EFX_OUTPUT(NULL, C_CENTER, M_MASTER_CENTER, OUT_D_CENTER, 100);

				/* XXX in digital mode (default) this should be muted because
				this output is shared with digital out */
				EFX_SKIP(1, ANALOGMUTE);
				EFX_OUTPUTD(C_CENTER, M_MASTER_CENTER, OUT_A_CENTER);

				/* (fx0+fx1)/2  to sub */
				EFX_CACHE(C_SUB);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_CENTER]),
					GPR(sc->cache_gpr[C_CENTER]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_L]),
					&pc);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_CENTER]),
					GPR(sc->cache_gpr[C_CENTER]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_R]),
					&pc);
				/* XXX add lowpass filter here */

				EFX_OUTPUT(NULL, C_SUB, M_MASTER_SUBWOOFER, OUT_D_SUB, 100);

				/* XXX in digital mode (default) this should be muted because
				this output is shared with digital out */
				EFX_SKIP(1, ANALOGMUTE);
				EFX_OUTPUTD(C_SUB, M_MASTER_SUBWOOFER, OUT_A_SUB);
			}
#endif
		} /* !mch_disabled */
		if (sc->mch_rec) {
			/*
			 * MCH RECORDING , hight 16 slots. On 5.1 cards first 4 slots
			 * are used as outputs and already filled with data
			 */
			/*
			 * XXX On Live! cards stream does not begin at zero offset.
			 * It can be HW, driver or sound buffering problem.
			 * Use sync substream (offset 0x3E) to let userland find
			 * correct data.
			 */

			/*
			 * Substream map (in byte offsets, each substream is 2 bytes):
			 *	0x00..0x1E - outputs
			 *	0x20..0x3E - FX, inputs and sync stream
			 */

			/* First 2 channels (offset 0x20,0x22) are empty */
			for(i = (sc->has_51 ? 2 : 0); i < 2; i++)
				EFX_COPY(FX2(i), DSP_CONST(0));

			/* PCM Playback monitoring, offset 0x24..0x2A */
			for(i = 0; i < 4; i++)
				EFX_COPY(FX2(i+2), FX(i));

			/* Copy of some inputs, offset 0x2C..0x3C */
			for(i = 0; i < 9; i++)
				EFX_COPY(FX2(i+8), INP(i));

			/* sync data (0xc0de, offset 0x3E) */
			sc->dummy_gpr = emu_rm_gpr_alloc(sc->rm, 1);
			emumix_set_gpr(sc, sc->dummy_gpr, 0xc0de0000);

			EFX_COPY(FX2(15), GPR(sc->dummy_gpr));
		} /* mch_rec */
	} else /* emu10k2 and later */ {
		EFX_CACHE(C_FRONT_L);
		EFX_CACHE(C_FRONT_R);
		EFX_CACHE(C_REC_L);
		EFX_CACHE(C_REC_R);

		/* fx0 to front/record, 100%/muted by default */
		/*
		 * FRONT_[L|R] is controlled by AC97 emulation in
		 * emu_ac97_[read|write]_emulation in -pcm.c
		 */
		EFX_ROUTE(NULL, FX(0), M_FX0_FRONT_L, C_FRONT_L, 100);
		EFX_ROUTE(NULL, FX(1), M_FX1_FRONT_R, C_FRONT_R, 100);
		EFX_ROUTE(NULL, FX(0), M_FX0_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, FX(1), M_FX1_REC_R, C_REC_R, 0);

		/* in0, from AC97 codec output */
		EFX_ROUTE(NULL, INP(A_IN_AC97_L), M_IN0_FRONT_L, C_FRONT_L, 100);
		EFX_ROUTE(NULL, INP(A_IN_AC97_R), M_IN0_FRONT_R, C_FRONT_R, 100);
		EFX_ROUTE(NULL, INP(A_IN_AC97_L), M_IN0_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_AC97_R), M_IN0_REC_R, C_REC_R, 0);

		/* in1, from CD S/PDIF */
		EFX_ROUTE(NULL, INP(A_IN_SPDIF_CD_L), M_IN1_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_SPDIF_CD_R), M_IN1_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(A_IN_SPDIF_CD_L), M_IN1_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_SPDIF_CD_R), M_IN1_REC_R, C_REC_R, 0);

		/* in2, optical & coax S/PDIF on AudigyDrive*/
		/* XXX Should be muted when GPRSCS valid stream == 0 */
		EFX_ROUTE(NULL, INP(A_IN_O_SPDIF_L), M_IN2_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_O_SPDIF_R), M_IN2_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(A_IN_O_SPDIF_L), M_IN2_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_O_SPDIF_R), M_IN2_REC_R, C_REC_R, 0);

		if (sc->dbg_level > 0) {
			/* in3, unknown */
			EFX_ROUTE("in3_front_l", INP(0x6), M_IN3_FRONT_L, C_FRONT_L, 0);
			EFX_ROUTE("in3_front_r", INP(0x7), M_IN3_FRONT_R, C_FRONT_R, 0);
			EFX_ROUTE("in3_rec_l", INP(0x6), M_IN3_REC_L, C_REC_L, 0);
			EFX_ROUTE("in3_rec_r", INP(0x7), M_IN3_REC_R, C_REC_R, 0);
		}

		/* in4, LineIn 2 on AudigyDrive */
		EFX_ROUTE(NULL, INP(A_IN_LINE2_L), M_IN4_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_LINE2_R), M_IN4_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(A_IN_LINE2_L), M_IN4_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_LINE2_R), M_IN4_REC_R, C_REC_R, 0);

		/* in5, on-card S/PDIF */
		EFX_ROUTE(NULL, INP(A_IN_R_SPDIF_L), M_IN5_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_R_SPDIF_R), M_IN5_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(A_IN_R_SPDIF_L), M_IN5_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_R_SPDIF_R), M_IN5_REC_R, C_REC_R, 0);

		/* in6, AUX2 on AudigyDrive */
		EFX_ROUTE(NULL, INP(A_IN_AUX2_L), M_IN6_FRONT_L, C_FRONT_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_AUX2_R), M_IN6_FRONT_R, C_FRONT_R, 0);
		EFX_ROUTE(NULL, INP(A_IN_AUX2_L), M_IN6_REC_L, C_REC_L, 0);
		EFX_ROUTE(NULL, INP(A_IN_AUX2_R), M_IN6_REC_R, C_REC_R, 0);

		if (sc->dbg_level > 0) {
			/* in7, unknown */
			EFX_ROUTE("in7_front_l", INP(0xE), M_IN7_FRONT_L, C_FRONT_L, 0);
			EFX_ROUTE("in7_front_r", INP(0xF), M_IN7_FRONT_R, C_FRONT_R, 0);
			EFX_ROUTE("in7_rec_l", INP(0xE), M_IN7_REC_L, C_REC_L, 0);
			EFX_ROUTE("in7_rec_r", INP(0xF), M_IN7_REC_R, C_REC_R, 0);
		}

		/* front output to headphones and  alog and digital *front */
		/* volume controlled by AC97 emulation */
		EFX_OUTPUT(NULL, C_FRONT_L, M_MASTER_FRONT_L, A_OUT_A_FRONT_L, 100);
		EFX_OUTPUT(NULL, C_FRONT_R, M_MASTER_FRONT_R, A_OUT_A_FRONT_R, 100);
		EFX_OUTPUTD(C_FRONT_L, M_MASTER_FRONT_L, A_OUT_D_FRONT_L);
		EFX_OUTPUTD(C_FRONT_R, M_MASTER_FRONT_R, A_OUT_D_FRONT_R);
		EFX_OUTPUTD(C_FRONT_L, M_MASTER_FRONT_L, A_OUT_HPHONE_L);
		EFX_OUTPUTD(C_FRONT_R, M_MASTER_FRONT_R, A_OUT_HPHONE_R);

		/* rec output to "ADC" */
		/* volume controlled by AC97 emulation */
		EFX_OUTPUT(NULL, C_REC_L, M_MASTER_REC_L, A_OUT_ADC_REC_L, 100);
		EFX_OUTPUT(NULL, C_REC_R, M_MASTER_REC_R, A_OUT_ADC_REC_R, 100);

		if (!(sc->mch_disabled)) {
			/*
			 * Additional channel volume is controlled by mixer in
			 * emu_dspmixer_set() in -pcm.c
			 */

			/* fx2/3 (pcm1) to rear */
			EFX_CACHE(C_REAR_L);
			EFX_CACHE(C_REAR_R);
			EFX_ROUTE(NULL, FX(2), M_FX2_REAR_L, C_REAR_L, 100);
			EFX_ROUTE(NULL, FX(3), M_FX3_REAR_R, C_REAR_R, 100);

			EFX_OUTPUT(NULL, C_REAR_L, M_MASTER_REAR_L, A_OUT_A_REAR_L, 100);
			EFX_OUTPUT(NULL, C_REAR_R, M_MASTER_REAR_R, A_OUT_A_REAR_R, 100);
			EFX_OUTPUTD(C_REAR_L, M_MASTER_REAR_L, A_OUT_D_REAR_L);
			EFX_OUTPUTD(C_REAR_R, M_MASTER_REAR_R, A_OUT_D_REAR_R);

			/* fx4 (pcm2) to center */
			EFX_CACHE(C_CENTER);
			EFX_ROUTE(NULL, FX(4), M_FX4_CENTER, C_CENTER, 100);
			EFX_OUTPUT(NULL, C_CENTER, M_MASTER_CENTER, A_OUT_D_CENTER, 100);
#if 0
			/*
			 * XXX in digital mode (default) this should be muted
			 * because this output is shared with digital out
			 */
			EFX_OUTPUTD(C_CENTER, M_MASTER_CENTER, A_OUT_A_CENTER);
#endif
			/* fx5 (pcm3) to sub */
			EFX_CACHE(C_SUB);
			EFX_ROUTE(NULL, FX(5), M_FX5_SUBWOOFER, C_SUB, 100);
			EFX_OUTPUT(NULL, C_SUB, M_MASTER_SUBWOOFER, A_OUT_D_SUB, 100);
#if 0
			/*
			 * XXX in digital mode (default) this should be muted
			 * because this output is shared with digital out
			 */
			EFX_OUTPUTD(C_SUB, M_MASTER_SUBWOOFER, A_OUT_A_SUB);
#endif
			if (sc->has_71) {
				/* XXX this will broke headphones on AudigyDrive */
				/* fx6/7 (pcm4) to side */
				EFX_CACHE(C_SIDE_L);
				EFX_CACHE(C_SIDE_R);
				EFX_ROUTE(NULL, FX(6), M_FX6_SIDE_L, C_SIDE_L, 100);
				EFX_ROUTE(NULL, FX(7), M_FX7_SIDE_R, C_SIDE_R, 100);
				EFX_OUTPUT(NULL, C_SIDE_L, M_MASTER_SIDE_L, A_OUT_A_SIDE_L, 100);
				EFX_OUTPUT(NULL, C_SIDE_R, M_MASTER_SIDE_R, A_OUT_A_SIDE_R, 100);
				EFX_OUTPUTD(C_SIDE_L, M_MASTER_SIDE_L, A_OUT_D_SIDE_L);
				EFX_OUTPUTD(C_SIDE_R, M_MASTER_SIDE_R, A_OUT_D_SIDE_R);
			}
		} else {	/* mch_disabled */
			EFX_OUTPUTD(C_FRONT_L, M_MASTER_FRONT_L, A_OUT_A_REAR_L);
			EFX_OUTPUTD(C_FRONT_R, M_MASTER_FRONT_R, A_OUT_A_REAR_R);

			EFX_OUTPUTD(C_FRONT_L, M_MASTER_FRONT_L, A_OUT_D_REAR_L);
			EFX_OUTPUTD(C_FRONT_R, M_MASTER_FRONT_R, A_OUT_D_REAR_R);

			if (sc->has_51) {
				/* (fx0+fx1)/2 to center */
				EFX_CACHE(C_CENTER);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_CENTER]),
					GPR(sc->cache_gpr[C_CENTER]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_L]),
					&pc);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_CENTER]),
					GPR(sc->cache_gpr[C_CENTER]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_R]),
					&pc);
				EFX_OUTPUT(NULL, C_CENTER, M_MASTER_CENTER, A_OUT_D_CENTER, 100);

				/* XXX in digital mode (default) this should be muted because
				this output is shared with digital out */
				EFX_SKIP(1, ANALOGMUTE);
				EFX_OUTPUTD(C_CENTER, M_MASTER_CENTER, A_OUT_A_CENTER);

				/* (fx0+fx1)/2  to sub */
				EFX_CACHE(C_SUB);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_SUB]),
					GPR(sc->cache_gpr[C_SUB]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_L]),
					&pc);
				emu_addefxop(sc, MACS,
					GPR(sc->cache_gpr[C_SUB]),
					GPR(sc->cache_gpr[C_SUB]),
					DSP_CONST(0xd), /* = 1/2 */
					GPR(sc->cache_gpr[C_FRONT_R]),
					&pc);
				/* XXX add lowpass filter here */

				EFX_OUTPUT(NULL, C_SUB, M_MASTER_SUBWOOFER, A_OUT_D_SUB, 100);

				/* XXX in digital mode (default) this should be muted because
				this output is shared with digital out */
				EFX_SKIP(1, ANALOGMUTE);
				EFX_OUTPUTD(C_SUB, M_MASTER_SUBWOOFER, A_OUT_A_SUB);
			}
		} /* mch_disabled */
		if (sc->mch_rec) {
			/* MCH RECORDING, high 32 slots */

			/*
			 * Stream map (in byte offsets):
			 *	0x00..0x3E - outputs
			 *	0x40..0x7E - FX, inputs
			 *	each substream is 2 bytes.
			 */
			/*
			 * XXX Audigy 2 Value cards (and, possibly,
			 * Audigy 4) write some unknown data in place of
			 * some outputs (offsets 0x20..0x3F) and one
			 * input (offset 0x7E).
			 */

			/* PCM Playback monitoring, offsets 0x40..0x5E */
			for(i = 0; i < 16; i++)
				EFX_COPY(FX2(i), FX(i));

			/* Copy of all inputs, offsets 0x60..0x7E */
			for(i = 0; i < 16; i++)
				EFX_COPY(FX2(i+16), INP(i));
#if 0
			/* XXX Audigy seems to work correct and does not need this */
			/* sync data (0xc0de), offset 0x7E */
			sc->dummy_gpr = emu_rm_gpr_alloc(sc->rm, 1);
			emumix_set_gpr(sc, sc->dummy_gpr, 0xc0de0000);
			EFX_COPY(FX2(31), GPR(sc->dummy_gpr));
#endif
		} /* mch_rec */
	}

	sc->routing_code_end = pc;

	/* start DSP */
	if (sc->is_emu10k1) {
		emu_wrptr(sc, 0, EMU_DBG, 0);
	} else {
		emu_wrptr(sc, 0, EMU_A_DBG, 0);
	}
}

/* /dev/em10kx */
static d_open_t		emu10kx_open;
static d_close_t	emu10kx_close;
static d_read_t		emu10kx_read;

static struct cdevsw emu10kx_cdevsw = {
	.d_open = 	emu10kx_open,
	.d_close =	emu10kx_close,
	.d_read = 	emu10kx_read,
	.d_name = 	"emu10kx",
	.d_version = 	D_VERSION,
};


static int
emu10kx_open(struct cdev *i_dev, int flags __unused, int mode __unused, struct thread *td __unused)
{
	int error;
	struct emu_sc_info *sc;

	sc = i_dev->si_drv1;
	mtx_lock(&sc->emu10kx_lock);
	if (sc->emu10kx_isopen) {
		mtx_unlock(&sc->emu10kx_lock);
		return (EBUSY);
	}
	sc->emu10kx_isopen = 1;
	mtx_unlock(&sc->emu10kx_lock);
	if (sbuf_new(&sc->emu10kx_sbuf, NULL, 4096, 0) == NULL) {
		error = ENXIO;
		goto out;
	}
	sc->emu10kx_bufptr = 0;
	error = (emu10kx_prepare(sc, &sc->emu10kx_sbuf) > 0) ? 0 : ENOMEM;
out:
	if (error) {
		mtx_lock(&sc->emu10kx_lock);
		sc->emu10kx_isopen = 0;
		mtx_unlock(&sc->emu10kx_lock);
	}
	return (error);
}

static int
emu10kx_close(struct cdev *i_dev, int flags __unused, int mode __unused, struct thread *td __unused)
{
	struct emu_sc_info *sc;

	sc = i_dev->si_drv1;

	mtx_lock(&sc->emu10kx_lock);
	if (!(sc->emu10kx_isopen)) {
		mtx_unlock(&sc->emu10kx_lock);
		return (EBADF);
	}
	sbuf_delete(&sc->emu10kx_sbuf);
	sc->emu10kx_isopen = 0;
	mtx_unlock(&sc->emu10kx_lock);

	return (0);
}

static int
emu10kx_read(struct cdev *i_dev, struct uio *buf, int flag __unused)
{
	int l, err;
	struct emu_sc_info *sc;

	sc = i_dev->si_drv1;
	mtx_lock(&sc->emu10kx_lock);
	if (!(sc->emu10kx_isopen)) {
		mtx_unlock(&sc->emu10kx_lock);
		return (EBADF);
	}
	mtx_unlock(&sc->emu10kx_lock);

	l = min(buf->uio_resid, sbuf_len(&sc->emu10kx_sbuf) - sc->emu10kx_bufptr);
	err = (l > 0) ? uiomove(sbuf_data(&sc->emu10kx_sbuf) + sc->emu10kx_bufptr, l, buf) : 0;
	sc->emu10kx_bufptr += l;

	return (err);
}

static int
emu10kx_prepare(struct emu_sc_info *sc, struct sbuf *s)
{
	int i;

	sbuf_printf(s, "FreeBSD EMU10Kx Audio Driver\n");
	sbuf_printf(s, "\nHardware resource usage:\n");
	sbuf_printf(s, "DSP General Purpose Registers: %d used, %d total\n", sc->rm->num_used, sc->rm->num_gprs);
	sbuf_printf(s, "DSP Instruction Registers: %d used, %d total\n", sc->routing_code_end, sc->code_size);
	sbuf_printf(s, "Card supports");
	if (sc->has_ac97) {
		sbuf_printf(s, " AC97 codec");
	} else {
		sbuf_printf(s, " NO AC97 codec");
	}
	if (sc->has_51) {
		if (sc->has_71)
			sbuf_printf(s, " and 7.1 output");
		else
			sbuf_printf(s, " and 5.1 output");
	}
	if (sc->is_emu10k1)
		sbuf_printf(s, ", SBLive! DSP code");
	if (sc->is_emu10k2)
		sbuf_printf(s, ", Audigy DSP code");
	if (sc->is_ca0102)
		sbuf_printf(s, ", Audigy DSP code with Audigy2 hacks");
	if (sc->is_ca0108)
		sbuf_printf(s, ", Audigy DSP code with Audigy2Value hacks");
	sbuf_printf(s, "\n");
	if (sc->broken_digital)
		sbuf_printf(s, "Digital mode unsupported\n");
	sbuf_printf(s, "\nInstalled devices:\n");
	for (i = 0; i < RT_COUNT; i++)
		if (sc->pcm[i] != NULL)
			if (device_is_attached(sc->pcm[i])) {
				sbuf_printf(s, "%s on %s\n", device_get_desc(sc->pcm[i]), device_get_nameunit(sc->pcm[i]));
			}
	if (sc->midi[0] != NULL)
		if (device_is_attached(sc->midi[0])) {
			sbuf_printf(s, "EMU10Kx MIDI Interface\n");
			sbuf_printf(s, "\tOn-card connector on %s\n", device_get_nameunit(sc->midi[0]));
		}
	if (sc->midi[1] != NULL)
		if (device_is_attached(sc->midi[1])) {
			sbuf_printf(s, "\tOn-Drive connector on %s\n", device_get_nameunit(sc->midi[1]));
		}
	if (sc->midi[0] != NULL)
		if (device_is_attached(sc->midi[0])) {
			sbuf_printf(s, "\tIR receiver MIDI events %s\n", sc->enable_ir ? "enabled" : "disabled");
		}
	sbuf_printf(s, "Card is in %s mode\n", (sc->mode == MODE_ANALOG) ? "analog" : "digital");

	sbuf_finish(s);
	return (sbuf_len(s));
}

/* INIT & UNINIT */
static int
emu10kx_dev_init(struct emu_sc_info *sc)
{
	int unit;

	mtx_init(&sc->emu10kx_lock, device_get_nameunit(sc->dev), "kxdevlock", 0);
	unit = device_get_unit(sc->dev);

	sc->cdev = make_dev(&emu10kx_cdevsw, PCMMINOR(unit), UID_ROOT, GID_WHEEL, 0640, "emu10kx%d", unit);
	if (sc->cdev != NULL) {
		sc->cdev->si_drv1 = sc;
		return (0);
	}
	return (ENXIO);
}

static int
emu10kx_dev_uninit(struct emu_sc_info *sc)
{
	mtx_lock(&sc->emu10kx_lock);
	if (sc->emu10kx_isopen) {
		mtx_unlock(&sc->emu10kx_lock);
		return (EBUSY);
	}
	if (sc->cdev)
		destroy_dev(sc->cdev);
	sc->cdev = NULL;

	mtx_destroy(&sc->emu10kx_lock);
	return (0);
}

/* resource manager */
int
emu_rm_init(struct emu_sc_info *sc)
{
	int i;
	int maxcount;
	struct emu_rm *rm;

	rm = malloc(sizeof(struct emu_rm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rm == NULL) {
		return (ENOMEM);
	}
	sc->rm = rm;
	rm->card = sc;
	maxcount = sc->num_gprs;
	rm->num_used = 0;
	mtx_init(&(rm->gpr_lock), device_get_nameunit(sc->dev), "gpr alloc", MTX_DEF);
	rm->num_gprs = (maxcount < EMU_MAX_GPR ? maxcount : EMU_MAX_GPR);
	for (i = 0; i < rm->num_gprs; i++)
		rm->allocmap[i] = 0;
	/* pre-allocate gpr[0] */
	rm->allocmap[0] = 1;
	rm->last_free_gpr = 1;

	return (0);
}

int
emu_rm_uninit(struct emu_sc_info *sc)
{
	int i;

	if (sc->dbg_level > 1) {
		mtx_lock(&(sc->rm->gpr_lock));
		for (i = 1; i < sc->rm->last_free_gpr; i++)
			if (sc->rm->allocmap[i] > 0)
				device_printf(sc->dev, "rm: gpr %d not free before uninit\n", i);
		mtx_unlock(&(sc->rm->gpr_lock));
	}

	mtx_destroy(&(sc->rm->gpr_lock));
	free(sc->rm, M_DEVBUF);
	return (0);
}

static int
emu_rm_gpr_alloc(struct emu_rm *rm, int count)
{
	int i, j;
	int allocated_gpr;

	allocated_gpr = rm->num_gprs;
	/* try fast way first */
	mtx_lock(&(rm->gpr_lock));
	if (rm->last_free_gpr + count <= rm->num_gprs) {
		allocated_gpr = rm->last_free_gpr;
		rm->last_free_gpr += count;
		rm->allocmap[allocated_gpr] = count;
		for (i = 1; i < count; i++)
			rm->allocmap[allocated_gpr + i] = -(count - i);
	} else {
		/* longer */
		i = 0;
		allocated_gpr = rm->num_gprs;
		while (i < rm->last_free_gpr - count) {
			if (rm->allocmap[i] > 0) {
				i += rm->allocmap[i];
			} else {
				allocated_gpr = i;
				for (j = 1; j < count; j++) {
					if (rm->allocmap[i + j] != 0)
						allocated_gpr = rm->num_gprs;
				}
				if (allocated_gpr == i)
					break;
			}
		}
		if (allocated_gpr + count < rm->last_free_gpr) {
			rm->allocmap[allocated_gpr] = count;
			for (i = 1; i < count; i++)
				rm->allocmap[allocated_gpr + i] = -(count - i);

		}
	}
	if (allocated_gpr == rm->num_gprs)
		allocated_gpr = (-1);
	if (allocated_gpr >= 0)
		rm->num_used += count;
	mtx_unlock(&(rm->gpr_lock));
	return (allocated_gpr);
}

/* mixer */
void
emumix_set_mode(struct emu_sc_info *sc, int mode)
{
	uint32_t a_iocfg;
	uint32_t hcfg;
	uint32_t tmp;

	switch (mode) {
	case MODE_DIGITAL:
		/* FALLTHROUGH */
	case MODE_ANALOG:
		break;
	default:
		return;
	}

	hcfg = EMU_HCFG_AUDIOENABLE | EMU_HCFG_AUTOMUTE;
	a_iocfg = 0;

	if (sc->rev >= 6)
		hcfg |= EMU_HCFG_JOYENABLE;

	if (sc->is_emu10k1)
		hcfg |= EMU_HCFG_LOCKTANKCACHE_MASK;
	else
		hcfg |= EMU_HCFG_CODECFMT_I2S | EMU_HCFG_JOYENABLE;


	if (mode == MODE_DIGITAL) {
		if (sc->broken_digital) {
			device_printf(sc->dev, "Digital mode is reported as broken on this card.\n");
		}
		a_iocfg |= EMU_A_IOCFG_GPOUT1;
		hcfg |= EMU_HCFG_GPOUT0;
	}

	if (mode == MODE_ANALOG)
		emumix_set_spdif_mode(sc, SPDIF_MODE_PCM);

	if (sc->is_emu10k2)
		a_iocfg |= 0x80; /* XXX */

	if ((sc->is_ca0102) || (sc->is_ca0108))
		/*
		 * Setting EMU_A_IOCFG_DISABLE_ANALOG will do opposite things
		 * on diffrerent cards.
		 * "don't disable analog outs" on Audigy 2 (ca0102/ca0108)
		 * "disable analog outs" on Audigy (emu10k2)
		 */
		a_iocfg |= EMU_A_IOCFG_DISABLE_ANALOG;

	if (sc->is_ca0108)
		a_iocfg |= 0x20; /* XXX */

	/* Mute analog center & subwoofer before mode change */
	if (mode == MODE_DIGITAL)
		emumix_set_gpr(sc, sc->mute_gpr[ANALOGMUTE], 1);

	emu_wr(sc, EMU_HCFG, hcfg, 4);

	if ((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108)) {
		tmp = emu_rd(sc, EMU_A_IOCFG, 2);
		tmp = a_iocfg;
		emu_wr(sc, EMU_A_IOCFG, tmp, 2);
	}

	/* Unmute if we have changed mode to analog. */

	if (mode == MODE_ANALOG)
		emumix_set_gpr(sc, sc->mute_gpr[ANALOGMUTE], 0);

	sc->mode = mode;
}

void
emumix_set_spdif_mode(struct emu_sc_info *sc, int mode)
{
	uint32_t spcs;

	switch (mode) {
	case SPDIF_MODE_PCM:
		break;
	case SPDIF_MODE_AC3:
		device_printf(sc->dev, "AC3 mode does not work and disabled\n");
		return;
	default:
		return;
	}

	spcs = EMU_SPCS_CLKACCY_1000PPM | EMU_SPCS_SAMPLERATE_48 |
	    EMU_SPCS_CHANNELNUM_LEFT | EMU_SPCS_SOURCENUM_UNSPEC |
	    EMU_SPCS_GENERATIONSTATUS | 0x00001200 | 0x00000000 |
	    EMU_SPCS_EMPHASIS_NONE | EMU_SPCS_COPYRIGHT;

	mode = SPDIF_MODE_PCM;

	emu_wrptr(sc, 0, EMU_SPCS0, spcs);
	emu_wrptr(sc, 0, EMU_SPCS1, spcs);
	emu_wrptr(sc, 0, EMU_SPCS2, spcs);
}

#define	L2L_POINTS	10

static int l2l_df[L2L_POINTS] = {
	0x572C5CA,		/* 100..90 */
	0x3211625,		/* 90..80 */
	0x1CC1A76,		/* 80..70 */
	0x108428F,		/* 70..60 */
	0x097C70A,		/* 60..50 */
	0x0572C5C,		/* 50..40 */
	0x0321162,		/* 40..30 */
	0x01CC1A7,		/* 30..20 */
	0x0108428,		/* 20..10 */
	0x016493D		/* 10..0 */
};

static int l2l_f[L2L_POINTS] = {
	0x4984461A,		/* 90 */
	0x2A3968A7,		/* 80 */
	0x18406003,		/* 70 */
	0x0DEDC66D,		/* 60 */
	0x07FFFFFF,		/* 50 */
	0x04984461,		/* 40 */
	0x02A3968A,		/* 30 */
	0x01840600,		/* 20 */
	0x00DEDC66,		/* 10 */
	0x00000000		/* 0 */
};


static int
log2lin(int log_t)
{
	int lin_t;
	int idx, lin;

	if (log_t <= 0) {
		lin_t = 0x00000000;
		return (lin_t);
	}

	if (log_t >= 100) {
		lin_t = 0x7fffffff;
		return (lin_t);
	}

	idx = (L2L_POINTS - 1) - log_t / (L2L_POINTS);
	lin = log_t % (L2L_POINTS);
	lin_t = l2l_df[idx] * lin + l2l_f[idx];
	return (lin_t);
}


void
emumix_set_fxvol(struct emu_sc_info *sc, unsigned gpr, int32_t vol)
{

	vol = log2lin(vol);
	emumix_set_gpr(sc, gpr, vol);
}

void
emumix_set_gpr(struct emu_sc_info *sc, unsigned gpr, int32_t val)
{
	if (sc->dbg_level > 1)
		if (gpr == 0) {
			device_printf(sc->dev, "Zero gpr write access\n");
#ifdef KDB
			kdb_backtrace();
#endif
			return;
			}

	emu_wrptr(sc, 0, GPR(gpr), val);
}

void
emumix_set_volume(struct emu_sc_info *sc, int mixer_idx, int volume)
{

	RANGE(volume, 0, 100);
	if (mixer_idx < NUM_MIXERS) {
		sc->mixer_volcache[mixer_idx] = volume;
		emumix_set_fxvol(sc, sc->mixer_gpr[mixer_idx], volume);
	}
}

int
emumix_get_volume(struct emu_sc_info *sc, int mixer_idx)
{
	if ((mixer_idx < NUM_MIXERS) && (mixer_idx >= 0))
		return (sc->mixer_volcache[mixer_idx]);
	return (-1);
}

/* Init CardBus part */
static int
emu_cardbus_init(struct emu_sc_info *sc)
{

	/*
	 * XXX May not need this if we have EMU_IPR3 handler.
	 * Is it a real init calls, or EMU_IPR3 interrupt acknowledgments?
	 * Looks much like "(data << 16) | register".
	 */
	emu_wr_cbptr(sc, (0x00d0 << 16) | 0x0000);
	emu_wr_cbptr(sc, (0x00d0 << 16) | 0x0001);
	emu_wr_cbptr(sc, (0x00d0 << 16) | 0x005f);
	emu_wr_cbptr(sc, (0x00d0 << 16) | 0x007f);

	emu_wr_cbptr(sc, (0x0090 << 16) | 0x007f);

	return (0);
}

/* Probe and attach the card */
static int
emu_init(struct emu_sc_info *sc)
{
	uint32_t ch, tmp;
	uint32_t spdif_sr;
	uint32_t ac97slot;
	int def_mode;
	int i;

	/* disable audio and lock cache */
	emu_wr(sc, EMU_HCFG, EMU_HCFG_LOCKSOUNDCACHE | EMU_HCFG_LOCKTANKCACHE_MASK | EMU_HCFG_MUTEBUTTONENABLE, 4);

	/* reset recording buffers */
	emu_wrptr(sc, 0, EMU_MICBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_MICBA, 0);
	emu_wrptr(sc, 0, EMU_FXBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_FXBA, 0);
	emu_wrptr(sc, 0, EMU_ADCBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_ADCBA, 0);

	/* disable channel interrupt */
	emu_wr(sc, EMU_INTE, EMU_INTE_INTERTIMERENB | EMU_INTE_SAMPLERATER | EMU_INTE_PCIERRENABLE, 4);
	emu_wrptr(sc, 0, EMU_CLIEL, 0);
	emu_wrptr(sc, 0, EMU_CLIEH, 0);
	emu_wrptr(sc, 0, EMU_SOLEL, 0);
	emu_wrptr(sc, 0, EMU_SOLEH, 0);

	/* disable P16V and S/PDIF interrupts */
	if ((sc->is_ca0102) || (sc->is_ca0108))
		emu_wr(sc, EMU_INTE2, 0, 4);

	if (sc->is_ca0102)
		emu_wr(sc, EMU_INTE3, 0, 4);

	/* init phys inputs and outputs */
	ac97slot = 0;
	if (sc->has_51)
		ac97slot = EMU_AC97SLOT_CENTER | EMU_AC97SLOT_LFE;
	if (sc->has_71)
		ac97slot = EMU_AC97SLOT_CENTER | EMU_AC97SLOT_LFE | EMU_AC97SLOT_REAR_LEFT | EMU_AC97SLOT_REAR_RIGHT;
	if (sc->is_emu10k2)
		ac97slot |= 0x40;
	emu_wrptr(sc, 0, EMU_AC97SLOT, ac97slot);

	if (sc->is_emu10k2)	/* XXX for later cards? */
		emu_wrptr(sc, 0, EMU_SPBYPASS, 0xf00);	/* What will happen if
							 * we write 1 here? */

	if (bus_dma_tag_create( /* parent */ bus_get_dma_tag(sc->dev),
	     /* alignment */ 2, /* boundary */ 0,
	     /* lowaddr */ (1U << 31) - 1,	/* can only access 0-2gb */
	     /* highaddr */ BUS_SPACE_MAXADDR,
	     /* filter */ NULL, /* filterarg */ NULL,
	     /* maxsize */ EMU_MAX_BUFSZ, /* nsegments */ 1, /* maxsegz */ 0x3ffff,
	     /* flags */ 0, /* lockfunc */ busdma_lock_mutex,
	     /* lockarg */ &Giant, &(sc->mem.dmat)) != 0) {
		device_printf(sc->dev, "unable to create dma tag\n");
		bus_dma_tag_destroy(sc->mem.dmat);
		return (ENOMEM);
	}

	sc->mem.card = sc;
	SLIST_INIT(&sc->mem.blocks);
	sc->mem.ptb_pages = emu_malloc(&sc->mem, EMU_MAXPAGES * sizeof(uint32_t), &sc->mem.ptb_pages_addr, &sc->mem.ptb_map);
	if (sc->mem.ptb_pages == NULL)
		return (ENOMEM);

	sc->mem.silent_page = emu_malloc(&sc->mem, EMUPAGESIZE, &sc->mem.silent_page_addr, &sc->mem.silent_map);
	if (sc->mem.silent_page == NULL) {
		emu_free(&sc->mem, sc->mem.ptb_pages, sc->mem.ptb_map);
		return (ENOMEM);
	}
	/* Clear page with silence & setup all pointers to this page */
	bzero(sc->mem.silent_page, EMUPAGESIZE);
	tmp = (uint32_t) (sc->mem.silent_page_addr) << 1;
	for (i = 0; i < EMU_MAXPAGES; i++)
		sc->mem.ptb_pages[i] = tmp | i;

	for (ch = 0; ch < NUM_G; ch++) {
		emu_wrptr(sc, ch, EMU_CHAN_MAPA, tmp | EMU_CHAN_MAP_PTI_MASK);
		emu_wrptr(sc, ch, EMU_CHAN_MAPB, tmp | EMU_CHAN_MAP_PTI_MASK);
	}
	emu_wrptr(sc, 0, EMU_PTB, (sc->mem.ptb_pages_addr));
	emu_wrptr(sc, 0, EMU_TCB, 0);	/* taken from original driver */
	emu_wrptr(sc, 0, EMU_TCBS, 0);	/* taken from original driver */

	/* init envelope engine */
	for (ch = 0; ch < NUM_G; ch++) {
		emu_wrptr(sc, ch, EMU_CHAN_DCYSUSV, 0);
		emu_wrptr(sc, ch, EMU_CHAN_IP, 0);
		emu_wrptr(sc, ch, EMU_CHAN_VTFT, 0xffff);
		emu_wrptr(sc, ch, EMU_CHAN_CVCF, 0xffff);
		emu_wrptr(sc, ch, EMU_CHAN_PTRX, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CPF, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CCR, 0);

		emu_wrptr(sc, ch, EMU_CHAN_PSST, 0);
		emu_wrptr(sc, ch, EMU_CHAN_DSL, 0x10);
		emu_wrptr(sc, ch, EMU_CHAN_CCCA, 0);
		emu_wrptr(sc, ch, EMU_CHAN_Z1, 0);
		emu_wrptr(sc, ch, EMU_CHAN_Z2, 0);
		emu_wrptr(sc, ch, EMU_CHAN_FXRT, 0xd01c0000);

		emu_wrptr(sc, ch, EMU_CHAN_ATKHLDM, 0);
		emu_wrptr(sc, ch, EMU_CHAN_DCYSUSM, 0);
		emu_wrptr(sc, ch, EMU_CHAN_IFATN, 0xffff);
		emu_wrptr(sc, ch, EMU_CHAN_PEFE, 0);
		emu_wrptr(sc, ch, EMU_CHAN_FMMOD, 0);
		emu_wrptr(sc, ch, EMU_CHAN_TREMFRQ, 24);	/* 1 Hz */
		emu_wrptr(sc, ch, EMU_CHAN_FM2FRQ2, 24);	/* 1 Hz */
		emu_wrptr(sc, ch, EMU_CHAN_TEMPENV, 0);

		/*** these are last so OFF prevents writing ***/
		emu_wrptr(sc, ch, EMU_CHAN_LFOVAL2, 0);
		emu_wrptr(sc, ch, EMU_CHAN_LFOVAL1, 0);
		emu_wrptr(sc, ch, EMU_CHAN_ATKHLDV, 0);
		emu_wrptr(sc, ch, EMU_CHAN_ENVVOL, 0);
		emu_wrptr(sc, ch, EMU_CHAN_ENVVAL, 0);

		if ((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108)) {
			emu_wrptr(sc, ch, 0x4c, 0x0);
			emu_wrptr(sc, ch, 0x4d, 0x0);
			emu_wrptr(sc, ch, 0x4e, 0x0);
			emu_wrptr(sc, ch, 0x4f, 0x0);
			emu_wrptr(sc, ch, EMU_A_CHAN_FXRT1, 0x3f3f3f3f);
			emu_wrptr(sc, ch, EMU_A_CHAN_FXRT2, 0x3f3f3f3f);
			emu_wrptr(sc, ch, EMU_A_CHAN_SENDAMOUNTS, 0x0);
		}
	}

	emumix_set_spdif_mode(sc, SPDIF_MODE_PCM);

	if ((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108))
		emu_wrptr(sc, 0, EMU_A_SPDIF_SAMPLERATE, EMU_A_SPDIF_48000);

	/*
	 * CAxxxx cards needs additional setup:
	 * 1. Set I2S capture sample rate to 96000
	 * 2. Disable P16v / P17v proceesing
	 * 3. Allow EMU10K DSP inputs
	 */
	if ((sc->is_ca0102) || (sc->is_ca0108)) {

		spdif_sr = emu_rdptr(sc, 0, EMU_A_SPDIF_SAMPLERATE);
		spdif_sr &= 0xfffff1ff;
		spdif_sr |= EMU_A_I2S_CAPTURE_96000;
		emu_wrptr(sc, 0, EMU_A_SPDIF_SAMPLERATE, spdif_sr);

		/* Disable P16v processing */
		emu_wr_p16vptr(sc, 0, EMU_A2_SRCSel, 0x14);

		/* Setup P16v/P17v sound routing */
		if (sc->is_ca0102)
			emu_wr_p16vptr(sc, 0, EMU_A2_SRCMULTI_ENABLE, 0xFF00FF00);
		else {
			emu_wr_p16vptr(sc, 0, EMU_A2_MIXER_I2S_ENABLE, 0xFF000000);
			emu_wr_p16vptr(sc, 0, EMU_A2_MIXER_SPDIF_ENABLE, 0xFF000000);

			tmp = emu_rd(sc, EMU_A_IOCFG, 2);
			emu_wr(sc, EMU_A_IOCFG, tmp & ~0x8, 2);
		}
	}
	emu_initefx(sc);

	def_mode = MODE_ANALOG;
	if ((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108))
		def_mode = MODE_DIGITAL;
	if (((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108)) && (sc->broken_digital)) {
		device_printf(sc->dev, "Audigy card initialized in analog mode.\n");
		def_mode = MODE_ANALOG;
	}
	emumix_set_mode(sc, def_mode);

	if (bootverbose) {
		tmp = emu_rd(sc, EMU_HCFG, 4);
		device_printf(sc->dev, "Card Configuration (   0x%08x )\n", tmp);
		device_printf(sc->dev, "Card Configuration ( & 0xff000000 ) : %s%s%s%s%s%s%s%s\n",
		    (tmp & 0x80000000 ? "[Legacy MPIC] " : ""),
		    (tmp & 0x40000000 ? "[0x40] " : ""),
		    (tmp & 0x20000000 ? "[0x20] " : ""),
		    (tmp & 0x10000000 ? "[0x10] " : ""),
		    (tmp & 0x08000000 ? "[0x08] " : ""),
		    (tmp & 0x04000000 ? "[0x04] " : ""),
		    (tmp & 0x02000000 ? "[0x02] " : ""),
		    (tmp & 0x01000000 ? "[0x01]" : " "));
		device_printf(sc->dev, "Card Configuration ( & 0x00ff0000 ) : %s%s%s%s%s%s%s%s\n",
		    (tmp & 0x00800000 ? "[0x80] " : ""),
		    (tmp & 0x00400000 ? "[0x40] " : ""),
		    (tmp & 0x00200000 ? "[Legacy INT] " : ""),
		    (tmp & 0x00100000 ? "[0x10] " : ""),
		    (tmp & 0x00080000 ? "[0x08] " : ""),
		    (tmp & 0x00040000 ? "[Codec4] " : ""),
		    (tmp & 0x00020000 ? "[Codec2] " : ""),
		    (tmp & 0x00010000 ? "[I2S Codec]" : " "));
		device_printf(sc->dev, "Card Configuration ( & 0x0000ff00 ) : %s%s%s%s%s%s%s%s\n",
		    (tmp & 0x00008000 ? "[0x80] " : ""),
		    (tmp & 0x00004000 ? "[GPINPUT0] " : ""),
		    (tmp & 0x00002000 ? "[GPINPUT1] " : ""),
		    (tmp & 0x00001000 ? "[GPOUT0] " : ""),
		    (tmp & 0x00000800 ? "[GPOUT1] " : ""),
		    (tmp & 0x00000400 ? "[GPOUT2] " : ""),
		    (tmp & 0x00000200 ? "[Joystick] " : ""),
		    (tmp & 0x00000100 ? "[0x01]" : " "));
		device_printf(sc->dev, "Card Configuration ( & 0x000000ff ) : %s%s%s%s%s%s%s%s\n",
		    (tmp & 0x00000080 ? "[0x80] " : ""),
		    (tmp & 0x00000040 ? "[0x40] " : ""),
		    (tmp & 0x00000020 ? "[0x20] " : ""),
		    (tmp & 0x00000010 ? "[AUTOMUTE] " : ""),
		    (tmp & 0x00000008 ? "[LOCKSOUNDCACHE] " : ""),
		    (tmp & 0x00000004 ? "[LOCKTANKCACHE] " : ""),
		    (tmp & 0x00000002 ? "[MUTEBUTTONENABLE] " : ""),
		    (tmp & 0x00000001 ? "[AUDIOENABLE]" : " "));

		if ((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108)) {
			tmp = emu_rd(sc, EMU_A_IOCFG, 2);
			device_printf(sc->dev, "Audigy Card Configuration (    0x%04x )\n", tmp);
			device_printf(sc->dev, "Audigy Card Configuration (  & 0xff00 )");
			printf(" : %s%s%s%s%s%s%s%s\n",
			    (tmp & 0x8000 ? "[Rear Speakers] " : ""),
			    (tmp & 0x4000 ? "[Front Speakers] " : ""),
			    (tmp & 0x2000 ? "[0x20] " : ""),
			    (tmp & 0x1000 ? "[0x10] " : ""),
			    (tmp & 0x0800 ? "[0x08] " : ""),
			    (tmp & 0x0400 ? "[0x04] " : ""),
			    (tmp & 0x0200 ? "[0x02] " : ""),
			    (tmp & 0x0100 ? "[AudigyDrive Phones]" : " "));
			device_printf(sc->dev, "Audigy Card Configuration (  & 0x00ff )");
			printf(" : %s%s%s%s%s%s%s%s\n",
			    (tmp & 0x0080 ? "[0x80] " : ""),
			    (tmp & 0x0040 ? "[Mute AnalogOut] " : ""),
			    (tmp & 0x0020 ? "[0x20] " : ""),
			    (tmp & 0x0010 ? "[0x10] " : ""),
			    (tmp & 0x0008 ? "[0x08] " : ""),
			    (tmp & 0x0004 ? "[GPOUT0] " : ""),
			    (tmp & 0x0002 ? "[GPOUT1] " : ""),
			    (tmp & 0x0001 ? "[GPOUT2]" : " "));
		}		/* is_emu10k2 or ca* */
	}			/* bootverbose */
	return (0);
}

static int
emu_uninit(struct emu_sc_info *sc)
{
	uint32_t ch;
	struct emu_memblk *blk;

	emu_wr(sc, EMU_INTE, 0, 4);
	for (ch = 0; ch < NUM_G; ch++)
		emu_wrptr(sc, ch, EMU_CHAN_DCYSUSV, 0);
	for (ch = 0; ch < NUM_G; ch++) {
		emu_wrptr(sc, ch, EMU_CHAN_VTFT, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CVCF, 0);
		emu_wrptr(sc, ch, EMU_CHAN_PTRX, 0);
		emu_wrptr(sc, ch, EMU_CHAN_CPF, 0);
	}

	/* disable audio and lock cache */
	emu_wr(sc, EMU_HCFG, EMU_HCFG_LOCKSOUNDCACHE | EMU_HCFG_LOCKTANKCACHE_MASK | EMU_HCFG_MUTEBUTTONENABLE, 4);

	emu_wrptr(sc, 0, EMU_PTB, 0);
	/* reset recording buffers */
	emu_wrptr(sc, 0, EMU_MICBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_MICBA, 0);
	emu_wrptr(sc, 0, EMU_FXBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_FXBA, 0);
	emu_wrptr(sc, 0, EMU_FXWC, 0);
	emu_wrptr(sc, 0, EMU_ADCBS, EMU_RECBS_BUFSIZE_NONE);
	emu_wrptr(sc, 0, EMU_ADCBA, 0);
	emu_wrptr(sc, 0, EMU_TCB, 0);
	emu_wrptr(sc, 0, EMU_TCBS, 0);

	/* disable channel interrupt */
	emu_wrptr(sc, 0, EMU_CLIEL, 0);
	emu_wrptr(sc, 0, EMU_CLIEH, 0);
	emu_wrptr(sc, 0, EMU_SOLEL, 0);
	emu_wrptr(sc, 0, EMU_SOLEH, 0);

	if (!SLIST_EMPTY(&sc->mem.blocks))
		device_printf(sc->dev, "warning: memblock list not empty\n");

	SLIST_FOREACH(blk, &sc->mem.blocks, link)
		if (blk != NULL)
		device_printf(sc->dev, "lost %d for %s\n", blk->pte_size, blk->owner);

	emu_free(&sc->mem, sc->mem.ptb_pages, sc->mem.ptb_map);
	emu_free(&sc->mem, sc->mem.silent_page, sc->mem.silent_map);

	return (0);
}

static int
emu_read_ivar(device_t bus, device_t dev, int ivar_index, uintptr_t * result)
{
	struct sndcard_func *func = device_get_ivars(dev);
	struct emu_sc_info *sc = device_get_softc(bus);

	if (func==NULL)
		return (ENOMEM);
	if (sc == NULL)
		return (ENOMEM);

	switch (ivar_index) {
	case EMU_VAR_FUNC:
		*result = func->func;
		break;
	case EMU_VAR_ROUTE:
		if (func->varinfo == NULL)
			return (ENOMEM);
		*result = ((struct emu_pcminfo *)func->varinfo)->route;
		break;
	case EMU_VAR_ISEMU10K1:
		*result = sc->is_emu10k1;
		break;
	case EMU_VAR_MCH_DISABLED:
		*result = sc->mch_disabled;
		break;
	case EMU_VAR_MCH_REC:
		*result = sc->mch_rec;
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

static int
emu_write_ivar(device_t bus __unused, device_t dev __unused,
    int ivar_index, uintptr_t value __unused)
{

	switch (ivar_index) {
		case 0:
		return (EINVAL);

	default:
		return (ENOENT);
	}
}

static int
emu_pci_probe(device_t dev)
{
	struct sbuf *s;
	unsigned int thiscard = 0;
	uint16_t vendor;

	vendor = pci_read_config(dev, PCIR_DEVVENDOR, /* bytes */ 2);
	if (vendor != 0x1102)
		return (ENXIO);	/* Not Creative */

	thiscard = emu_getcard(dev);
	if (thiscard == 0)
		return (ENXIO);

	s = sbuf_new(NULL, NULL, 4096, 0);
	if (s == NULL)
		return (ENOMEM);
	sbuf_printf(s, "Creative %s [%s]", emu_cards[thiscard].desc, emu_cards[thiscard].SBcode);
	sbuf_finish(s);

	device_set_desc_copy(dev, sbuf_data(s));

	sbuf_delete(s);

	return (BUS_PROBE_DEFAULT);
}


static int
emu_pci_attach(device_t dev)
{
	struct sndcard_func *func;
	struct emu_sc_info *sc;
	struct emu_pcminfo *pcminfo;
#if 0
	struct emu_midiinfo *midiinfo;
#endif
	int i;
	int device_flags;
	char status[255];
	int error = ENXIO;
	int unit;

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);

	/* Get configuration */

	sc->ctx = device_get_sysctl_ctx(dev);
	if (sc->ctx == NULL)
		goto bad;
	sc->root = device_get_sysctl_tree(dev);
	if (sc->root == NULL)
		goto bad;

	if (resource_int_value("emu10kx", unit, "multichannel_disabled", &(sc->mch_disabled)))
		RANGE(sc->mch_disabled, 0, 1);
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "multichannel_disabled", CTLFLAG_RD, &(sc->mch_disabled), 0, "Multichannel playback setting");

	if (resource_int_value("emu10kx", unit, "multichannel_recording", &(sc->mch_rec)))
		RANGE(sc->mch_rec, 0, 1);
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "multichannel_recording", CTLFLAG_RD,  &(sc->mch_rec), 0, "Multichannel recording setting");

	if (resource_int_value("emu10kx", unit, "debug", &(sc->dbg_level)))
		RANGE(sc->mch_rec, 0, 2);
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "debug", CTLFLAG_RW, &(sc->dbg_level), 0, "Debug level");

	/* Fill in the softc. */
	mtx_init(&sc->lock, device_get_nameunit(dev), "bridge conf", MTX_DEF);
	mtx_init(&sc->rw, device_get_nameunit(dev), "exclusive io", MTX_DEF);
	sc->dev = dev;
	sc->type = pci_get_devid(dev);
	sc->rev = pci_get_revid(dev);
	sc->enable_ir = 0;
	sc->has_ac97 = 0;
	sc->has_51 = 0;
	sc->has_71 = 0;
	sc->broken_digital = 0;
	sc->is_emu10k1 = 0;
	sc->is_emu10k2 = 0;
	sc->is_ca0102 = 0;
	sc->is_ca0108 = 0;
	sc->is_cardbus = 0;

	device_flags = emu_cards[emu_getcard(dev)].flags;
	if (device_flags & HAS_51)
		sc->has_51 = 1;
	if (device_flags & HAS_71) {
		sc->has_51 = 1;
		sc->has_71 = 1;
	}
	if (device_flags & IS_EMU10K1)
		sc->is_emu10k1 = 1;
	if (device_flags & IS_EMU10K2)
		sc->is_emu10k2 = 1;
	if (device_flags & IS_CA0102)
		sc->is_ca0102 = 1;
	if (device_flags & IS_CA0108)
		sc->is_ca0108 = 1;
	if ((sc->is_emu10k2) && (sc->rev == 4)) {
		sc->is_emu10k2 = 0;
		sc->is_ca0102 = 1;	/* for unknown Audigy 2 cards */
	}
	if ((sc->is_ca0102 == 1) || (sc->is_ca0108 == 1))
		if (device_flags & IS_CARDBUS)
			sc->is_cardbus = 1;

	if ((sc->is_emu10k1 + sc->is_emu10k2 + sc->is_ca0102 + sc->is_ca0108) != 1) {
		device_printf(sc->dev, "Unable to detect HW chipset\n");
		goto bad;
	}
	if (device_flags & BROKEN_DIGITAL)
		sc->broken_digital = 1;
	if (device_flags & HAS_AC97)
		sc->has_ac97 = 1;

	sc->opcode_shift = 0;
	if ((sc->is_emu10k2) || (sc->is_ca0102) || (sc->is_ca0108)) {
		sc->opcode_shift = 24;
		sc->high_operand_shift = 12;

	/*	DSP map				*/
	/*	sc->fx_base = 0x0		*/
		sc->input_base = 0x40;
	/*	sc->p16vinput_base = 0x50;	*/
		sc->output_base = 0x60;
		sc->efxc_base = 0x80;
	/*	sc->output32h_base = 0xa0;	*/
	/*	sc->output32l_base = 0xb0;	*/
		sc->dsp_zero = 0xc0;
	/*	0xe0...0x100 are unknown	*/
	/*	sc->tram_base = 0x200		*/
	/*	sc->tram_addr_base = 0x300	*/
		sc->gpr_base = EMU_A_FXGPREGBASE;
		sc->num_gprs = 0x200;
		sc->code_base = EMU_A_MICROCODEBASE;
		sc->code_size = 0x800 / 2;	/* 0x600-0xdff,  2048 words,
						 * 1024 instructions */

		sc->mchannel_fx = 8;
		sc->num_fxbuses = 16;
		sc->num_inputs = 8;
		sc->num_outputs = 16;
		sc->address_mask = EMU_A_PTR_ADDR_MASK;
	}
	if (sc->is_emu10k1) {
		sc->has_51 = 0;	/* We don't support 5.1 sound on SB Live! 5.1 */
		sc->opcode_shift = 20;
		sc->high_operand_shift = 10;
		sc->code_base = EMU_MICROCODEBASE;
		sc->code_size = 0x400 / 2;	/* 0x400-0x7ff,  1024 words,
						 * 512 instructions */
		sc->gpr_base = EMU_FXGPREGBASE;
		sc->num_gprs = 0x100;
		sc->input_base = 0x10;
		sc->output_base = 0x20;
		/*
		 * XXX 5.1 Analog outputs are inside efxc address space!
		 * They use output+0x11/+0x12 (=efxc+1/+2).
		 * Don't use this efx registers for recording on SB Live! 5.1!
		 */
		sc->efxc_base = 0x30;
		sc->dsp_zero = 0x40;
		sc->mchannel_fx = 0;
		sc->num_fxbuses = 8;
		sc->num_inputs = 8;
		sc->num_outputs = 16;
		sc->address_mask = EMU_PTR_ADDR_MASK;
	}
	if (sc->opcode_shift == 0)
		goto bad;

	pci_enable_busmaster(dev);

	i = PCIR_BAR(0);
	sc->reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &i, RF_ACTIVE);
	if (sc->reg == NULL) {
		device_printf(dev, "unable to map register space\n");
		goto bad;
	}
	sc->st = rman_get_bustag(sc->reg);
	sc->sh = rman_get_bushandle(sc->reg);

	for (i = 0; i < EMU_MAX_IRQ_CONSUMERS; i++)
		sc->timer[i] = 0;	/* disable it */

	i = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &i, RF_ACTIVE | RF_SHAREABLE);
	if ((sc->irq == NULL) || bus_setup_intr(dev, sc->irq, INTR_MPSAFE | INTR_TYPE_AV,
	    NULL,
	    emu_intr, sc, &sc->ih)) {
		device_printf(dev, "unable to map interrupt\n");
		goto bad;
	}
	if (emu_rm_init(sc) != 0) {
		device_printf(dev, "unable to create resource manager\n");
		goto bad;
	}
	if (sc->is_cardbus)
		if (emu_cardbus_init(sc) != 0) {
			device_printf(dev, "unable to initialize CardBus interface\n");
			goto bad;
		}
	if (emu_init(sc) != 0) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}
	if (emu10kx_dev_init(sc) != 0) {
		device_printf(dev, "unable to create control device\n");
		goto bad;
	}
	snprintf(status, 255, "rev %d at io 0x%jx irq %jd", sc->rev, rman_get_start(sc->reg), rman_get_start(sc->irq));

	/* Voices */
	for (i = 0; i < NUM_G; i++) {
		sc->voice[i].vnum = i;
		sc->voice[i].slave = NULL;
		sc->voice[i].busy = 0;
		sc->voice[i].ismaster = 0;
		sc->voice[i].running = 0;
		sc->voice[i].b16 = 0;
		sc->voice[i].stereo = 0;
		sc->voice[i].speed = 0;
		sc->voice[i].start = 0;
		sc->voice[i].end = 0;
	}

	/* PCM Audio */
	for (i = 0; i < RT_COUNT; i++)
		sc->pcm[i] = NULL;

	/* FRONT */
	func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (func == NULL) {
		error = ENOMEM;
		goto bad;
	}
	pcminfo = malloc(sizeof(struct emu_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pcminfo == NULL) {
		error = ENOMEM;
		goto bad;
	}
	pcminfo->card = sc;
	pcminfo->route = RT_FRONT;

	func->func = SCF_PCM;
	func->varinfo = pcminfo;
	sc->pcm[RT_FRONT] = device_add_child(dev, "pcm", -1);
	device_set_ivars(sc->pcm[RT_FRONT], func);

	if (!(sc->mch_disabled)) {
		/* REAR */
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL) {
			error = ENOMEM;
			goto bad;
		}
		pcminfo = malloc(sizeof(struct emu_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (pcminfo == NULL) {
			error = ENOMEM;
			goto bad;
		}
		pcminfo->card = sc;
		pcminfo->route = RT_REAR;

		func->func = SCF_PCM;
		func->varinfo = pcminfo;
		sc->pcm[RT_REAR] = device_add_child(dev, "pcm", -1);
		device_set_ivars(sc->pcm[RT_REAR], func);
		if (sc->has_51) {
			/* CENTER */
			func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (func == NULL) {
				error = ENOMEM;
				goto bad;
			}
			pcminfo = malloc(sizeof(struct emu_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (pcminfo == NULL) {
				error = ENOMEM;
				goto bad;
			}
			pcminfo->card = sc;
			pcminfo->route = RT_CENTER;

			func->func = SCF_PCM;
			func->varinfo = pcminfo;
			sc->pcm[RT_CENTER] = device_add_child(dev, "pcm", -1);
			device_set_ivars(sc->pcm[RT_CENTER], func);
			/* SUB */
			func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (func == NULL) {
				error = ENOMEM;
				goto bad;
			}
			pcminfo = malloc(sizeof(struct emu_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (pcminfo == NULL) {
				error = ENOMEM;
				goto bad;
			}
			pcminfo->card = sc;
			pcminfo->route = RT_SUB;

			func->func = SCF_PCM;
			func->varinfo = pcminfo;
			sc->pcm[RT_SUB] = device_add_child(dev, "pcm", -1);
			device_set_ivars(sc->pcm[RT_SUB], func);
		}
		if (sc->has_71) {
			/* SIDE */
			func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (func == NULL) {
				error = ENOMEM;
				goto bad;
			}
			pcminfo = malloc(sizeof(struct emu_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
			if (pcminfo == NULL) {
				error = ENOMEM;
				goto bad;
			}
			pcminfo->card = sc;
			pcminfo->route = RT_SIDE;

			func->func = SCF_PCM;
			func->varinfo = pcminfo;
			sc->pcm[RT_SIDE] = device_add_child(dev, "pcm", -1);
			device_set_ivars(sc->pcm[RT_SIDE], func);
		}
	} /* mch_disabled */

	if (sc->mch_rec) {
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL) {
			error = ENOMEM;
			goto bad;
		}
		pcminfo = malloc(sizeof(struct emu_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (pcminfo == NULL) {
			error = ENOMEM;
			goto bad;
		}
		pcminfo->card = sc;
		pcminfo->route = RT_MCHRECORD;

		func->func = SCF_PCM;
		func->varinfo = pcminfo;
		sc->pcm[RT_MCHRECORD] = device_add_child(dev, "pcm", -1);
		device_set_ivars(sc->pcm[RT_MCHRECORD], func);
	} /*mch_rec */

	for (i = 0; i < 2; i++)
		sc->midi[i] = NULL;

	/* MIDI has some memory mangament and (possible) locking problems */
#if 0
	/* Midi Interface 1: Live!, Audigy, Audigy 2 */
	if ((sc->is_emu10k1) || (sc->is_emu10k2) || (sc->is_ca0102)) {
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL) {
			error = ENOMEM;
			goto bad;
		}
		midiinfo = malloc(sizeof(struct emu_midiinfo), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (midiinfo == NULL) {
			error = ENOMEM;
			goto bad;
		}
		midiinfo->card = sc;
		if (sc->is_emu10k2 || (sc->is_ca0102)) {
			midiinfo->port = EMU_A_MUDATA1;
			midiinfo->portnr = 1;
		}
		if (sc->is_emu10k1) {
			midiinfo->port = MUDATA;
			midiinfo->portnr = 1;
		}
		func->func = SCF_MIDI;
		func->varinfo = midiinfo;
		sc->midi[0] = device_add_child(dev, "midi", -1);
		device_set_ivars(sc->midi[0], func);
	}
	/* Midi Interface 2: Audigy, Audigy 2 (on AudigyDrive) */
	if (sc->is_emu10k2 || (sc->is_ca0102)) {
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL) {
			error = ENOMEM;
			goto bad;
		}
		midiinfo = malloc(sizeof(struct emu_midiinfo), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (midiinfo == NULL) {
			error = ENOMEM;
			goto bad;
		}
		midiinfo->card = sc;

		midiinfo->port = EMU_A_MUDATA2;
		midiinfo->portnr = 2;

		func->func = SCF_MIDI;
		func->varinfo = midiinfo;
		sc->midi[1] = device_add_child(dev, "midi", -1);
		device_set_ivars(sc->midi[1], func);
	}
#endif
	return (bus_generic_attach(dev));

bad:
	/* XXX can we just call emu_pci_detach here? */
	if (sc->cdev)
		emu10kx_dev_uninit(sc);
	if (sc->rm != NULL)
		emu_rm_uninit(sc);
	if (sc->reg)
		bus_release_resource(dev, SYS_RES_IOPORT, PCIR_BAR(0), sc->reg);
	if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	mtx_destroy(&sc->rw);
	mtx_destroy(&sc->lock);
	return (error);
}

static int
emu_pci_detach(device_t dev)
{
	struct emu_sc_info *sc;
	struct sndcard_func *func;
	int devcount, i;
	device_t *childlist;
	int r = 0;

	sc = device_get_softc(dev);
	
	for (i = 0; i < RT_COUNT; i++) {
		if (sc->pcm[i] != NULL) {
			func = device_get_ivars(sc->pcm[i]);
			if (func != NULL && func->func == SCF_PCM) {
				device_set_ivars(sc->pcm[i], NULL);
				free(func->varinfo, M_DEVBUF);
				free(func, M_DEVBUF);
			}
			r = device_delete_child(dev, sc->pcm[i]);
			if (r)	return (r);
		}
	}

	if (sc->midi[0] != NULL) {
		func = device_get_ivars(sc->midi[0]);
		if (func != NULL && func->func == SCF_MIDI) {
			device_set_ivars(sc->midi[0], NULL);
			free(func->varinfo, M_DEVBUF);
			free(func, M_DEVBUF);
		}
		r = device_delete_child(dev, sc->midi[0]);
		if (r)	return (r);
	}

	if (sc->midi[1] != NULL) {
		func = device_get_ivars(sc->midi[1]);
		if (func != NULL && func->func == SCF_MIDI) {
			device_set_ivars(sc->midi[1], NULL);
			free(func->varinfo, M_DEVBUF);
			free(func, M_DEVBUF);
		}
		r = device_delete_child(dev, sc->midi[1]);
		if (r)	return (r);
	}

	if (device_get_children(dev, &childlist, &devcount) == 0)
		for (i = 0; i < devcount - 1; i++) {
			device_printf(dev, "removing stale child %d (unit %d)\n", i, device_get_unit(childlist[i]));
			func = device_get_ivars(childlist[i]);
			if (func != NULL && (func->func == SCF_MIDI || func->func == SCF_PCM)) {
				device_set_ivars(childlist[i], NULL);
				free(func->varinfo, M_DEVBUF);
				free(func, M_DEVBUF);
			}
			device_delete_child(dev, childlist[i]);
		}
	if (childlist != NULL)
		free(childlist, M_TEMP);

	r = emu10kx_dev_uninit(sc);
	if (r)
		return (r);

	/* shutdown chip */
	emu_uninit(sc);
	emu_rm_uninit(sc);

	if (sc->mem.dmat)
		bus_dma_tag_destroy(sc->mem.dmat);

	if (sc->reg)
		bus_release_resource(dev, SYS_RES_IOPORT, PCIR_BAR(0), sc->reg);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	mtx_destroy(&sc->rw);
	mtx_destroy(&sc->lock);

	return (bus_generic_detach(dev));
}
/* add suspend, resume */
static device_method_t emu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, emu_pci_probe),
	DEVMETHOD(device_attach, emu_pci_attach),
	DEVMETHOD(device_detach, emu_pci_detach),
	/* Bus methods */
	DEVMETHOD(bus_read_ivar, emu_read_ivar),
	DEVMETHOD(bus_write_ivar, emu_write_ivar),

	DEVMETHOD_END
};


static driver_t emu_driver = {
	"emu10kx",
	emu_methods,
	sizeof(struct emu_sc_info),
	NULL,
	0,
	NULL
};

static int
emu_modevent(module_t mod __unused, int cmd, void *data __unused)
{
	int err = 0;

	switch (cmd) {
	case MOD_LOAD:
		break;		/* Success */

	case MOD_UNLOAD:
	case MOD_SHUTDOWN:

		/* XXX  Should we check state of pcm & midi subdevices here? */

		break;		/* Success */

	default:
		err = EINVAL;
		break;
	}

	return (err);

}

static devclass_t emu_devclass;

DRIVER_MODULE(snd_emu10kx, pci, emu_driver, emu_devclass, emu_modevent, NULL);
MODULE_VERSION(snd_emu10kx, SND_EMU10KX_PREFVER);
