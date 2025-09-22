/*	$OpenBSD: sdmmcvar.h,v 1.34 2020/08/14 14:49:04 kettenis Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SDMMCVAR_H_
#define _SDMMCVAR_H_

#include <sys/queue.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcreg.h>

struct sdmmc_csd {
	int	csdver;		/* CSD structure format */
	int	mmcver;		/* MMC version (for CID format) */
	int	capacity;	/* total number of sectors */
	int	sector_size;	/* sector size in bytes */
	int	read_bl_len;	/* block length for reads */
	int	tran_speed;	/* transfer speed (kbit/s) */
	int	ccc;		/* Card Command Class for SD */
	/* ... */
};

struct sdmmc_cid {
	int	mid;		/* manufacturer identification number */
	int	oid;		/* OEM/product identification number */
	char	pnm[8];		/* product name (MMC v1 has the longest) */
	int	rev;		/* product revision */
	int	psn;		/* product serial number */
	int	mdt;		/* manufacturing date */
};

struct sdmmc_scr {
	int	sd_spec;
	int	bus_width;
};

typedef u_int32_t sdmmc_response[4];

struct sdmmc_softc;

struct sdmmc_task {
	void (*func)(void *arg);
	void *arg;
	int onqueue;
	struct sdmmc_softc *sc;
	TAILQ_ENTRY(sdmmc_task) next;
};

#define	sdmmc_init_task(xtask, xfunc, xarg) do {			\
	(xtask)->func = (xfunc);					\
	(xtask)->arg = (xarg);						\
	(xtask)->onqueue = 0;						\
	(xtask)->sc = NULL;						\
} while (0)

#define sdmmc_task_pending(xtask) ((xtask)->onqueue)

struct sdmmc_command {
	struct sdmmc_task c_task;	/* task queue entry */
	u_int16_t	 c_opcode;	/* SD or MMC command index */
	u_int32_t	 c_arg;		/* SD/MMC command argument */
	sdmmc_response	 c_resp;	/* response buffer */
	bus_dmamap_t	 c_dmamap;
	void		*c_data;	/* buffer to send or read into */
	int		 c_datalen;	/* length of data buffer */
	int		 c_blklen;	/* block length */
	int		 c_flags;	/* see below */
#define SCF_ITSDONE	 0x0001		/* command is complete */
#define SCF_CMD(flags)	 ((flags) & 0x00f0)
#define SCF_CMD_AC	 0x0000
#define SCF_CMD_ADTC	 0x0010
#define SCF_CMD_BC	 0x0020
#define SCF_CMD_BCR	 0x0030
#define SCF_CMD_READ	 0x0040		/* read command (data expected) */
#define SCF_RSP_BSY	 0x0100
#define SCF_RSP_136	 0x0200
#define SCF_RSP_CRC	 0x0400
#define SCF_RSP_IDX	 0x0800
#define SCF_RSP_PRESENT	 0x1000
/* response types */
#define SCF_RSP_R0	 0 /* none */
#define SCF_RSP_R1	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
#define SCF_RSP_R1B	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX|SCF_RSP_BSY)
#define SCF_RSP_R2	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_136)
#define SCF_RSP_R3	 (SCF_RSP_PRESENT)
#define SCF_RSP_R4	 (SCF_RSP_PRESENT)
#define SCF_RSP_R5	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
#define SCF_RSP_R5B	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX|SCF_RSP_BSY)
#define SCF_RSP_R6	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
#define SCF_RSP_R7	 (SCF_RSP_PRESENT|SCF_RSP_CRC|SCF_RSP_IDX)
	int		 c_error;	/* errno value on completion */

	/* Host controller owned fields for data xfer in progress */
	int c_resid;			/* remaining I/O */
	u_char *c_buf;			/* remaining data */
};

/*
 * Decoded PC Card 16 based Card Information Structure (CIS),
 * per card (function 0) and per function (1 and greater).
 */
struct sdmmc_cis {
	u_int16_t	 manufacturer;
#define SDMMC_VENDOR_INVALID	0xffff
	u_int16_t	 product;
#define SDMMC_PRODUCT_INVALID	0xffff
	u_int8_t	 function;
#define SDMMC_FUNCTION_INVALID	0xff
	u_char		 cis1_major;
	u_char		 cis1_minor;
	char		 cis1_info_buf[256];
	char		*cis1_info[4];
};

/*
 * Structure describing either an SD card I/O function or a SD/MMC
 * memory card from a "stack of cards" that responded to CMD2.  For a
 * combo card with one I/O function and one memory card, there will be
 * two of these structures allocated.  Each card slot has such a list
 * of sdmmc_function structures.
 */
struct sdmmc_function {
	/* common members */
	struct sdmmc_softc *sc;		/* card slot softc */
	u_int16_t rca;			/* relative card address */
	int flags;
#define SFF_ERROR		0x0001	/* function is poo; ignore it */
#define SFF_SDHC		0x0002	/* SD High Capacity card */
	void *cookie;			/* pass extra info from bus to dev */
	SIMPLEQ_ENTRY(sdmmc_function) sf_list;
	/* SD card I/O function members */
	int number;			/* I/O function number or -1 */
	struct device *child;		/* function driver */
	struct sdmmc_cis cis;		/* decoded CIS */
	unsigned int cur_blklen;	/* current block length */
	/* SD/MMC memory card members */
	struct sdmmc_csd csd;		/* decoded CSD value */
	struct sdmmc_cid cid;		/* decoded CID value */
	sdmmc_response raw_cid;		/* temp. storage for decoding */
	struct sdmmc_scr scr;		/* decoded SCR value */
};

/*
 * Structure describing a single SD/MMC/SDIO card slot.
 */
struct sdmmc_softc {
	struct device sc_dev;		/* base device */
#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)
	sdmmc_chipset_tag_t sct;	/* host controller chipset tag */
	sdmmc_chipset_handle_t sch;	/* host controller chipset handle */

	bus_dma_tag_t sc_dmat;
	bus_dmamap_t sc_dmap;
#define SDMMC_MAXNSEGS	((MAXPHYS / PAGE_SIZE) + 1)

	int sc_flags;
#define SMF_SD_MODE		0x0001	/* host in SD mode (MMC otherwise) */
#define SMF_IO_MODE		0x0002	/* host in I/O mode (SD mode only) */
#define SMF_MEM_MODE		0x0004	/* host in memory mode (SD or MMC) */
#define SMF_UHS_MODE		0x0010	/* host in UHS mode */
#define SMF_CARD_PRESENT	0x0020	/* card presence noticed */
#define SMF_CARD_ATTACHED	0x0040	/* card driver(s) attached */
#define SMF_STOP_AFTER_MULTIPLE	0x0080	/* send a stop after a multiple cmd */
#define SMF_CONFIG_PENDING	0x0100	/* config_pending_incr() called */

	uint32_t sc_caps;		/* host capability */
#define SMC_CAPS_AUTO_STOP	0x0001	/* send CMD12 automagically by host */
#define SMC_CAPS_4BIT_MODE	0x0002	/* 4-bits data bus width */
#define SMC_CAPS_DMA		0x0004	/* DMA transfer */
#define SMC_CAPS_SPI_MODE	0x0008	/* SPI mode */
#define SMC_CAPS_POLL_CARD_DET	0x0010	/* Polling card detect */
#define SMC_CAPS_SINGLE_ONLY	0x0020	/* only single read/write */
#define SMC_CAPS_8BIT_MODE	0x0040	/* 8-bits data bus width */
#define SMC_CAPS_MULTI_SEG_DMA	0x0080	/* multiple segment DMA transfer */
#define SMC_CAPS_SD_HIGHSPEED	0x0100	/* SD high-speed timing */
#define SMC_CAPS_MMC_HIGHSPEED	0x0200	/* MMC high-speed timing */
#define SMC_CAPS_UHS_SDR50	0x0400	/* UHS SDR50 timing */
#define SMC_CAPS_UHS_SDR104	0x0800	/* UHS SDR104 timing */
#define SMC_CAPS_UHS_DDR50	0x1000	/* UHS DDR50 timing */
#define SMC_CAPS_UHS_MASK	0x1c00
#define SMC_CAPS_MMC_DDR52	0x2000  /* eMMC DDR52 timing */
#define SMC_CAPS_MMC_HS200	0x4000	/* eMMC HS200 timing */
#define SMC_CAPS_MMC_HS400	0x8000	/* eMMC HS400 timing */
#define SMC_CAPS_NONREMOVABLE	0x10000	/* non-removable devices */

	int sc_function_count;		/* number of I/O functions (SDIO) */
	struct sdmmc_function *sc_card;	/* selected card */
	struct sdmmc_function *sc_fn0;	/* function 0, the card itself */
	SIMPLEQ_HEAD(, sdmmc_function) sf_head; /* list of card functions */
	int sc_dying;			/* bus driver is shutting down */
	struct proc *sc_task_thread;	/* asynchronous tasks */
	TAILQ_HEAD(, sdmmc_task) sc_tskq;   /* task thread work queue */
	struct sdmmc_task sc_discover_task; /* card attach/detach task */
	struct sdmmc_task sc_intr_task;	/* card interrupt task */
	struct rwlock sc_lock;		/* lock around host controller */
	void *sc_scsibus;		/* SCSI bus emulation softc */
	TAILQ_HEAD(, sdmmc_intr_handler) sc_intrq; /* interrupt handlers */
	long sc_max_seg;		/* maximum segment size */
	long sc_max_xfer;		/* maximum transfer size */
	void *sc_cookies[SDMMC_MAX_FUNCTIONS]; /* pass extra info from bus to dev */
};

/*
 * Attach devices at the sdmmc bus.
 */
struct sdmmc_attach_args {
	struct scsibus_attach_args	 saa;
	struct sdmmc_function		*sf;
};

#define IPL_SDMMC	IPL_BIO
#define splsdmmc()	splbio()

#define	SDMMC_ASSERT_LOCKED(sc) \
	rw_assert_wrlock(&(sc)->sc_lock)

void	sdmmc_add_task(struct sdmmc_softc *, struct sdmmc_task *);
void	sdmmc_del_task(struct sdmmc_task *);

struct	sdmmc_function *sdmmc_function_alloc(struct sdmmc_softc *);
void	sdmmc_function_free(struct sdmmc_function *);
int	sdmmc_set_bus_power(struct sdmmc_softc *, u_int32_t, u_int32_t);
int	sdmmc_mmc_command(struct sdmmc_softc *, struct sdmmc_command *);
int	sdmmc_app_command(struct sdmmc_softc *, struct sdmmc_command *);
void	sdmmc_go_idle_state(struct sdmmc_softc *);
int	sdmmc_select_card(struct sdmmc_softc *, struct sdmmc_function *);
int	sdmmc_set_relative_addr(struct sdmmc_softc *,
	    struct sdmmc_function *);
int	sdmmc_send_if_cond(struct sdmmc_softc *, uint32_t);

void	sdmmc_intr_enable(struct sdmmc_function *);
void	sdmmc_intr_disable(struct sdmmc_function *);
void	*sdmmc_intr_establish(struct device *, int (*)(void *),
	    void *, const char *);
void	sdmmc_intr_disestablish(void *);
void	sdmmc_intr_task(void *);

int	sdmmc_io_enable(struct sdmmc_softc *);
void	sdmmc_io_scan(struct sdmmc_softc *);
int	sdmmc_io_init(struct sdmmc_softc *, struct sdmmc_function *);
void	sdmmc_io_attach(struct sdmmc_softc *);
void	sdmmc_io_detach(struct sdmmc_softc *);
u_int8_t sdmmc_io_read_1(struct sdmmc_function *, int);
u_int16_t sdmmc_io_read_2(struct sdmmc_function *, int);
u_int32_t sdmmc_io_read_4(struct sdmmc_function *, int);
int	sdmmc_io_read_multi_1(struct sdmmc_function *, int, u_char *, int);
int	sdmmc_io_read_region_1(struct sdmmc_function *, int, u_char *, int);
void	sdmmc_io_write_1(struct sdmmc_function *, int, u_int8_t);
void	sdmmc_io_write_2(struct sdmmc_function *, int, u_int16_t);
void	sdmmc_io_write_4(struct sdmmc_function *, int, u_int32_t);
int	sdmmc_io_write_multi_1(struct sdmmc_function *, int, u_char *, int);
int	sdmmc_io_write_region_1(struct sdmmc_function *, int, u_char *, int);
int	sdmmc_io_function_ready(struct sdmmc_function *);
int	sdmmc_io_function_enable(struct sdmmc_function *);
void	sdmmc_io_function_disable(struct sdmmc_function *);
void	sdmmc_io_set_blocklen(struct sdmmc_function *, unsigned int);

int	sdmmc_read_cis(struct sdmmc_function *, struct sdmmc_cis *);
void	sdmmc_print_cis(struct sdmmc_function *);
void	sdmmc_check_cis_quirks(struct sdmmc_function *);

int	sdmmc_mem_enable(struct sdmmc_softc *);
void	sdmmc_mem_scan(struct sdmmc_softc *);
int	sdmmc_mem_init(struct sdmmc_softc *, struct sdmmc_function *);
int	sdmmc_mem_read_block(struct sdmmc_function *, int, u_char *, size_t);
int	sdmmc_mem_write_block(struct sdmmc_function *, int, u_char *, size_t);

#ifdef HIBERNATE
int	sdmmc_mem_hibernate_write(struct sdmmc_function *, daddr_t, u_char *,
	    size_t);
#endif

/* ioctls */

#include <sys/ioccom.h>

struct bio_sdmmc_command {
	void *cookie;
	struct sdmmc_command cmd;
};

struct bio_sdmmc_debug {
	void *cookie;
	int debug;
};

#define SDIOCEXECMMC	_IOWR('S',0, struct bio_sdmmc_command)
#define SDIOCEXECAPP	_IOWR('S',1, struct bio_sdmmc_command)
#define SDIOCSETDEBUG	_IOWR('S',2, struct bio_sdmmc_debug)

#endif
