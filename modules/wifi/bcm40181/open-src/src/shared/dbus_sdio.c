/*
 * Dongle BUS interface
 * Common to all SDIO interface
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: dbus_sdio.c,v 1.41.8.10 2010/11/08 19:34:10 Exp $
 */

#include <typedefs.h>
#include <osl.h>

#include <bcmsdh.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>

#include <siutils.h>
#include <hndpmu.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <sbhnddma.h>
#include <bcmsrom.h>

#include <sdio.h>
#include <spid.h>
#include <sbsdio.h>
#include <sbsdpcmdev.h>
#include <bcmsdpcm.h>

#include <proto/ethernet.h>
#include <proto/802.1d.h>
#include <proto/802.11.h>
#include <sdiovar.h>
#include "dbus.h"

/* FIX: Some of these are brought in from dhdioctl.h.  We'll move
 * DHD-specific features/test code out of DBUS, but for now just don't
 * include dhdioctl.h.
 */
#define DHD_IOCTL_MAXLEN	8192
#ifdef SDTEST
/* For pktgen iovar */
typedef struct dhd_pktgen {
	uint version;		/* To allow structure change tracking */
	uint freq;		/* Max ticks between tx/rx attempts */
	uint count;		/* Test packets to send/rcv each attempt */
	uint print;		/* Print counts every <print> attempts */
	uint total;		/* Total packets (or bursts) */
	uint minlen;		/* Minimum length of packets to send */
	uint maxlen;		/* Maximum length of packets to send */
	uint numsent;		/* Count of test packets sent */
	uint numrcvd;		/* Count of test packets received */
	uint numfail;		/* Count of test send failures */
	uint mode;		/* Test mode (type of test packets) */
	uint stop;		/* Stop after this many tx failures */
} dhd_pktgen_t;

/* Version in case structure changes */
#define DHD_PKTGEN_VERSION 2

/* Type of test packets to use */
#define DHD_PKTGEN_ECHO		1	/* Send echo requests */
#define DHD_PKTGEN_SEND		2	/* Send discard packets */
#define DHD_PKTGEN_RXBURST	3	/* Request dongle send N packets */
#define DHD_PKTGEN_RECV		4	/* Continuous rx from continuous tx dongle */
#endif /* SDTEST */

#define IDLE_IMMEDIATE	(-1)	/* Enter idle immediately (no timeout) */
/* Values for idleclock iovar: other values are the sd_divisor to use when idle */
#define IDLE_ACTIVE	0	/* Do not request any SD clock change when idle */
#define IDLE_STOP	(-1)	/* Request SD clock be stopped (and use SD1 mode) */

#define PRIOMASK	7

#define TXRETRIES	2	/* # of retries for tx frames */

#if defined(CONFIG_MACH_SANDGATE2G)
#define DHD_RXBOUND	250	/* Default for max rx frames in one scheduling */
#else
#define DHD_RXBOUND	50	/* Default for max rx frames in one scheduling */
#endif /* defined(CONFIG_MACH_SANDGATE2G) */

#define DHD_TXBOUND	20	/* Default for max tx frames in one scheduling */
#define DHD_TXMINMAX	1	/* Max tx frames if rx still pending */

#define MEMBLOCK    2048 /* Block size used for downloading of dongle image */
#define MAX_DATA_BUF (32 * 1024)	/* which should be more than
						* and to hold biggest glom possible
						*/

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef SDALIGN
#define SDALIGN	32
#endif
#if !ISPOWEROF2(SDALIGN)
#error SDALIGN is not a power of 2!
#endif

/* Total length of frame header for dongle protocol */
#define SDPCM_HDRLEN	(SDPCM_FRAMETAG_LEN + SDPCM_SWHEADER_LEN)
#ifdef SDTEST
#define SDPCM_RESERVE	(SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + SDALIGN)
#else
#define SDPCM_RESERVE	(SDPCM_HDRLEN + SDALIGN)
#endif

/* Space for header read, limit for data packets */
#define MAX_HDR_READ	32
#define MAX_RX_DATASZ	2048

/* Maximum milliseconds to wait for F2 to come up */
#define DHD_WAIT_F2RDY	4000

/* Value for ChipClockCSR during initial setup */
#define DHD_INIT_CLKCTL1	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ)
#define DHD_INIT_CLKCTL2	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP)

/* Flags for SDH calls */
#define F2SYNC	(SDIO_REQ_4BYTE | SDIO_REQ_FIXED)

/* Packet free applicable unconditionally for sdio and sdspi.  Contional if
 * bufpool was present for gspi bus.
 */
#define PKTFREE2()		if ((sdio_info->bus != SPI_BUS) || sdio_info->usebufpool) \
					dbus_sdcb_pktfree(sdio_info, pkt, FALSE);

typedef struct {
	bool pending;
	bool is_iovar;

	union {
		struct {
			uint8 *buf;
			int len;
		} ctl;
		struct {
			const char *name;
			void *params;
			int plen;
			void *arg;
			int len;
			bool set;
		} iovar;
	};
} sdctl_info_t;

typedef struct {
	dbus_pub_t *pub; /* Must be first */

	void *cbarg;
	dbus_intf_callbacks_t *cbs;
	dbus_intf_t *drvintf;
	void *sdos_info;

	/* FIX: Ported from dhd_info_t */
	uint maxctl;            /* Max size rxctl request from proto to bus */
	ulong rx_readahead_cnt; /* Number of packets where header read-ahead was used. */
	ulong tx_realloc;       /* Number of tx packets we had to realloc for headroom */
	uint32 tx_ctlerrs;
	uint32 tx_ctlpkts;
	uint32 rx_ctlerrs;
	uint32 rx_ctlpkts;
	bool up;                /* Driver up/down (to OS) */
	bool dongle_reset;  /* TRUE = DEVRESET put dongle into reset */
	uint8 wme_dp;   /* wme discard priority */

	sdctl_info_t rxctl_req;
	sdctl_info_t txctl_req;
	bool sdlocked;

	/* FIX: Ported from dhd_bus_t */
	bcmsdh_info_t	*sdh;	/* Handle for BCMSDH calls */
	si_t		*sih;	/* Handle for SI calls */
	char		*vars;	/* Variables (from CIS and/or other) */
	uint		varsz;	/* Size of variables buffer */

	sdpcmd_regs_t	*regs;    /* Registers for SDIO core */
	uint		sdpcmrev; /* SDIO core revision */
	uint		armrev;	  /* CPU core revision */
	uint		ramrev;	  /* SOCRAM core revision */
	uint32		ramsize;  /* Size of RAM in SOCRAM (bytes) */
	uint32		orig_ramsize;  /* Size of RAM in SOCRAM (bytes) */

	uint32		bus;      /* gSPI or SDIO bus */
	uint32		hostintmask;  /* Copy of Host Interrupt Mask */
	uint32		intstatus; /* Intstatus bits (events) pending */
	bool		dpc_sched; /* Indicates DPC schedule (intrpt rcvd) */
	bool		fcstate;   /* State of dongle flow-control */

	char		*firmware_path; /* module_param: path to firmware image */
	char		*nvram_path; /* module_param: path to nvram vars file */

	uint		blocksize; /* Block size of SDIO transfers */
	uint		roundup; /* Max roundup limit */

	struct pktq	txq;	/* Queue length used for flow-control */
	uint8		flowcontrol;	/* per prio flow control bitmask */
	uint8		tx_seq;	/* Transmit sequence number (next) */
	uint8		tx_max;	/* Maximum transmit sequence allowed */

	uint8		hdrbuf[MAX_HDR_READ + SDALIGN];
	uint8		*rxhdr; /* Header of current rx frame (in hdrbuf) */
	uint16		nextlen; /* Next Read Len from last header */
	uint8		rx_seq;	/* Receive sequence number (expected) */
	bool		rxskip;	/* Skip receive (awaiting NAK ACK) */

	void		*glomd;	/* Packet containing glomming descriptor */
	void		*glom;	/* Packet chain for glommed superframe */
	uint		glomerr; /* Glom packet read errors */

	uint8		*rxbuf; /* Buffer for receiving control packets */
	uint		rxblen;	/* Allocated length of rxbuf */
	uint8		*rxctl;	/* Aligned pointer into rxbuf */
	uint8		*databuf; /* Buffer for receiving big glom packet */
	uint8		*dataptr; /* Aligned pointer into databuf */
	uint		rxlen;	/* Length of valid data in buffer */

	uint8		sdpcm_ver; /* Bus protocol reported by dongle */

	bool		intr;	/* Use interrupts */
	bool		poll;	/* Use polling */
	bool		ipend;	/* Device interrupt is pending */
	bool		intdis;	/* Interrupts disabled by isr */
	uint 		intrcount; /* Count of device interrupt callbacks */
	uint		lastintrs; /* Count as of last watchdog timer */
	uint		spurious; /* Count of spurious interrupts */
	uint		pollrate; /* Ticks between device polls */
	uint		polltick; /* Tick counter */
	uint		pollcnt; /* Count of active polls */

	uint		regfails; /* Count of R_REG/W_REG failures */

	uint		clkstate; /* State of sd and backplane clock(s) */
	bool		activity; /* Activity flag for clock down */
	int32		idletime; /* Control for activity timeout */
	uint32		idlecount; /* Activity timeout counter */
	int32		idleclock; /* How to set bus driver when idle */
	uint32		sd_divisor; /* Speed control to bus driver */
	uint32		sd_mode; /* Mode control to bus driver */
	uint32		sd_rxchain; /* If bcmsdh api accepts PKT chains */
	bool		use_rxchain; /* If dhd should use PKT chains */
	bool		sleeping; /* Is SDIO bus sleeping? */
	/* Field to decide if rx of control frames happen in rxbuf or lb-pool */
	bool		usebufpool;

#ifdef SDTEST
	/* external loopback */
	bool	ext_loop;
	uint8	loopid;

	/* pktgen configuration */
	uint	pktgen_freq;	/* Ticks between bursts */
	uint	pktgen_count;	/* Packets to send each burst */
	uint	pktgen_print;	/* Bursts between count displays */
	uint	pktgen_total;	/* Stop after this many */
	uint	pktgen_minlen;	/* Minimum packet data len */
	uint	pktgen_maxlen;	/* Maximum packet data len */
	uint	pktgen_mode;	/* Configured mode: tx, rx, or echo */
	uint	pktgen_stop;	/* Number of tx failures causing stop */

	/* active pktgen fields */
	uint	pktgen_tick;	/* Tick counter for bursts */
	uint	pktgen_ptick;	/* Burst counter for printing */
	uint	pktgen_sent;	/* Number of test packets generated */
	uint	pktgen_rcvd;	/* Number of test packets received */
	uint	pktgen_fail;	/* Number of failed send attempts */
	uint16	pktgen_len;	/* Length of next packet to send */
#endif /* SDTEST */

	/* Some additional counters */
	uint	tx_sderrs;	/* Count of tx attempts with sd errors */
	uint	fcqueued;	/* Tx packets that got queued */
	uint	rxrtx;		/* Count of rtx requests (NAK to dongle) */
	uint	rx_toolong;	/* Receive frames too long to receive */
	uint	rxc_errors;	/* SDIO errors when reading control frames */
	uint	rx_hdrfail;	/* SDIO errors on header reads */
	uint	rx_badhdr;	/* Bad received headers (roosync?) */
	uint	rx_badseq;	/* Mismatched rx sequence number */
	uint	fc_rcvd;	/* Number of flow-control events received */
	uint	fc_xoff;	/* Number which turned on flow-control */
	uint	fc_xon;		/* Number which turned off flow-control */
	uint	rxglomfail;	/* Failed deglom attempts */
	uint	rxglomframes;	/* Number of glom frames (superframes) */
	uint	rxglompkts;	/* Number of packets from glom frames */
	uint	f2rxhdrs;	/* Number of header reads */
	uint	f2rxdata;	/* Number of frame data reads */
	uint	f2txdata;	/* Number of f2 frame writes */
	uint	f1regdata;	/* Number of f1 register accesses */

} sdio_info_t;

typedef struct {
	sdio_info_t *sdio_info;
	dbus_irb_tx_t *txirb;
} pkttag_t;

struct exec_parms {
union {
	struct {
		sdio_info_t *sdio_info;
		int tx_prec_map;
		int *prec_out;
	} pdeq;

	struct {
		sdio_info_t *sdio_info;
		void *pkt;
		int prec;
	} penq;
};
};

/* clkstate */
#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2	/* Not used yet */
#define CLK_AVAIL	3

#define DHD_NOPMU(dhd)	(FALSE)


/* Tx/Rx bounds */
uint dhd_txbound = DHD_TXBOUND;
uint dhd_rxbound = DHD_RXBOUND;
/* static uint dhd_txminmax = DHD_TXMINMAX; */

/* overrride the RAM size if possible */
#define DONGLE_MIN_MEMSIZE (128 *1024)
int dhd_dongle_memsize = 0;


static bool dhd_alignctl = TRUE;

static bool sd1idle = TRUE;

static bool retrydata = FALSE;
#define RETRYCHAN(chan) (((chan) == SDPCM_EVENT_CHANNEL) || retrydata)

static const uint watermark = 8;
static const uint firstread = 32;

#ifdef SDTEST
/* Echo packet generator (SDIO), pkts/s */
extern uint dhd_pktgen;

/* Echo packet len (0 => sawtooth, max 1800) */
extern uint dhd_pktgen_len;
#define MAX_PKTGEN_LEN 1800
#endif
extern uint dhd_watchdog_ms;

/* optionally set by a module_param_string() */
#define MOD_PARAM_PATHLEN       2048
char fw_path[MOD_PARAM_PATHLEN];
char nv_path[MOD_PARAM_PATHLEN];

/* Use interrupts */
extern uint dhd_intr;

/* Use polling */
extern uint dhd_poll;

/* Initial idletime behavior (immediate, never, or ticks) */
extern int dhd_idletime;
#define DHD_IDLETIME_TICKS 1;

/* SDIO Drive Strength */
extern uint dhd_sdiod_drive_strength;

/* Override to force tx queueing all the time */
extern uint dhd_force_tx_queueing;

#define HDATLEN (firstread - (SDPCM_HDRLEN))

/* Retry count for register access failures */
static uint retry_limit = 2;

/* Force even SD lengths (some host controllers mess up on odd bytes) */
static bool forcealign = TRUE;

/*
 * Default is to bring up eth1 immediately.
 */
uint delay_eth = 0;

#define ALIGNMENT  4

#define PKTALIGN(osh, p, len, align) \
	do {                                                        \
		uint datalign;                                      \
								    \
		datalign = (uintptr)PKTDATA((osh), (p));            \
		datalign = ROUNDUP(datalign, (align)) - datalign;   \
		ASSERT(datalign < (align));                         \
		ASSERT(PKTLEN((osh), (p)) >= ((len) + datalign));   \
		if (datalign)                                       \
			PKTPULL((osh), (p), datalign);              \
		PKTSETLEN((osh), (p), (len));                       \
	} while (0)

/* Limit on rounding up frames */
static uint max_roundup = 512;

/* Try doing readahead */
static bool dhd_readahead = TRUE;

/* To check if there's window offered */
#define DATAOK(bus) \
	(((uint8)(sdio_info->tx_max - sdio_info->tx_seq) != 0) && \
	(((uint8)(sdio_info->tx_max - sdio_info->tx_seq) & 0x80) == 0))

/* Macros to get register read/write status */
/* NOTE: these assume a local dbus_sdio_bus_t *bus! */

#define R_SDREG(regvar, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		regvar = R_REG(sdio_info->pub->osh, regaddr); \
	} while (bcmsdh_regfail(sdio_info->sdh) && (++retryvar <= retry_limit)); \
	if (retryvar) { \
		sdio_info->regfails += (retryvar-1); \
		if (retryvar > retry_limit) { \
			DBUSERR(("%s: FAILED" #regvar "READ, LINE %d\n", \
			           __FUNCTION__, __LINE__)); \
			regvar = 0; \
		} \
	} \
} while (0)

#define W_SDREG(regval, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		W_REG(sdio_info->pub->osh, regaddr, regval); \
	} while (bcmsdh_regfail(sdio_info->sdh) && (++retryvar <= retry_limit)); \
	if (retryvar) { \
		sdio_info->regfails += (retryvar-1); \
		if (retryvar > retry_limit) \
			DBUSERR(("%s: FAILED REGISTER WRITE, LINE %d\n", \
			           __FUNCTION__, __LINE__)); \
	} \
} while (0)


#define SD_BUSTYPE			SDIO_BUS

#define PKT_AVAILABLE()		(intstatus & I_HMB_FRAME_IND)

#define HOSTINTMASK		(I_TOHOSTMAIL | I_CHIPACTIVE)

#define GSPI_PR55150_BAILOUT


#define BUS_WAKE(sdio_info) \
	do { \
		if ((sdio_info)->sleeping) \
			dbus_sdio_bussleep((sdio_info), FALSE); \
	} while (0);

/* Debug */
#define DBUSINTR DBUSTRACE
#define DBUSINFO DBUSTRACE
#define DBUSTIMER DBUSTRACE
#define DBUSGLOM DBUSTRACE
#define DBUSDATA DBUSTRACE
#define DBUSCTL DBUSTRACE
#define DBUSGLOM_ON() 0

/* IOVar table */
enum {
	IOV_INTR = 1,
	IOV_POLLRATE,
	IOV_SDREG,
	IOV_SBREG,
	IOV_SDCIS,
	IOV_MEMBYTES,
	IOV_MEMSIZE,
	IOV_DOWNLOAD,
	IOV_FORCEEVEN,
	IOV_SDIOD_DRIVE,
	IOV_READAHEAD,
	IOV_SDRXCHAIN,
	IOV_ALIGNCTL,
	IOV_SDALIGN,
	IOV_DEVRESET,
#ifdef SDTEST
	IOV_PKTGEN,
	IOV_EXTLOOP,
#endif /* SDTEST */
	IOV_SPROM,
	IOV_TXBOUND,
	IOV_RXBOUND,
	IOV_IDLETIME,
	IOV_IDLECLOCK,
	IOV_SD1IDLE,
	IOV_SLEEP,
	IOV_VARS
};

static const bcm_iovar_t dbus_sdio_iovars[] = {
	{"intr",	IOV_INTR,	0,	IOVT_BOOL,	0 },
	{"sleep",	IOV_SLEEP,	0,	IOVT_BOOL,	0 },
	{"pollrate",	IOV_POLLRATE,	0,	IOVT_UINT32,	0 },
	{"idletime",	IOV_IDLETIME,	0,	IOVT_INT32,	0 },
	{"idleclock",	IOV_IDLECLOCK,	0,	IOVT_INT32,	0 },
	{"sd1idle",	IOV_SD1IDLE,	0,	IOVT_BOOL,	0 },
	{"membytes",	IOV_MEMBYTES,	0,	IOVT_BUFFER,	2 * sizeof(int) },
	{"memsize",	IOV_MEMSIZE,	0,	IOVT_UINT32,	0 },
	{"download",	IOV_DOWNLOAD,	0,	IOVT_BOOL,	0 },
	{"vars",	IOV_VARS,	0,	IOVT_BUFFER,	0 },
	{"sdiod_drive",	IOV_SDIOD_DRIVE, 0,	IOVT_UINT32,	0 },
	{"readahead",	IOV_READAHEAD,	0,	IOVT_BOOL,	0 },
	{"sdrxchain",	IOV_SDRXCHAIN,	0,	IOVT_BOOL,	0 },
	{"alignctl",	IOV_ALIGNCTL,	0,	IOVT_BOOL,	0 },
	{"sdalign",	IOV_SDALIGN,	0,	IOVT_BOOL,	0 },
	{"devreset",	IOV_DEVRESET,	0,	IOVT_BOOL,	0 },
#ifdef SDTEST
	{"extloop",	IOV_EXTLOOP,	0,	IOVT_BOOL,	0 },
	{"pktgen",	IOV_PKTGEN,	0,	IOVT_BUFFER,	sizeof(dhd_pktgen_t) },
#endif /* SDTEST */

	{NULL, 0, 0, 0, 0 }
};

typedef struct {
	chipcregs_t	*ccregs;
	sdpcmd_regs_t	*sdregs;
	uint32		socram_size;
} chipinfo_t;

/* This stores SD Host info during probe callback
 * since attach() is not called yet at this point
 */
typedef struct {
	uint16 venid;
	uint16 devid;
	uint16 bus_no;
	uint16 slot;
	uint16 func;
	uint bustype;
	void *regsva;
	osl_t *osh;	/* Comes from SD Host */
	bool free_probe_osh;

	bcmsdh_info_t	*sdh;	/* Handle for BCMSDH calls */
	si_t		*sih;	/* Handle for SI calls */

	uint32		ramsize;  /* Size of RAM in SOCRAM (bytes) */
	uint32		orig_ramsize;  /* Size of RAM in SOCRAM (bytes) */
	char		*vars;	/* Variables (from CIS and/or other) */
	uint		varsz;	/* Size of variables buffer */
	bool alp_only; /* Don't use HT clock (ALP only) */

	char *firmware_file;
	char *nvram_file;
	bool devready;

	uint32 dl_addr;
	const chipinfo_t *chinfo;
} probe_sdh_info_t;

static probe_sdh_info_t g_probe_info;

/*
 * FIX: Basic information needed to prep dongle for download.
 * The goal is to simplify probe setup before a valid
 * image has been downloaded.  Also, can we avoid si_attach() during
 * probe setup since it brings in a lot of unnecessary dependencies?
 */

/* 4325 and 4315 have the same address map */
static const chipinfo_t chipinfo_4325_15 = {
	(chipcregs_t *) 0x18000000,
	(sdpcmd_regs_t *) 0x18002000,
	(384 * 1024)
};

static const chipinfo_t chipinfo_4329 = {
	(chipcregs_t *) 0x18000000,
	(sdpcmd_regs_t *) 0x18011000,
	(288 * 1024)
};

static const chipinfo_t chipinfo_4336 = {
	(chipcregs_t *) 0x18000000,
	(sdpcmd_regs_t *) 0x18002000,
	(240 * 1024)
};

static const chipinfo_t chipinfo_4330 = {
	(chipcregs_t *) 0x18000000,
	(sdpcmd_regs_t *) 0x18002000,
	(240 * 1024)
};

static const chipinfo_t chipinfo_43237 = {
	(chipcregs_t *) 0x18000000,
	(sdpcmd_regs_t *) 0x18002000,
	(320 * 1024)
};

/*
 * SDH registration callbacks
 */
static void * dbus_sdh_probe(uint16 venid, uint16 devid, uint16 bus_no, uint16 slot,
	uint16 func, uint bustype, void *regsva, osl_t * osh,
	void *sdh);
static void dbus_sdh_disconnect(void *ptr);
static void dbus_sdh_isr(void *handle);

/*
 * Local function prototypes
 */
static void *dbus_sdio_probe_cb(void *handle, const char *desc, uint32 bustype, uint32 hdrlen);
static void dbus_sdio_disconnect_cb(void *handle);

#ifdef SDTEST
static void dbus_sdio_sdtest_set(sdio_info_t *sdio_info, bool start);
static void dbus_sdio_testrcv(sdio_info_t *sdio_info, void *pkt, uint seq);
#endif
static bool dbus_sdio_attach_init(sdio_info_t *sdio_info, void *sdh,
	char *firmware_path, char * nvram_path);
static void dbus_sdio_release(sdio_info_t *sdio_info, osl_t *osh);
static void dbus_sdio_release_dongle(sdio_info_t *sdio_info, osl_t *osh);
static int dbus_sdio_rxctl(sdio_info_t *sdio_info, uchar *msg, uint msglen);
static uint dbus_sdio_sendfromq(sdio_info_t *sdio_info, uint maxframes);
static int dbus_sdio_txctl(sdio_info_t *sdio_info, uchar *msg, uint msglen);
static void dbus_sdio_txq_flush(sdio_info_t *sdio_info);

/*
 * NOTE: These functions can also be called before attach() occurs
 * so do not access sdio_info from them.  This is to support DBUS
 * async probe callback to upper layer such as DHD/BMAC/etc.  Another
 * alternative was to modify SDH to do async probe callback only
 * when a valid image is downloaded to the dongle.
 */
static bool dbus_sdio_probe_init(probe_sdh_info_t *pinfo);
static void dbus_sdio_probe_deinit(probe_sdh_info_t *pinfo);
static int dbus_sdio_download_state(probe_sdh_info_t *pinfo, bool enter);
static int dbus_sdio_membytes(probe_sdh_info_t *pinfo, bool write,
	uint32 address, uint8 *data, uint size);
static int dbus_sdio_write_vars(probe_sdh_info_t *pinfo);
static int dbus_sdio_downloadvars(probe_sdh_info_t *pinfo, void *arg, int len);
#if defined(BCM_DNGL_EMBEDIMAGE)
static int dhd_bus_download_nvram_file(probe_sdh_info_t *pinfo, char * nvram_path);
#endif
#ifdef BCM_DNGL_EMBEDIMAGE
static int dhd_bus_download_image_array(probe_sdh_info_t *pinfo,
	char * nvram_path, uint8 *fw, int len);
#endif

/*
 * Wrappers to interface functions in dbus_sdio_os.c
 */
static void dbus_sdos_lock(sdio_info_t *sdio_info);
static void dbus_sdos_unlock(sdio_info_t *sdio_info);
static void * dbus_sdos_exec_txlock(sdio_info_t *sdio_info, exec_cb_t cb, struct exec_parms *args);
static int dbus_sdos_sched_dpc(sdio_info_t *sdio_info);
#ifndef BCM_DNGL_EMBEDIMAGE
static int dbus_sdos_sched_probe_cb(void);
#endif

/*
 * Wrappers to callback functions in dbus.c
 */
static void *dbus_sdcb_pktget(sdio_info_t *sdio_info, uint len, bool send);
static void dbus_sdcb_pktfree(sdio_info_t *sdio_info, void *p, bool send);
static dbus_irb_t *dbus_sdcb_getirb(sdio_info_t *sdio_info, bool send);

/*
 * Callbacks common to all SDIO
 */
static void dbus_sdio_disconnect_cb(void *handle);
static void dbus_sdio_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb);
static void dbus_sdio_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status);
static void dbus_sdio_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status);
static void dbus_sdio_errhandler(void *handle, int err);
static void dbus_sdio_ctl_complete(void *handle, int type, int status);
static void dbus_sdio_state_change(void *handle, int state);
static bool dbus_sdio_isr(void *handle, bool *wantdpc);
static bool dbus_sdio_dpc(void *handle, bool bounded);
static void dbus_sdio_watchdog(void *handle);

static dbus_intf_callbacks_t dbus_sdio_intf_cbs = {
	dbus_sdio_send_irb_timeout,
	dbus_sdio_send_irb_complete,
	dbus_sdio_recv_irb_complete,
	dbus_sdio_errhandler,
	dbus_sdio_ctl_complete,
	dbus_sdio_state_change,
	dbus_sdio_isr,
	dbus_sdio_dpc,
	dbus_sdio_watchdog
};

/*
 * Need global for probe() and disconnect() since
 * attach() is not called at probe and detach()
 * can be called inside disconnect()
 */
static probe_cb_t probe_cb = NULL;
static disconnect_cb_t disconnect_cb = NULL;
static void *probe_arg = NULL;
static void *disc_arg = NULL;

/* 
 * dbus_intf_t common to all SDIO
 * These functions override dbus_sdio_os.c.
 */
static void *dbus_sdif_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs);
static void dbus_sdif_detach(dbus_pub_t *pub, void *info);
static int dbus_sdif_send_irb(void *bus, dbus_irb_tx_t *txirb);
static int dbus_sdif_send_ctl(void *bus, uint8 *buf, int len);
static int dbus_sdif_recv_ctl(void *bus, uint8 *buf, int len);
static int dbus_sdif_up(void *bus);
static int dbus_sdif_iovar_op(void *bus, const char *name,
	void *params, int plen, void *arg, int len, bool set);
static bool dbus_sdif_device_exists(void *bus);
static bool dbus_sdif_dlneeded(void *bus);
static int dbus_sdif_dlstart(void *bus, uint8 *fw, int len);
static int dbus_sdif_dlrun(void *bus);
static int dbus_sdif_stop(void *bus);
static int dbus_sdif_down(void *bus);
static int dbus_sdif_get_attrib(void *bus, dbus_attrib_t *attrib);

static dbus_intf_t dbus_sdio_intf;
static dbus_intf_t *g_dbusintf = NULL;

/* Register/Unregister functions are called by the main DHD entry
 * point (e.g. module insertion) to link with the bus driver, in
 * order to look for or await the device.
 */

bcmsdh_driver_t sdh_driver = {
	dbus_sdh_probe,
	dbus_sdh_disconnect
};

/* Functions shared between dbus_sdio.c/dbus_sdio_os.c */
extern int dbus_sdio_txq_sched(void *bus);
extern int dbus_sdio_txq_stop(void *bus);
extern int dbus_sdio_txq_process(void *bus);
extern int probe_dlstart(void);
extern int probe_dlstop(void);
extern int probe_dlwrite(uint8 *buf, int count, bool isvars);
extern int probe_iovar(const char *name, void *params, int plen, void *arg, int len, bool set,
	void **val, int *val_len);

/*
 * Local functions
 */
static int
dbus_sdio_set_siaddr_window(bcmsdh_info_t *sdh, uint32 address)
{
	int err = 0;
	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
	                 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRMID,
		                 (address >> 16) & SBSDIO_SBADDRMID_MASK, &err);
	if (!err)
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRHIGH,
		                 (address >> 24) & SBSDIO_SBADDRHIGH_MASK, &err);
	return err;
}


static int
dbus_sdio_alpclk(bcmsdh_info_t *sdh)
{
	int err;
	uint8 clkctl = 0;

	/*
	 * Request ALP clock; ALP is required before starting a download
	 */
	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_ALP_AVAIL_REQ, &err);
	if (err) {
		DBUSERR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
		return DBUS_ERR;
	}

	/* Check current status */
	clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (err) {
		DBUSERR(("%s: HT Avail read error: %d\n", __FUNCTION__, err));
		return DBUS_ERR;
	}

	if (!SBSDIO_CLKAV(clkctl, TRUE)) {
		SPINWAIT(((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, &err)),
			!SBSDIO_CLKAV(clkctl, TRUE)), PMU_MAX_TRANSITION_DLY);
	}

	return DBUS_OK;
}

/* Turn backplane clock on or off */
static int
dbus_sdio_htclk(sdio_info_t *sdio_info, bool on, bool pendok)
{
	int err;
	uint8 clkctl, clkreq, devctl;
	bcmsdh_info_t *sdh;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	clkctl = 0;
	sdh = sdio_info->sdh;


	if (on) {
		/* Request HT Avail */
		clkreq = g_probe_info.alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;


		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		if (err) {
			DBUSERR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}

		if (pendok &&
		    ((sdio_info->sih->buscoretype == PCMCIA_CORE_ID) &&
			(sdio_info->sih->buscorerev == 9))) {
			uint32 dummy, retries;
			R_SDREG(dummy, &sdio_info->regs->clockctlstatus, retries);
		}

		/* Check current status */
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DBUSERR(("%s: HT Avail read error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}

		/* Go to pending and await interrupt if appropriate */
		if (!SBSDIO_CLKAV(clkctl, g_probe_info.alp_only) && pendok) {
			DBUSINFO(("CLKCTL: set PENDING\n"));
			sdio_info->clkstate = CLK_PENDING;

			/* Allow only clock-available interrupt */
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DBUSERR(("%s: Devctl access error setting CA: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}

			devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			return BCME_OK;
		} else if (sdio_info->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
		}

		/* Otherwise, wait here (polling) for HT Avail */
		if (!SBSDIO_CLKAV(clkctl, g_probe_info.alp_only)) {
			SPINWAIT(((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				SBSDIO_FUNC1_CHIPCLKCSR, &err)),
				!SBSDIO_CLKAV(clkctl, g_probe_info.alp_only)),
				PMU_MAX_TRANSITION_DLY);
		}
		if (err) {
			DBUSERR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}
		if (!SBSDIO_CLKAV(clkctl, g_probe_info.alp_only)) {
			DBUSERR(("%s: HT Avail timeout (%d): clkctl 0x%02x\n",
			           __FUNCTION__, PMU_MAX_TRANSITION_DLY, clkctl));
			return BCME_ERROR;
		}

		/* Mark clock available */
		sdio_info->clkstate = CLK_AVAIL;
		DBUSINFO(("CLKCTL: turned ON\n"));


		sdio_info->activity = TRUE;
	} else {
		clkreq = 0;

		if (sdio_info->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
		}

		sdio_info->clkstate = CLK_SDONLY;
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		DBUSINFO(("CLKCTL: turned OFF\n"));
		if (err) {
			DBUSERR(("%s: Failed access turning clock off: %d\n",
			           __FUNCTION__, err));
			return BCME_ERROR;
		}
	}
	return BCME_OK;
}

/* Change idle/active SD state */
static int
dbus_sdio_sdclk(sdio_info_t *sdio_info, bool on)
{
	int err;
	int32 iovalue;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (on) {
		if (sdio_info->idleclock == IDLE_STOP) {
			/* Turn on clock and restore mode */
			iovalue = 1;
			err = bcmsdh_iovar_op(sdio_info->sdh, "sd_clock", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DBUSERR(("%s: error enabling sd_clock: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}

			iovalue = sdio_info->sd_mode;
			err = bcmsdh_iovar_op(sdio_info->sdh, "sd_mode", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DBUSERR(("%s: error changing sd_mode: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		} else if (sdio_info->idleclock != IDLE_ACTIVE) {
			/* Restore clock speed */
			iovalue = sdio_info->sd_divisor;
			err = bcmsdh_iovar_op(sdio_info->sdh, "sd_divisor", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DBUSERR(("%s: error restoring sd_divisor: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
		sdio_info->clkstate = CLK_SDONLY;
	} else {
		/* Stop or slow the SD clock itself */
		if ((sdio_info->sd_divisor == -1) || (sdio_info->sd_mode == -1)) {
			DBUSTRACE(("%s: can't idle clock, divisor %d mode %d\n",
			           __FUNCTION__, sdio_info->sd_divisor, sdio_info->sd_mode));
			return BCME_ERROR;
		}
		if (sdio_info->idleclock == IDLE_STOP) {
			if (sd1idle) {
				/* Change to SD1 mode and turn off clock */
				iovalue = 1;
				err = bcmsdh_iovar_op(sdio_info->sdh, "sd_mode", NULL, 0,
				                      &iovalue, sizeof(iovalue), TRUE);
				if (err) {
					DBUSERR(("%s: error changing sd_clock: %d\n",
					           __FUNCTION__, err));
					return BCME_ERROR;
				}
			}

			iovalue = 0;
			err = bcmsdh_iovar_op(sdio_info->sdh, "sd_clock", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DBUSERR(("%s: error disabling sd_clock: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		} else if (sdio_info->idleclock != IDLE_ACTIVE) {
			/* Set divisor to idle value */
			iovalue = sdio_info->idleclock;
			err = bcmsdh_iovar_op(sdio_info->sdh, "sd_divisor", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DBUSERR(("%s: error changing sd_divisor: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
		sdio_info->clkstate = CLK_NONE;
	}

	return BCME_OK;
}

/* Transition SD and backplane clock readiness */
static int
dbus_sdio_clkctl(sdio_info_t *sdio_info, uint target, bool pendok)
{
	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	/* Early exit if we're already there */
	if (sdio_info->clkstate == target) {
		if (target == CLK_AVAIL)
			sdio_info->activity = TRUE;
		return BCME_OK;
	}

	switch (target) {
	case CLK_AVAIL:
		/* Make sure SD clock is available */
		if (sdio_info->clkstate == CLK_NONE)
			dbus_sdio_sdclk(sdio_info, TRUE);
		/* Now request HT Avail on the backplane */
		dbus_sdio_htclk(sdio_info, TRUE, pendok);
		sdio_info->activity = TRUE;
		break;

	case CLK_SDONLY:
		/* Remove HT request, or bring up SD clock */
		if (sdio_info->clkstate == CLK_NONE)
			dbus_sdio_sdclk(sdio_info, TRUE);
		else if (sdio_info->clkstate == CLK_AVAIL)
			dbus_sdio_htclk(sdio_info, FALSE, FALSE);
		else
			DBUSERR(("dbus_sdio_clkctl: request for %d -> %d\n",
			           sdio_info->clkstate, target));
		break;

	case CLK_NONE:
		/* Make sure to remove HT request */
		if (sdio_info->clkstate == CLK_AVAIL)
			dbus_sdio_htclk(sdio_info, FALSE, FALSE);
		/* Now remove the SD clock */
		dbus_sdio_sdclk(sdio_info, FALSE);
		break;
	}
	DBUSINFO(("dbus_sdio_clkctl: %d -> %d\n", oldstate, sdio_info->clkstate));

	return BCME_OK;
}

static int
dbus_sdio_bussleep(sdio_info_t *sdio_info, bool sleep)
{
	bcmsdh_info_t *sdh = sdio_info->sdh;
	sdpcmd_regs_t *regs = sdio_info->regs;
	uint retries = 0;

	DBUSINFO(("dbus_sdio_bussleep: request %s (currently %s)\n",
	          (sleep ? "SLEEP" : "WAKE"),
	          (sdio_info->sleeping ? "SLEEP" : "WAKE")));

	/* Done if we're already in the requested state */
	if (sleep == sdio_info->sleeping)
		return BCME_OK;

	/* Going to sleep: set the alarm and turn off the lights... */
	if (sleep) {
		/* Don't sleep if something is pending */
		if (sdio_info->dpc_sched || sdio_info->rxskip || pktq_len(&sdio_info->txq))
			return BCME_BUSY;


		/* Disable SDIO interrupts (no longer interested) */
		bcmsdh_intr_disable(sdio_info->sdh);

		/* Make sure the controller has the bus up */
		dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);

		/* Tell device to start using OOB wakeup */
		W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
		if (retries > retry_limit)
			DBUSERR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));

		/* Turn off our contribution to the HT clock request */
		dbus_sdio_clkctl(sdio_info, CLK_SDONLY, FALSE);

		/* Isolate the bus */
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL,
		                 SBSDIO_DEVCTL_PADS_ISO, NULL);

		/* Change state */
		sdio_info->sleeping = TRUE;

	} else {
		/* Waking up: bus power up is ok, set local state */

		/* Force pad isolation off if possible (in case power never toggled) */
		if ((sdio_info->sih->buscoretype == PCMCIA_CORE_ID) &&
			(sdio_info->sih->buscorerev >= 10))
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, 0, NULL);


		/* Make sure we have SD bus access */
		if (sdio_info->clkstate == CLK_NONE)
			dbus_sdio_clkctl(sdio_info, CLK_SDONLY, FALSE);

		/* Send misc interrupt to indicate OOB not needed */
		W_SDREG(0, &regs->tosbmailboxdata, retries);
		if (retries <= retry_limit)
			W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);

		if (retries > retry_limit)
			DBUSERR(("CANNOT SIGNAL CHIP TO CLEAR OOB!!\n"));

		/* Change state */
		sdio_info->sleeping = FALSE;

		/* Enable interrupts again */
		if (sdio_info->intr && (sdio_info->pub->busstate == DBUS_STATE_UP)) {
			sdio_info->intdis = FALSE;
			bcmsdh_intr_enable(sdio_info->sdh);
		}
	}

	return BCME_OK;
}

/* Writes a HW/SW header into the packet and sends it. */
/* Assumes: (a) header space already there, (b) caller holds lock */
static int
dbus_sdio_txpkt(sdio_info_t *sdio_info, void *pkt, uint chan)
{
	int ret;
	osl_t *osh;
	uint8 *frame;
	uint16 len, pad;
	uint32 swheader;
	uint retries = 0;
	bcmsdh_info_t *sdh;
	void *new;
	pkttag_t *ptag;
	int i;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	sdh = sdio_info->sdh;
	osh = sdio_info->pub->osh;

	if (sdio_info->dongle_reset) {
		ret = BCME_NOTREADY;
		goto done;
	}

	frame = (uint8*)PKTDATA(osh, pkt);

	/* Add alignment padding, allocate new packet if needed */
	if ((pad = ((uintptr)frame % SDALIGN))) {
		if (PKTHEADROOM(osh, pkt) < pad) {
			DBUSINFO(("%s: insufficient headroom %d for %d pad\n",
			          __FUNCTION__, (int)PKTHEADROOM(osh, pkt), pad));
			sdio_info->tx_realloc++;
			new = dbus_sdcb_pktget(sdio_info, (PKTLEN(osh, pkt) + SDALIGN), TRUE);
			if (!new) {
				DBUSERR(("%s: couldn't allocate new %d-byte packet\n",
				           __FUNCTION__, PKTLEN(osh, pkt) + SDALIGN));
				ret = BCME_NOMEM;
				goto done;
			}

			PKTALIGN(osh, new, PKTLEN(osh, pkt), SDALIGN);
			bcopy(PKTDATA(osh, pkt), PKTDATA(osh, new), PKTLEN(osh, pkt));
			dbus_sdcb_pktfree(sdio_info, pkt, TRUE);
			pkt = new;
			frame = (uint8*)PKTDATA(osh, pkt);
			ASSERT(((uintptr)frame % SDALIGN) == 0);
			pad = 0;
		} else {
			PKTPUSH(osh, pkt, pad);
			frame = (uint8*)PKTDATA(osh, pkt);
			bzero(frame, pad + SDPCM_HDRLEN);
		}
	}
	ASSERT(pad < SDALIGN);

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	len = (uint16)PKTLEN(osh, pkt);
	*(uint16*)frame = htol16(len);
	*(((uint16*)frame) + 1) = htol16(~len);

	/* Software tag: channel, sequence number, data offset */
	swheader = ((chan << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK) | sdio_info->tx_seq |
	        (((pad + SDPCM_HDRLEN) << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
	htol32_ua_store(swheader, frame + SDPCM_FRAMETAG_LEN);
	htol32_ua_store(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));
	sdio_info->tx_seq = (sdio_info->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

	/* Raise len to next SDIO block to eliminate tail command */
	if (sdio_info->roundup && sdio_info->blocksize && (len > sdio_info->blocksize)) {
		pad = sdio_info->blocksize - (len % sdio_info->blocksize);
		if ((pad <= sdio_info->roundup) && (pad < sdio_info->blocksize))
#ifdef NOTUSED
			if (pad <= PKTTAILROOM(osh, pkt))
#endif
				len += pad;
	}

	/* Some controllers have trouble with odd bytes -- round to even */
	if (forcealign && (len & (ALIGNMENT - 1))) {
#ifdef NOTUSED
		if (PKTTAILROOM(osh, pkt))
#endif
			len = ROUNDUP(len, ALIGNMENT);
#ifdef NOTUSED
		else
			DBUSERR(("%s: sending unrounded %d-byte packet\n", __FUNCTION__, len));
#endif
	}

	do {
		ret = bcmsdh_send_buf(sdh, SI_ENUM_BASE, SDIO_FUNC_2, F2SYNC,
		                      frame, len, pkt, NULL, NULL);
		sdio_info->f2txdata++;
		ASSERT(ret != BCME_PENDING);

		if (ret < 0) {
			/* On failure, abort the command and terminate the frame */
			DBUSINFO(("%s: sdio error %d, abort command and terminate frame.\n",
			          __FUNCTION__, ret));
			sdio_info->tx_sderrs++;

			ret = bcmsdh_abort(sdh, SDIO_FUNC_2);
			if (ret == BCME_NODEVICE) {
				dbus_sdio_state_change(sdio_info, DBUS_STATE_DISCONNECT);
				break;
			}
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL,
			                 SFC_WF_TERM, NULL);
			sdio_info->f1regdata++;

			for (i = 0; i < 3; i++) {
				uint8 hi, lo;
				hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCHI, NULL);
				lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCLO, NULL);
				sdio_info->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}
		}
	} while ((ret < 0) && retrydata && retries++ < TXRETRIES);

done:
	ASSERT(OSL_PKTTAG_SZ >= sizeof(pkttag_t));
	ptag = (pkttag_t *) PKTTAG(pkt);
	ASSERT(ptag);
	dbus_sdio_send_irb_complete(sdio_info, ptag->txirb, (ret ? DBUS_ERR_TXFAIL : DBUS_OK));

	dbus_sdcb_pktfree(sdio_info, pkt, TRUE);
	return ret;
}

static void *
dbus_prec_pkt_deq(sdio_info_t *sdio_info, int tx_prec_map, int *prec_out)
{
	return pktq_mdeq(&sdio_info->txq, tx_prec_map, prec_out);
}

static void *
dbus_prec_pkt_deq_exec(struct exec_parms *args)
{
	return dbus_prec_pkt_deq(args->pdeq.sdio_info, args->pdeq.tx_prec_map,
		args->pdeq.prec_out);
}

/*
 * FIX: Move WMM pkt prioritization out of DBUS/SDIO to DHD so
 * USB can leverage the same logic
 */
static bool
dbus_prec_pkt_enq(sdio_info_t *sdio_info, void *pkt, int prec)
{
	void *p;
	int eprec = -1;		/* precedence to evict from */
	bool discard_oldest;
	struct pktq *q = &sdio_info->txq;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(q, prec) && !pktq_full(q)) {
		pktq_penq(q, prec, pkt);
		return TRUE;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(q, prec))
		eprec = prec;
	else if (pktq_full(q)) {
		p = pktq_peek_tail(q, &eprec);
		ASSERT(p);
		if (eprec > prec)
			goto err;
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		ASSERT(!pktq_pempty(q, eprec));
		discard_oldest = AC_BITMAP_TST(sdio_info->wme_dp, eprec);
		if (eprec == prec && !discard_oldest)
			goto err; /* refuse newer (incoming) packet */
		/* Evict packet according to discard policy */
		p = discard_oldest ? pktq_pdeq(q, eprec) : pktq_pdeq_tail(q, eprec);
		ASSERT(p);

		dbus_sdcb_pktfree(sdio_info, p, TRUE);
	}

	/* Enqueue */
	p = pktq_penq(q, prec, pkt);
	ASSERT(p);

	return TRUE;
err:
	return FALSE;
}

static void *
dbus_prec_pkt_enq_exec(struct exec_parms *args)
{
	return (void *) (uintptr) dbus_prec_pkt_enq(args->penq.sdio_info, args->penq.pkt,
		args->penq.prec);
}

static int
dbus_sdio_txbuf_submit(sdio_info_t *sdio_info, dbus_irb_tx_t *txirb)
{
	int ret = 0;
	int berr;
	osl_t *osh;
	uint datalen, prec;
	void *pkt;
	pkttag_t *ptag;
	struct exec_parms exec_args;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	osh = sdio_info->pub->osh;
	pkt = txirb->pkt;
	if (pkt == NULL) {
		/*
		 * For BMAC sdio high driver that uses send_buf,
		 * we need to convert the buf into pkt for dbus.
		 */
		datalen = txirb->len;
		DBUSTRACE(("%s: Converting buf(%d bytes) to pkt.\n", __FUNCTION__, datalen));
		pkt = dbus_sdcb_pktget(sdio_info, datalen, TRUE);
		if (pkt == NULL) {
			DBUSERR(("%s: Out of Tx buf.\n", __FUNCTION__));
			return DBUS_ERR_TXDROP;
		}

		txirb->pkt = pkt;
		bcopy(txirb->buf, PKTDATA(osh, pkt), datalen);
		PKTLEN(osh, pkt) = datalen;
	} else
		datalen = PKTLEN(osh, pkt);

	ASSERT(OSL_PKTTAG_SZ >= sizeof(pkttag_t));
	ptag = (pkttag_t *) PKTTAG(pkt);
	ptag->sdio_info = sdio_info;
	ptag->txirb = txirb;

#ifdef SDTEST
	/* Push the test header if doing loopback */
	if (sdio_info->ext_loop) {
		uint8* data;
		PKTPUSH(osh, pkt, SDPCM_TEST_HDRLEN);
		data = PKTDATA(osh, pkt);
		*data++ = SDPCM_TEST_ECHOREQ;
		*data++ = (uint8)sdio_info->loopid++;
		*data++ = (datalen >> 0);
		*data++ = (datalen >> 8);
		datalen += SDPCM_TEST_HDRLEN;
	}
#endif /* SDTEST */

	ASSERT(PKTHEADROOM(osh, pkt) >= SDPCM_HDRLEN);
	/* Add space for the header */
	PKTPUSH(osh, pkt, SDPCM_HDRLEN);
	ASSERT(ISALIGNED((uintptr)PKTDATA(osh, pkt), 2));

	prec = PRIO2PREC((PKTPRIO(pkt) & PRIOMASK));

	sdio_info->fcqueued++;

	/* Priority based enq */
	exec_args.penq.sdio_info = sdio_info;
	exec_args.penq.pkt = pkt;
	exec_args.penq.prec = prec;
	berr = (uintptr) dbus_sdos_exec_txlock(sdio_info,
		(exec_cb_t) dbus_prec_pkt_enq_exec, &exec_args);
	if (berr == FALSE) {
		DBUSERR(("%s: Dropping pkt!\n", __FUNCTION__));
		ASSERT(0); /* FIX: Should not be dropping pkts */
		ret = DBUS_ERR_TXFAIL;
		goto err;
	}
	dbus_sdio_txq_sched(sdio_info->sdos_info);

err:
	return ret;
}

static void
dbus_bus_stop(sdio_info_t *sdio_info)
{
	uint8 saveclk;
	uint retries;
	int err;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	BUS_WAKE(sdio_info);

	dbus_sdio_txq_stop(sdio_info->sdos_info);

	/* Enable clock for device interrupts */
	dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);

	/* Disable and clear interrupts at the chip level also */
	W_SDREG(0, &sdio_info->regs->hostintmask, retries);
	W_SDREG(sdio_info->hostintmask, &sdio_info->regs->intstatus, retries);
	sdio_info->hostintmask = 0;

	/* Change our idea of bus state */
	sdio_info->pub->busstate = DBUS_STATE_DOWN;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = bcmsdh_cfg_read(sdio_info->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DBUSERR(("%s: Failed to force clock for F2: err %d\n", __FUNCTION__, err));
	}

	/* Turn off the bus (F2), free any pending packets */
	DBUSINTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
	bcmsdh_intr_disable(sdio_info->sdh);
	bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);

	/* Turn off the backplane clock (only) */
	dbus_sdio_clkctl(sdio_info, CLK_SDONLY, FALSE);

	dbus_sdio_txq_flush(sdio_info);

	/* Clear any held glomming stuff */
	if (sdio_info->glomd) {
		dbus_sdcb_pktfree(sdio_info, sdio_info->glomd, FALSE);
		sdio_info->glomd = NULL;
	}

	if (sdio_info->glom) {
		dbus_sdcb_pktfree(sdio_info, sdio_info->glom, FALSE);
		sdio_info->glom = NULL;
	}

	/* Clear rx control and wake any waiters */
	sdio_info->rxlen = 0;

	/* Reset some F2 state stuff */
	sdio_info->rxskip = FALSE;
	sdio_info->tx_seq = sdio_info->rx_seq = 0;
}

static int
dbus_sdio_init(sdio_info_t *sdio_info)
{
	uint retries = 0;

	uint8 ready = 0, enable;
	int err, ret = 0;
	uint8 saveclk;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	/* Make sure backplane clock is on, needed to generate F2 interrupt */
	dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);
	if (sdio_info->clkstate != CLK_AVAIL)
		goto exit;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = bcmsdh_cfg_read(sdio_info->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DBUSERR(("%s: Failed to force clock for F2: err %d\n", __FUNCTION__, err));
		goto exit;
	}

	/* Enable function 2 (frame transfers) */
	W_SDREG((SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT),
	        &sdio_info->regs->tosbmailboxdata, retries);
	enable = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, enable, NULL);

	/* Using interrupt mode and wait for up indication from dongle */
	bcmsdh_intr_enable(sdio_info->sdh); /* We get interrupts immediately */

	/* FIX: Interrupt does not happen under PXA at this point.  Why?
	 */

	/* Give the dongle some time to do its thing and set IOR2 */
	retries = DHD_WAIT_F2RDY;

	while ((enable !=
	        ((ready = bcmsdh_cfg_read(sdio_info->sdh, SDIO_FUNC_0, SDIOD_CCCR_IORDY, NULL)))) &&
	       retries--) {
		OSL_DELAY(1000);
	}

	retries = 0;

	DBUSERR(("%s: enable 0x%02x, ready 0x%02x\n", __FUNCTION__, enable, ready));


	/* If F2 successfully enabled, set core and enable interrupts */
	if (ready == enable) {
		/* Make sure we're talking to the core. */
		if (!(sdio_info->regs = si_setcore(sdio_info->sih, PCMCIA_CORE_ID, 0)))
			sdio_info->regs = si_setcore(sdio_info->sih, SDIOD_CORE_ID, 0);

		bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK,
			(uint8)watermark, &err);

		/* bcmsdh_intr_unmask(sdio_info->sdh); */

		sdio_info->pub->busstate = DBUS_STATE_UP;
		sdio_info->intdis = FALSE;
		if (sdio_info->intr) {
			DBUSINTR(("%s: enable SDIO device interrupts\n", __FUNCTION__));
			bcmsdh_intr_enable(sdio_info->sdh);
		} else {
			DBUSINTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
			bcmsdh_intr_disable(sdio_info->sdh);
		}

	}


	else {
		ret = DBUS_ERR;

		/* Disable F2 again */
		enable = SDIO_FUNC_ENABLE_1;
		bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, enable, NULL);
	}

	/* Restore previous clock setting */
	bcmsdh_cfg_write(sdio_info->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, saveclk, &err);


	/* If we didn't come up, turn off backplane clock */
	if (sdio_info->pub->busstate != DBUS_STATE_UP) {
		DBUSERR(("Error: Not up yet!\n"));
		dbus_sdio_clkctl(sdio_info, CLK_NONE, FALSE);
	}
exit:
	return ret;
}

static void
dbus_sdio_rxfail(sdio_info_t *sdio_info, bool abort, bool rtx)
{
	bcmsdh_info_t *sdh = sdio_info->sdh;
	sdpcmd_regs_t *regs = sdio_info->regs;
	uint retries = 0;
	uint16 lastrbc;
	uint8 hi, lo;
	int err;

	DBUSERR(("%s: %sterminate frame%s\n", __FUNCTION__,
	           (abort ? "abort command, " : ""), (rtx ? ", send NAK" : "")));

	if (abort) {
		err = bcmsdh_abort(sdh, SDIO_FUNC_2);
		if (err == BCME_NODEVICE) {
			dbus_sdio_state_change(sdio_info, DBUS_STATE_DISCONNECT);
			return;
		}
	}

	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL, SFC_RF_TERM, &err);
	sdio_info->f1regdata++;

	/* Wait until the packet has been flushed (device/FIFO stable) */
	for (lastrbc = retries = 0xffff; retries > 0; retries--) {
		hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_RFRAMEBCHI, NULL);
		lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_RFRAMEBCLO, NULL);
		sdio_info->f1regdata += 2;

		if ((hi == 0) && (lo == 0))
			break;

		if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
			DBUSERR(("%s: count growing: last 0x%04x now 0x%04x\n",
			           __FUNCTION__, lastrbc, ((hi << 8) + lo)));
		}
		lastrbc = (hi << 8) + lo;
	}

	if (!retries) {
		DBUSERR(("%s: count never zeroed: last 0x%04x\n", __FUNCTION__, lastrbc));
	} else {
		DBUSINFO(("%s: flush took %d iterations\n", __FUNCTION__, (0xffff - retries)));
	}

	if (rtx) {
		sdio_info->rxrtx++;
		W_SDREG(SMB_NAK, &regs->tosbmailbox, retries);
		sdio_info->f1regdata++;
		if (retries <= retry_limit) {
			sdio_info->rxskip = TRUE;
		}
	}

	/* Clear partial in any case */
	sdio_info->nextlen = 0;

	/* If we can't reach the device, signal failure */
	if (err || bcmsdh_regfail(sdh))
		sdio_info->pub->busstate = DBUS_STATE_DOWN;
}

static void
dbus_sdio_read_control(sdio_info_t *sdio_info, uint8 *hdr, uint len, uint doff)
{
	bcmsdh_info_t *sdh = sdio_info->sdh;
	uint rdlen, pad;

	int sdret;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	/* Control data already received in aligned rxctl */
	if ((sdio_info->bus == SPI_BUS) && (!sdio_info->usebufpool))
		goto gotpkt;

	ASSERT(sdio_info->rxbuf);
	/* Set rxctl for frame (w/optional alignment) */
	sdio_info->rxctl = sdio_info->rxbuf;
	if (dhd_alignctl) {
		sdio_info->rxctl += firstread;
		if ((pad = ((uintptr)sdio_info->rxctl % SDALIGN)))
			sdio_info->rxctl += (SDALIGN - pad);
		sdio_info->rxctl -= firstread;
	}
	ASSERT(sdio_info->rxctl >= sdio_info->rxbuf);

	/* Copy the already-read portion over */
	bcopy(hdr, sdio_info->rxctl, firstread);
	if (len <= firstread)
		goto gotpkt;

	/* Copy the full data pkt in gSPI case and process ioctl. */
	if (sdio_info->bus == SPI_BUS) {
		bcopy(hdr, sdio_info->rxctl, len);
		goto gotpkt;
	}

	/* Raise rdlen to next SDIO block to avoid tail command */
	rdlen = len - firstread;
	if (sdio_info->roundup && sdio_info->blocksize && (rdlen > sdio_info->blocksize)) {
		pad = sdio_info->blocksize - (rdlen % sdio_info->blocksize);
		if ((pad <= sdio_info->roundup) && (pad < sdio_info->blocksize) &&
		    ((len + pad) < sdio_info->maxctl))
			rdlen += pad;
	}

	/* Satisfy length-alignment requirements */
	if (forcealign && (rdlen & (ALIGNMENT - 1)))
		rdlen = ROUNDUP(rdlen, ALIGNMENT);

	/* Drop if the read is too big or it exceeds our maximum */
	if ((rdlen + firstread) > sdio_info->maxctl) {
		DBUSERR(("%s: %d-byte control read exceeds %d-byte buffer\n",
		           __FUNCTION__, rdlen, sdio_info->maxctl));
		sdio_info->pub->stats.rx_errors++;
		dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
		goto done;
	}

	if ((len - doff) > sdio_info->maxctl) {
		DBUSERR(("%s: %d-byte ctl frame (%d-byte ctl data) exceeds %d-byte limit\n",
		           __FUNCTION__, len, (len - doff), sdio_info->maxctl));
		sdio_info->pub->stats.rx_errors++; sdio_info->rx_toolong++;
		dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
		goto done;
	}


	/* Read remainder of frame body into the rxctl buffer */
	sdret = bcmsdh_recv_buf(sdh, SI_ENUM_BASE, SDIO_FUNC_2, F2SYNC,
	                        (sdio_info->rxctl + firstread), rdlen, NULL, NULL, NULL);
	sdio_info->f2rxdata++;
	ASSERT(sdret != BCME_PENDING);

	/* Control frame failures need retransmission */
	if (sdret < 0) {
		DBUSERR(("%s: read %d control bytes failed: %d\n", __FUNCTION__, rdlen, sdret));
		sdio_info->rxc_errors++; /* dhd.rx_ctlerrs is higher level */
		dbus_sdio_rxfail(sdio_info, TRUE, TRUE);
		goto done;
	}

gotpkt:
	/* Point to valid data and indicate its length */
	sdio_info->rxctl += doff;

	if (sdio_info->rxlen != 0) {
		DBUSERR(("dropping previous recv ctl pkt\n"));
	}
	sdio_info->rxlen = len - doff;

	if (sdio_info->cbarg && sdio_info->cbs) {
		if (sdio_info->rxctl_req.pending == TRUE) {
			dbus_sdio_rxctl(sdio_info, sdio_info->rxctl_req.ctl.buf,
				sdio_info->rxctl_req.ctl.len);
			bzero(&sdio_info->rxctl_req, sizeof(sdio_info->rxctl_req));
			dbus_sdio_ctl_complete(sdio_info, DBUS_CBCTL_READ, DBUS_OK);
		}
		/* If receive ctl pkt before user request, leave in cache
		 * and retrieve it next time recv_ctl() is called.
		 */
	}
done:
	return;
}

static uint8
dbus_sdio_rxglom(sdio_info_t *sdio_info, uint8 rxseq)
{
	uint16 dlen, totlen;
	uint8 *dptr, num = 0;

	uint16 sublen, check;
	void *pfirst, *plast, *pnext, *save_pfirst;
	osl_t *osh = sdio_info->pub->osh;

	int errcode;
	uint8 chan, seq, doff, sfdoff;
	uint8 txmax;

	bool usechain = sdio_info->use_rxchain;

	/* If packets, issue read(s) and send up packet chain */
	/* Return sequence numbers consumed? */

	DBUSTRACE(("dbus_sdio_rxglom: start: glomd %p glom %p\n",
		sdio_info->glomd, sdio_info->glom));

	/* If there's a descriptor, generate the packet chain */
	if (sdio_info->glomd) {
		dlen = (uint16)PKTLEN(osh, sdio_info->glomd);
		dptr = PKTDATA(osh, sdio_info->glomd);
		if (!dlen || (dlen & 1)) {
			DBUSERR(("%s: bad glomd len (%d), toss descriptor\n",
			           __FUNCTION__, dlen));
			dbus_sdcb_pktfree(sdio_info, sdio_info->glomd, FALSE);
			sdio_info->glomd = NULL;
			sdio_info->nextlen = 0;
			return 0;
		}

		pfirst = plast = pnext = NULL;

		for (totlen = num = 0; dlen; num++) {
			/* Get (and move past) next length */
			sublen = ltoh16_ua(dptr);
			dlen -= sizeof(uint16);
			dptr += sizeof(uint16);
			if ((sublen < SDPCM_HDRLEN) ||
			    ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
				DBUSERR(("%s: desciptor len %d bad: %d\n",
				           __FUNCTION__, num, sublen));
				pnext = NULL;
				break;
			}
			if (sublen % SDALIGN) {
				DBUSERR(("%s: sublen %d not a multiple of %d\n",
				           __FUNCTION__, sublen, SDALIGN));
				usechain = FALSE;
			}
			totlen += sublen;

			/* For last frame, adjust read len so total is a block multiple */
			if (!dlen) {
				sublen += (ROUNDUP(totlen, sdio_info->blocksize) - totlen);
				totlen = ROUNDUP(totlen, sdio_info->blocksize);
			}

			/* Allocate/chain packet for next subframe */
			if ((pnext = dbus_sdcb_pktget(sdio_info,
				sublen + SDALIGN, FALSE)) == NULL) {
				DBUSERR(("%s: dbus_sdio_pktget failed, num %d len %d\n",
				           __FUNCTION__, num, sublen));
				break;
			}
			ASSERT(!PKTLINK(pnext));
			if (!pfirst) {
				ASSERT(!plast);
				pfirst = plast = pnext;
			} else {
				ASSERT(plast);
				PKTSETNEXT(osh, plast, pnext);
				plast = pnext;
			}

			/* Adhere to start alignment requirements */
			PKTALIGN(osh, pnext, sublen, SDALIGN);
		}

		/* If allocation failed, toss entirely and increment count */
		if (!pnext) {
			if (pfirst)
				dbus_sdcb_pktfree(sdio_info, pfirst, FALSE);
			dbus_sdcb_pktfree(sdio_info, sdio_info->glomd, FALSE);
			sdio_info->glomd = NULL;
			sdio_info->nextlen = 0;
			return 0;
		}

		/* Ok, we have a packet chain, save in bus structure */
		DBUSGLOM(("%s: allocated %d-byte packet chain for %d subframes\n",
		          __FUNCTION__, totlen, num));
		if (DBUSGLOM_ON() && sdio_info->nextlen) {
			if (totlen != sdio_info->nextlen) {
				DBUSGLOM(("%s: glomdesc mismatch: nextlen %d glomdesc %d "
				          "rxseq %d\n", __FUNCTION__, sdio_info->nextlen,
				          totlen, rxseq));
			}
		}
		sdio_info->glom = pfirst;

		/* Done with descriptor packet */
		dbus_sdcb_pktfree(sdio_info, sdio_info->glomd, FALSE);
		sdio_info->glomd = NULL;
		sdio_info->nextlen = 0;
	}

	/* Ok -- either we just generated a packet chain, or had one from before */
	if (sdio_info->glom) {
		if (DBUSGLOM_ON()) {
			DBUSGLOM(("%s: attempt superframe read, packet chain:\n", __FUNCTION__));
			for (pnext = sdio_info->glom; pnext; pnext = PKTNEXT(osh, pnext)) {
				DBUSGLOM(("    %p: %p len 0x%04x (%d)\n",
				          pnext, (uint8*)PKTDATA(osh, pnext),
				          PKTLEN(osh, pnext), PKTLEN(osh, pnext)));
			}
		}

		pfirst = sdio_info->glom;
		dlen = (uint16)pkttotlen(osh, pfirst);

		/* Do an SDIO read for the superframe.  Configurable iovar to
		 * read directly into the chained packet, or allocate a large
		 * packet and and copy into the chain.
		 */
		if (usechain) {
			errcode = bcmsdh_recv_buf(sdio_info->sdh, SI_ENUM_BASE, SDIO_FUNC_2,
			                          F2SYNC, (uint8*)PKTDATA(osh, pfirst),
			                          dlen, pfirst, NULL, NULL);
		} else if (sdio_info->dataptr) {
			errcode = bcmsdh_recv_buf(sdio_info->sdh, SI_ENUM_BASE, SDIO_FUNC_2,
			                          F2SYNC, sdio_info->dataptr,
			                          dlen, NULL, NULL, NULL);
			sublen = (uint16)pktfrombuf(osh, pfirst, 0, dlen, sdio_info->dataptr);
			if (sublen != dlen) {
				DBUSERR(("%s: FAILED TO COPY, dlen %d sublen %d\n",
				           __FUNCTION__, dlen, sublen));
				errcode = -1;
			}
			pnext = NULL;
		} else {
			DBUSERR(("COULDN'T ALLOC %d-BYTE GLOM, FORCE FAILURE\n", dlen));
			errcode = -1;
		}
		sdio_info->f2rxdata++;
		ASSERT(errcode != BCME_PENDING);

		/* On failure, kill the superframe, allow a couple retries */
		if (errcode < 0) {
			DBUSERR(("%s: glom read of %d bytes failed: %d\n",
			           __FUNCTION__, dlen, errcode));
			sdio_info->pub->stats.rx_errors++;

			if (sdio_info->glomerr++ < 3) {
				dbus_sdio_rxfail(sdio_info, TRUE, TRUE);
			} else {
				sdio_info->glomerr = 0;
				dbus_sdio_rxfail(sdio_info, TRUE, FALSE);
				dbus_sdcb_pktfree(sdio_info, sdio_info->glom, FALSE);
				sdio_info->rxglomfail++;
				sdio_info->glom = NULL;
			}
			OSL_DELAY(dlen/128);
			return 0;
		}



		/* Validate the superframe header */
		dptr = (uint8 *)PKTDATA(osh, pfirst);
		sublen = ltoh16_ua(dptr);
		check = ltoh16_ua(dptr + sizeof(uint16));

		chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
		sdio_info->nextlen = dptr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((sdio_info->nextlen << 4) > MAX_RX_DATASZ) {
			DBUSINFO(("%s: got frame w/nextlen too large (%d) seq %d\n",
			          __FUNCTION__, sdio_info->nextlen, seq));
			sdio_info->nextlen = 0;
		}
		doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

		errcode = 0;
		if ((uint16)~(sublen^check)) {
			DBUSERR(("%s (superframe): HW hdr error: len/check 0x%04x/0x%04x\n",
			           __FUNCTION__, sublen, check));
			errcode = -1;
		} else if (ROUNDUP(sublen, sdio_info->blocksize) != dlen) {
			DBUSERR(("%s (superframe): len 0x%04x, rounded 0x%04x, expect 0x%04x\n",
				__FUNCTION__, sublen,
				ROUNDUP(sublen, sdio_info->blocksize), dlen));
			errcode = -1;
		} else if (SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]) != SDPCM_GLOM_CHANNEL) {
			DBUSERR(("%s (superframe): bad channel %d\n", __FUNCTION__,
			           SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN])));
			errcode = -1;
		} else if (SDPCM_GLOMDESC(&dptr[SDPCM_FRAMETAG_LEN])) {
			DBUSERR(("%s (superframe): got second descriptor?\n", __FUNCTION__));
			errcode = -1;
		} else if ((doff < SDPCM_HDRLEN) ||
		           (doff > (PKTLEN(osh, pfirst) - SDPCM_HDRLEN))) {
			DBUSERR(("%s (superframe): Bad data offset %d: HW %d pkt %d min %d\n",
			           __FUNCTION__, doff, sublen, PKTLEN(osh, pfirst), SDPCM_HDRLEN));
			errcode = -1;
		}

		/* Check sequence number of superframe SW header */
		if (rxseq != seq) {
			DBUSINFO(("%s: (superframe) rx_seq %d, expected %d\n",
			          __FUNCTION__, seq, rxseq));
			sdio_info->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((uint8)(txmax - sdio_info->tx_seq) > 0x40) {
			DBUSERR(("%s: got unlikely tx max %d with tx_seq %d\n",
			           __FUNCTION__, txmax, sdio_info->tx_seq));
			txmax = sdio_info->tx_seq + 2;
		}
		sdio_info->tx_max = txmax;

		/* Remove superframe header, remember offset */
		PKTPULL(osh, pfirst, doff);
		sfdoff = doff;

		/* Validate all the subframe headers */
		for (num = 0, pnext = pfirst; pnext && !errcode;
		     num++, pnext = PKTNEXT(osh, pnext)) {
			dptr = (uint8 *)PKTDATA(osh, pnext);
			dlen = (uint16)PKTLEN(osh, pnext);
			sublen = ltoh16_ua(dptr);
			check = ltoh16_ua(dptr + sizeof(uint16));
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

			if ((uint16)~(sublen^check)) {
				DBUSERR(("%s (subframe %d): HW hdr error: "
				           "len/check 0x%04x/0x%04x\n",
				           __FUNCTION__, num, sublen, check));
				errcode = -1;
			} else if ((sublen > dlen) || (sublen < SDPCM_HDRLEN)) {
				DBUSERR(("%s (subframe %d): length mismatch: "
				           "len 0x%04x, expect 0x%04x\n",
				           __FUNCTION__, num, sublen, dlen));
				errcode = -1;
			} else if ((chan != SDPCM_DATA_CHANNEL) &&
			           (chan != SDPCM_EVENT_CHANNEL)) {
				DBUSERR(("%s (subframe %d): bad channel %d\n",
				           __FUNCTION__, num, chan));
				errcode = -1;
			} else if ((doff < SDPCM_HDRLEN) || (doff > sublen)) {
				DBUSERR(("%s (subframe %d): Bad data offset %d: HW %d min %d\n",
				           __FUNCTION__, num, doff, sublen, SDPCM_HDRLEN));
				errcode = -1;
			}
		}

		if (errcode) {
			/* Terminate frame on error, request a couple retries */
			if (sdio_info->glomerr++ < 3) {
				/* Restore superframe header space */
				PKTPUSH(osh, pfirst, sfdoff);
				dbus_sdio_rxfail(sdio_info, TRUE, TRUE);
			} else {
				sdio_info->glomerr = 0;
				dbus_sdio_rxfail(sdio_info, TRUE, FALSE);
				dbus_sdcb_pktfree(sdio_info, sdio_info->glom, FALSE);
				sdio_info->rxglomfail++;
				sdio_info->glom = NULL;
			}
			sdio_info->nextlen = 0;
			return 0;
		}

		/* Basic SD framing looks ok - process each packet (header) */
		save_pfirst = pfirst;
		sdio_info->glom = NULL;
		plast = NULL;

		for (num = 0; pfirst; rxseq++, pfirst = pnext) {
			pnext = PKTNEXT(osh, pfirst);
			PKTSETNEXT(osh, pfirst, NULL);

			dptr = (uint8 *)PKTDATA(osh, pfirst);
			sublen = ltoh16_ua(dptr);
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

			DBUSGLOM(("%s: Get subframe %d, %p(%p/%d), sublen %d chan %d seq %d\n",
			          __FUNCTION__, num, pfirst, PKTDATA(osh, pfirst),
			          PKTLEN(osh, pfirst), sublen, chan, seq));

			ASSERT((chan == SDPCM_DATA_CHANNEL) || (chan == SDPCM_EVENT_CHANNEL));

			if (rxseq != seq) {
				DBUSGLOM(("%s: rx_seq %d, expected %d\n",
				          __FUNCTION__, seq, rxseq));
				sdio_info->rx_badseq++;
				rxseq = seq;
			}

			PKTSETLEN(osh, pfirst, sublen);
			PKTPULL(osh, pfirst, doff);

			if (PKTLEN(osh, pfirst) == 0) {
				dbus_sdcb_pktfree(sdio_info, pfirst, FALSE);
				if (plast) {
					PKTSETNEXT(osh, plast, pnext);
				} else {
					ASSERT(save_pfirst == pfirst);
					save_pfirst = pnext;
				}
				continue;
			}

			/* this packet will go up, link back into chain and count it */
			PKTSETNEXT(osh, pfirst, pnext);
			plast = pfirst;
			num++;

		}

		{
			int i;
			void *pnext;
			void *plist;
			dbus_irb_rx_t *rxirb;

			plist = save_pfirst;
			for (i = 0; plist && i < num; i++, plist = pnext) {
				pnext = PKTNEXT(osh, plist);
				PKTSETNEXT(osh, plist, NULL);

				rxirb = (dbus_irb_rx_t *) dbus_sdcb_getirb(sdio_info, FALSE);
				if (rxirb != NULL) {
					rxirb->pkt = plist;
					dbus_sdio_recv_irb_complete(sdio_info, rxirb, DBUS_OK);
				} else {
					ASSERT(0); /* FIX: Handle this case */
				}
			}
		}

		sdio_info->rxglomframes++;
		sdio_info->rxglompkts += num;
	}
	return num;
}

/* Return TRUE if there may be more frames to read */
static uint
dbus_sdio_readframes(sdio_info_t *sdio_info, uint maxframes, bool *finished)
{
	bcmsdh_info_t *sdh = sdio_info->sdh;

	uint16 len, check;	/* Extracted hardware header fields */
	uint8 chan, seq, doff;	/* Extracted software header fields */
	uint8 fcbits;		/* Extracted fcbits from software header */
	uint8 delta;

	void *pkt;	/* Packet for event or data frames */
	uint16 pad;	/* Number of pad bytes to read */
	uint16 rdlen;	/* Total number of bytes to read */
	uint8 rxseq;	/* Next sequence number to expect */
	uint rxleft = 0;	/* Remaining number of frames allowed */
	int sdret;	/* Return code from bcmsdh calls */
	uint8 txmax;	/* Maximum tx sequence offered */
	uint32 dstatus = 0;	/* gSPI device status bits of */
	bool len_consistent; /* Result of comparing readahead len and len from hw-hdr */
	uint8 *rxbuf;
	dbus_irb_rx_t *rxirb;

#if defined(SDTEST)
	bool sdtest = FALSE;	/* To limit message spew from test mode */
#endif

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(maxframes);

#ifdef SDTEST
	/* Allow pktgen to override maxframes */
	if (sdio_info->pktgen_count && (sdio_info->pktgen_mode == DHD_PKTGEN_RECV)) {
		maxframes = sdio_info->pktgen_count;
		sdtest = TRUE;
	}
#endif

	/* Not finished unless we encounter no more frames indication */
	*finished = FALSE;


	for (rxseq = sdio_info->rx_seq, rxleft = maxframes;
	     !sdio_info->rxskip && rxleft && sdio_info->pub->busstate != DBUS_STATE_DOWN;
	     rxseq++, rxleft--) {

		/* Handle glomming separately */
		if (sdio_info->glom || sdio_info->glomd) {
			uint8 cnt;
			DBUSGLOM(("%s: calling rxglom: glomd %p, glom %p\n",
			          __FUNCTION__, sdio_info->glomd, sdio_info->glom));

			cnt = dbus_sdio_rxglom(sdio_info, rxseq);
			DBUSGLOM(("%s: rxglom returned %d\n", __FUNCTION__, cnt));
			rxseq += cnt - 1;
			rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
			continue;
		}

		/* Try doing single read if we can */
		if (dhd_readahead && sdio_info->nextlen) {
			uint16 nextlen = sdio_info->nextlen;
			sdio_info->nextlen = 0;

			if (sdio_info->bus == SPI_BUS) {
				rdlen = len = nextlen;
			}
			else {
				rdlen = len = nextlen << 4;

				/* Pad read to blocksize for efficiency */
				if (sdio_info->roundup && sdio_info->blocksize &&
					(rdlen > sdio_info->blocksize)) {
					pad = sdio_info->blocksize - (rdlen % sdio_info->blocksize);
					if ((pad <= sdio_info->roundup) &&
						(pad < sdio_info->blocksize) &&
						((rdlen + pad + firstread) < MAX_RX_DATASZ))
						rdlen += pad;
				}
			}

			/* We use sdio_info->rxctl buffer in WinXP for initial control pkt receives.
			 * Later we use buffer-poll for data as well as control packets.
			 * This is required becuase dhd receives full frame in gSPI unlike SDIO.
			 * After the frame is received we have to distinguish whether it is data
			 * or non-data frame.
			 */
			/* Allocate a packet buffer */
			if (!(pkt = dbus_sdcb_pktget(sdio_info, rdlen + SDALIGN, FALSE))) {
				if (sdio_info->bus == SPI_BUS) {
					sdio_info->usebufpool = FALSE;
					sdio_info->rxctl = sdio_info->rxbuf;
					if (dhd_alignctl) {
						sdio_info->rxctl += firstread;
						if ((pad = ((uintptr)sdio_info->rxctl % SDALIGN)))
							sdio_info->rxctl += (SDALIGN - pad);
						sdio_info->rxctl -= firstread;
					}
					ASSERT(sdio_info->rxctl >= sdio_info->rxbuf);
					rxbuf = sdio_info->rxctl;
					/* Read the entire frame */
					sdret = bcmsdh_recv_buf(sdh, SI_ENUM_BASE, SDIO_FUNC_2,
					           F2SYNC, rxbuf, rdlen, NULL, NULL, NULL);
					sdio_info->f2rxdata++;
					ASSERT(sdret != BCME_PENDING);


					/* Control frame failures need retransmission */
					if (sdret < 0) {
						DBUSERR(("%s: read %d control bytes failed: %d\n",
						   __FUNCTION__, rdlen, sdret));
						/* dhd.rx_ctlerrs is higher level */
						sdio_info->rxc_errors++;
						dbus_sdio_rxfail(sdio_info, TRUE,
						    (sdio_info->bus == SPI_BUS) ? FALSE : TRUE);
						continue;
					}
				} else {
				/* Give up on data, request rtx of events */
				DBUSERR(("%s (nextlen): dbus_sdio_pktget failed: len %d rdlen %d "
				           "expected rxseq %d\n",
				           __FUNCTION__, len, rdlen, rxseq));
				/* Just go try again w/normal header read */
				continue;
				}
			} else {
				if (sdio_info->bus == SPI_BUS)
					sdio_info->usebufpool = TRUE;

				ASSERT(!PKTLINK(pkt));
				PKTALIGN(sdio_info->pub->osh, pkt, rdlen, SDALIGN);
				rxbuf = (uint8 *)PKTDATA(sdio_info->pub->osh, pkt);
				/* Read the entire frame */
				sdret = bcmsdh_recv_buf(sdh, SI_ENUM_BASE, SDIO_FUNC_2, F2SYNC,
				          rxbuf, rdlen, pkt, NULL, NULL);
				sdio_info->f2rxdata++;
				ASSERT(sdret != BCME_PENDING);

				if (bcmsdh_get_dstatus((void *)sdio_info->sdh) & STATUS_UNDERFLOW) {
					sdio_info->nextlen = 0;
					*finished = TRUE;
					DBUSERR(("%s (nextlen): read %d bytes failed due "
					           "to spi underflow\n",
					           __FUNCTION__, rdlen));
					dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
					sdio_info->pub->stats.rx_errors++;
					continue;
				}

				if (sdret < 0) {
					DBUSERR(("%s (nextlen): read %d bytes failed: %d\n",
					   __FUNCTION__, rdlen, sdret));
					dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
					sdio_info->pub->stats.rx_errors++;
					/* Force retry w/normal header read.  Don't attemp NAK for
					 * gSPI
					 */
					dbus_sdio_rxfail(sdio_info, TRUE,
					      (sdio_info->bus == SPI_BUS) ? FALSE : TRUE);
					continue;
				}
			}

			/* Now check the header */
			bcopy(rxbuf, sdio_info->rxhdr, SDPCM_HDRLEN);

			/* Extract hardware header fields */
			len = ltoh16_ua(sdio_info->rxhdr);
			check = ltoh16_ua(sdio_info->rxhdr + sizeof(uint16));

			/* All zeros means readahead info was bad */
			if (!(len|check)) {
				DBUSINFO(("%s (nextlen): read zeros in HW header???\n",
				           __FUNCTION__));
				PKTFREE2();
				GSPI_PR55150_BAILOUT;
				continue;
			}

			/* Validate check bytes */
			if ((uint16)~(len^check)) {
				DBUSERR(("%s (nextlen): HW hdr error: nextlen/len/check"
				           " 0x%04x/0x%04x/0x%04x\n", __FUNCTION__, nextlen,
				           len, check));
				PKTFREE2();
				sdio_info->rx_badhdr++;
				dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			/* Validate frame length */
			if (len < SDPCM_HDRLEN) {
				DBUSERR(("%s (nextlen): HW hdr length invalid: %d\n",
				           __FUNCTION__, len));
				PKTFREE2();
				GSPI_PR55150_BAILOUT;
				continue;
			}

			/* Check for consistency with readahead info */
			if (sdio_info->bus == SPI_BUS)
				len_consistent = (nextlen != len);
			else
				len_consistent = (nextlen != (ROUNDUP(len, 16) >> 4));
			if (len_consistent) {
				/* Mismatch, force retry w/normal header (may be >4K) */
				DBUSERR(("%s (nextlen): mismatch, nextlen %d len %d rnd %d; "
				           "expected rxseq %d\n",
				           __FUNCTION__, nextlen, len, ROUNDUP(len, 16), rxseq));
				PKTFREE2();
				dbus_sdio_rxfail(sdio_info, TRUE,
					(sdio_info->bus == SPI_BUS) ? FALSE : TRUE);
				GSPI_PR55150_BAILOUT;
				continue;
			}


			/* Extract software header fields */
			chan = SDPCM_PACKET_CHANNEL(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);
			txmax = SDPCM_WINDOW_VALUE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);

				sdio_info->nextlen =
				      sdio_info->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
				if ((sdio_info->nextlen << 4) > MAX_RX_DATASZ) {
					DBUSINFO(("%s (nextlen): got frame w/nextlen too large"
					      " (%d), seq %d\n", __FUNCTION__, sdio_info->nextlen,
					      seq));
					sdio_info->nextlen = 0;
				}

				sdio_info->rx_readahead_cnt ++;

			/* Handle Flow Control - Brett */
			fcbits = SDPCM_FCMASK_VALUE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);

			delta = 0;
			if (~sdio_info->flowcontrol & fcbits) {
				sdio_info->fc_xoff++;
				delta = 1;
			}
			if (sdio_info->flowcontrol & ~fcbits) {
				sdio_info->fc_xon++;
				delta = 1;
			}

			if (delta) {
				sdio_info->fc_rcvd++;
				sdio_info->flowcontrol = fcbits;
			}

			/* Check and update sequence number */
			if (rxseq != seq) {
				DBUSINFO(("%s (nextlen): rx_seq %d, expected %d\n",
				          __FUNCTION__, seq, rxseq));
				sdio_info->rx_badseq++;
				rxseq = seq;
			}

			/* Check window for sanity */
			if ((uint8)(txmax - sdio_info->tx_seq) > 0x40) {
				if ((sdio_info->bus == SPI_BUS) &&
					!(dstatus & STATUS_F2_RX_READY)) {
					DBUSERR(("%s: got unlikely tx max %d with tx_seq %d\n",
						__FUNCTION__, txmax, sdio_info->tx_seq));
					txmax = sdio_info->tx_seq + 2;
				} else {
					DBUSERR(("%s: got unlikely tx max %d with tx_seq %d\n",
						__FUNCTION__, txmax, sdio_info->tx_seq));
					txmax = sdio_info->tx_seq + 2;
				}
			}
			sdio_info->tx_max = txmax;

			if (chan == SDPCM_CONTROL_CHANNEL) {
				if (sdio_info->bus == SPI_BUS) {
					dbus_sdio_read_control(sdio_info, rxbuf, len, doff);
					if (sdio_info->usebufpool) {
						dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
					}
					continue;
				} else {
					DBUSERR(("%s (nextlen): readahead on control"
					           " packet %d?\n", __FUNCTION__, seq));
					/* Force retry w/normal header read */
					sdio_info->nextlen = 0;
					dbus_sdio_rxfail(sdio_info, FALSE, TRUE);
					PKTFREE2();
					continue;
				}
			}

			if ((sdio_info->bus == SPI_BUS) && !sdio_info->usebufpool) {
				DBUSERR(("Received %d bytes on %d channel. Running out of "
				           "rx pktbuf's or not yet malloced.\n", len, chan));
				continue;
			}

			/* Validate data offset */
			if ((doff < SDPCM_HDRLEN) || (doff > len)) {
				DBUSERR(("%s (nextlen): bad data offset %d: HW len %d min %d\n",
				           __FUNCTION__, doff, len, SDPCM_HDRLEN));
				PKTFREE2();
				ASSERT(0);
				dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
				continue;
			}

			/* All done with this one -- now deliver the packet */
			goto deliver;
		}
		/* gSPI frames should not be handled in fractions */
		if (sdio_info->bus == SPI_BUS) {
			break;
		}

		/* Read frame header (hardware and software) */
		sdret = bcmsdh_recv_buf(sdio_info->sdh, SI_ENUM_BASE, SDIO_FUNC_2, F2SYNC,
		                        sdio_info->rxhdr, firstread, NULL, NULL, NULL);
		sdio_info->f2rxhdrs++;
		ASSERT(sdret != BCME_PENDING);

		if (sdret < 0) {
			DBUSERR(("%s: RXHEADER FAILED: %d\n", __FUNCTION__, sdret));
			sdio_info->rx_hdrfail++;
			dbus_sdio_rxfail(sdio_info, TRUE, TRUE);
			continue;
		}

		/* Extract hardware header fields */
		len = ltoh16_ua(sdio_info->rxhdr);
		check = ltoh16_ua(sdio_info->rxhdr + sizeof(uint16));

		/* All zeros means no more frames */
		if (!(len|check)) {
			*finished = TRUE;
			break;
		}

		/* Validate check bytes */
		if ((uint16)~(len^check)) {
			DBUSERR(("%s: HW hdr error: len/check 0x%04x/0x%04x\n",
			           __FUNCTION__, len, check));
			sdio_info->rx_badhdr++;
			dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
			continue;
		}

		/* Validate frame length */
		if (len < SDPCM_HDRLEN) {
			DBUSERR(("%s: HW hdr length invalid: %d\n", __FUNCTION__, len));
			continue;
		}

		/* Extract software header fields */
		chan = SDPCM_PACKET_CHANNEL(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);
		doff = SDPCM_DOFFSET_VALUE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);

		/* Validate data offset */
		if ((doff < SDPCM_HDRLEN) || (doff > len)) {
			DBUSERR(("%s: Bad data offset %d: HW len %d, min %d seq %d\n",
			           __FUNCTION__, doff, len, SDPCM_HDRLEN, seq));
			sdio_info->rx_badhdr++;
			ASSERT(0);
			dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
			continue;
		}

		/* Save the readahead length if there is one */
		sdio_info->nextlen = sdio_info->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((sdio_info->nextlen << 4) > MAX_RX_DATASZ) {
			DBUSINFO(("%s (nextlen): got frame w/nextlen too large (%d), seq %d\n",
			          __FUNCTION__, sdio_info->nextlen, seq));
			sdio_info->nextlen = 0;
		}

		/* Handle Flow Control */
		fcbits = SDPCM_FCMASK_VALUE(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN]);

		delta = 0;
		if (~sdio_info->flowcontrol & fcbits) {
			sdio_info->fc_xoff++;
			delta = 1;
		}
		if (sdio_info->flowcontrol & ~fcbits) {
			sdio_info->fc_xon++;
			delta = 1;
		}

		if (delta) {
			sdio_info->fc_rcvd++;
			sdio_info->flowcontrol = fcbits;
		}

		/* Check and update sequence number */
		if (rxseq != seq) {
			DBUSINFO(("%s: rx_seq %d, expected %d\n", __FUNCTION__, seq, rxseq));
			sdio_info->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((uint8)(txmax - sdio_info->tx_seq) > 0x40) {
			DBUSERR(("%s: got unlikely tx max %d with tx_seq %d\n",
			           __FUNCTION__, txmax, sdio_info->tx_seq));
			txmax = sdio_info->tx_seq + 2;
		}
		sdio_info->tx_max = txmax;

		/* Call a separate function for control frames */
		if (chan == SDPCM_CONTROL_CHANNEL) {
			dbus_sdio_read_control(sdio_info, sdio_info->rxhdr, len, doff);
			continue;
		}

		ASSERT((chan == SDPCM_DATA_CHANNEL) || (chan == SDPCM_EVENT_CHANNEL) ||
		       (chan == SDPCM_TEST_CHANNEL) || (chan == SDPCM_GLOM_CHANNEL));

		/* Length to read */
		rdlen = (len > firstread) ? (len - firstread) : 0;

		/* May pad read to blocksize for efficiency */
		if (sdio_info->roundup && sdio_info->blocksize && (rdlen > sdio_info->blocksize)) {
			pad = sdio_info->blocksize - (rdlen % sdio_info->blocksize);
			if ((pad <= sdio_info->roundup) && (pad < sdio_info->blocksize) &&
			    ((rdlen + pad + firstread) < MAX_RX_DATASZ))
				rdlen += pad;
		}

		/* Satisfy length-alignment requirements */
		if (forcealign && (rdlen & (ALIGNMENT - 1)))
			rdlen = ROUNDUP(rdlen, ALIGNMENT);

		if ((rdlen + firstread) > MAX_RX_DATASZ) {
			/* Too long -- skip this frame */
			DBUSERR(("%s: too long: len %d rdlen %d\n", __FUNCTION__, len, rdlen));
			sdio_info->pub->stats.rx_errors++; sdio_info->rx_toolong++;
			dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
			continue;
		}

		if (!(pkt = dbus_sdcb_pktget(sdio_info, (rdlen + firstread + SDALIGN), FALSE))) {
			/* Give up on data, request rtx of events */
			DBUSERR(("%s: dbus_sdio_pktget failed: rdlen %d chan %d\n",
			           __FUNCTION__, rdlen, chan));
			sdio_info->pub->stats.rx_dropped++;
			dbus_sdio_rxfail(sdio_info, FALSE, RETRYCHAN(chan));
			continue;
		}

		ASSERT(!PKTLINK(pkt));

		/* Leave room for what we already read, and align remainder */
		ASSERT(firstread < (PKTLEN(sdio_info->pub->osh, pkt)));
		PKTPULL(sdio_info->pub->osh, pkt, firstread);
		PKTALIGN(sdio_info->pub->osh, pkt, rdlen, SDALIGN);

		/* Read the remaining frame data */
		sdret = bcmsdh_recv_buf(sdh, SI_ENUM_BASE, SDIO_FUNC_2, F2SYNC,
		                        ((uint8 *)PKTDATA(osh, pkt)), rdlen, pkt, NULL, NULL);
		sdio_info->f2rxdata++;
		ASSERT(sdret != BCME_PENDING);

		if (sdret < 0) {
			DBUSERR(("%s: read %d %s bytes failed: %d\n", __FUNCTION__, rdlen,
			           ((chan == SDPCM_EVENT_CHANNEL) ? "event" :
			            ((chan == SDPCM_DATA_CHANNEL) ? "data" : "test")), sdret));
			dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
			sdio_info->pub->stats.rx_errors++;
			dbus_sdio_rxfail(sdio_info, TRUE, RETRYCHAN(chan));
			continue;
		}

		/* Copy the already-read portion */
		PKTPUSH(sdio_info->pub->osh, pkt, firstread);
		bcopy(sdio_info->rxhdr, PKTDATA(sdio_info->pub->osh, pkt), firstread);

deliver:
		/* Save superframe descriptor and allocate packet frame */
		if (chan == SDPCM_GLOM_CHANNEL) {
			if (SDPCM_GLOMDESC(&sdio_info->rxhdr[SDPCM_FRAMETAG_LEN])) {
				DBUSGLOM(("%s: got glom descriptor, %d bytes:\n",
				          __FUNCTION__, len));
				PKTSETLEN(sdio_info->pub->osh, pkt, len);
				ASSERT(doff == SDPCM_HDRLEN);
				PKTPULL(sdio_info->pub->osh, pkt, SDPCM_HDRLEN);
				sdio_info->glomd = pkt;
			} else {
				DBUSERR(("%s: glom superframe w/o descriptor!\n", __FUNCTION__));
				dbus_sdio_rxfail(sdio_info, FALSE, FALSE);
			}
			continue;
		}

		/* Fill in packet len and prio, deliver upward */
		PKTSETLEN(sdio_info->pub->osh, pkt, len);
		PKTPULL(sdio_info->pub->osh, pkt, doff);

#ifdef SDTEST
		/* Test channel packets are processed separately */
		if (chan == SDPCM_TEST_CHANNEL) {
			dbus_sdio_testrcv(sdio_info, pkt, seq);
			continue;
		}
#endif /* SDTEST */

		if (PKTLEN(sdio_info->pub->osh, pkt) == 0) {
			dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
			continue;
		}

		rxirb = (dbus_irb_rx_t *) dbus_sdcb_getirb(sdio_info, FALSE);
		if (rxirb != NULL) {
			rxirb->pkt = pkt;
			dbus_sdio_recv_irb_complete(sdio_info, rxirb, DBUS_OK);
		} else {
			DBUSERR(("ERROR: failed to get rx irb\n"));
			dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
		}
	}

	DBUSDATA(("%s: processed %d frames\n", __FUNCTION__, (maxframes - rxleft)));

	/* Back off rxseq if awaiting rtx, upate rx_seq */
	if (sdio_info->rxskip)
		rxseq--;
	sdio_info->rx_seq = rxseq;

	return (maxframes - rxleft);
}

static uint32
dbus_sdio_hostmail(sdio_info_t *sdio_info)
{
	sdpcmd_regs_t *regs = sdio_info->regs;
	uint32 intstatus = 0;
	uint32 hmb_data;
	uint8 fcbits;
	uint retries = 0;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	/* Read mailbox data and ack that we did so */
	R_SDREG(hmb_data, &regs->tohostmailboxdata, retries);
	if (retries <= retry_limit)
		W_SDREG(SMB_INT_ACK, &regs->tosbmailbox, retries);
	sdio_info->f1regdata += 2;

	/* Dongle recomposed rx frames, accept them again */
	if (hmb_data & HMB_DATA_NAKHANDLED) {
		DBUSINFO(("Dongle reports NAK handled, expect rtx of %d\n", sdio_info->rx_seq));
		if (!sdio_info->rxskip) {
			DBUSERR(("%s: unexpected NAKHANDLED!\n", __FUNCTION__));
		}
		sdio_info->rxskip = FALSE;
		intstatus |= I_HMB_FRAME_IND;
	}

	/*
	 * Not using DEVREADY or FWREADY at the moment; just print.
	 * DEVREADY does not occur with gSPI.
	 */
	if (hmb_data & (HMB_DATA_DEVREADY | HMB_DATA_FWREADY)) {
		sdio_info->sdpcm_ver = (hmb_data & HMB_DATA_VERSION_MASK) >> HMB_DATA_VERSION_SHIFT;
		if (sdio_info->sdpcm_ver != SDPCM_PROT_VERSION)
			DBUSERR(("Version mismatch, dongle reports %d, expecting %d\n",
			           sdio_info->sdpcm_ver, SDPCM_PROT_VERSION));
		else
			DBUSINFO(("Dongle ready, protocol version %d\n", sdio_info->sdpcm_ver));
	}

	/*
	 * Flow Control has been moved into the RX headers and this out of band
	 * method isn't used any more.  Leae this here for possibly remaining backward
	 * compatible with older dongles
	 */
	if (hmb_data & HMB_DATA_FC) {
		fcbits = (hmb_data & HMB_DATA_FCDATA_MASK) >> HMB_DATA_FCDATA_SHIFT;

		if (fcbits & ~sdio_info->flowcontrol)
			sdio_info->fc_xoff++;
		if (sdio_info->flowcontrol & ~fcbits)
			sdio_info->fc_xon++;

		sdio_info->fc_rcvd++;
		sdio_info->flowcontrol = fcbits;
	}

	/* Shouldn't be any others */
	if (hmb_data & ~(HMB_DATA_DEVREADY |
	                 HMB_DATA_NAKHANDLED |
	                 HMB_DATA_FC |
	                 HMB_DATA_FWREADY |
	                 HMB_DATA_FCDATA_MASK |
	                 HMB_DATA_VERSION_MASK)) {
		DBUSERR(("Unknown mailbox data content: 0x%02x\n", hmb_data));
	}

	return intstatus;
}

#ifndef BCM_DNGL_EMBEDIMAGE
static void
dbus_sdh_devrdy_isr(void *handle)
{
	probe_sdh_info_t *pinfo = handle;
	bcmsdh_info_t *sdh = pinfo->sdh;
	uint32 intstatus = 0, hmb_data = 0;

	if (pinfo->devready == FALSE) {
		intstatus = R_REG(pinfo->osh, &pinfo->chinfo->sdregs->intstatus);
		if (intstatus & I_HMB_HOST_INT) {
			hmb_data = R_REG(pinfo->osh, &pinfo->chinfo->sdregs->tohostmailboxdata);
			if (hmb_data & (HMB_DATA_DEVREADY|HMB_DATA_FWREADY)) {
				bcmsdh_intr_disable(sdh);
				pinfo->devready = TRUE;
				dbus_sdos_sched_probe_cb();

			}
		}
	}
}
#endif /* BCM_DNGL_EMBEDIMAGE */

static void
dbus_sdh_isr(void *handle)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;
	bool wantdpc;

	ASSERT(sdio_info);
	ASSERT(sdio_info->sdh);

	if (dbus_sdio_isr(sdio_info, &wantdpc) == TRUE) {
		bcmsdh_intr_disable(sdio_info->sdh);
		sdio_info->intdis = TRUE;
	}
}

static bool
dbus_sdio_isr(void *handle, bool *wantdpc)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;
	bool handle_int = FALSE;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));


	/*
	 * NOTE for NDIS:
	 *
	 * Do not use spinlock in isr() to share
	 * resources with other lower priority functions
	 * because isr() runs at DIRQL which can preempt
	 * them and cause race condition/deadlock.
	 * To share resources with isr() use NdisMSynchronizeWithInterrupt()
	 * Functions that indirectly use spinlock bcmsdh_reg_read(),
	 * bcmsdh_intr_disable(), etc.
	 */

	ASSERT(sdio_info);

	*wantdpc = FALSE;

	/* Count the interrupt call */
	sdio_info->intrcount++;
	sdio_info->ipend = TRUE;

	/* Shouldn't get this interrupt if we're sleeping? */
	if (sdio_info->sleeping) {
		DBUSERR(("INTERRUPT WHILE SLEEPING??\n"));
		handle_int = FALSE;
		goto err;
	}

	/* Disable additional interrupts (is this needed now)? */
	if (sdio_info->intr) {
		DBUSINTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
	} else {
		DBUSERR(("dbus_sdio_isr() w/o interrupt configured!\n"));
	}

	sdio_info->intdis = TRUE;

	dbus_sdos_sched_dpc(sdio_info);
	sdio_info->dpc_sched = TRUE;
	*wantdpc = TRUE;

	handle_int = TRUE;
err:
	return handle_int;
}

#ifdef SDTEST
static void
dbus_sdio_pktgen_init(sdio_info_t *sdio_info)
{
	/* Default to specified length, or full range */
	if (dhd_pktgen_len) {
		sdio_info->pktgen_maxlen = MIN(dhd_pktgen_len, MAX_PKTGEN_LEN);
		sdio_info->pktgen_minlen = sdio_info->pktgen_maxlen;
	} else {
		sdio_info->pktgen_maxlen = MAX_PKTGEN_LEN;
		sdio_info->pktgen_minlen = 0;
	}
	sdio_info->pktgen_len = (uint16)sdio_info->pktgen_minlen;

	/* Default to per-watchdog burst with 10s print time */
	sdio_info->pktgen_freq = 1;
	sdio_info->pktgen_print = 10000/dhd_watchdog_ms;
	sdio_info->pktgen_count = (dhd_pktgen * dhd_watchdog_ms + 999) / 1000;

	/* Default to echo mode */
	sdio_info->pktgen_mode = DHD_PKTGEN_ECHO;
	sdio_info->pktgen_stop = 1;
}

static void
dbus_sdio_pktgen(sdio_info_t *sdio_info)
{
	void *pkt;
	uint8 *data;
	uint pktcount;
	uint fillbyte;
	uint16 len;

	/* Display current count if appropriate */
	if (sdio_info->pktgen_print && (++sdio_info->pktgen_ptick >= sdio_info->pktgen_print)) {
		sdio_info->pktgen_ptick = 0;
		printf("%s: send attempts %d rcvd %d\n",
		       __FUNCTION__, sdio_info->pktgen_sent, sdio_info->pktgen_rcvd);
	}

	/* For recv mode, just make sure dongle has started sending */
	if (sdio_info->pktgen_mode == DHD_PKTGEN_RECV) {
		if (!sdio_info->pktgen_rcvd)
			dbus_sdio_sdtest_set(sdio_info, TRUE);
		return;
	}

	/* Otherwise, generate or request the specified number of packets */
	for (pktcount = 0; pktcount < sdio_info->pktgen_count; pktcount++) {
		/* Stop if total has been reached */
		if (sdio_info->pktgen_total &&
			(sdio_info->pktgen_sent >= sdio_info->pktgen_total)) {
			sdio_info->pktgen_count = 0;
			break;
		}

		/* Allocate an appropriate-sized packet */
		len = sdio_info->pktgen_len;
		if (!(pkt = dbus_sdcb_pktget(sdio_info,
			(len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + SDALIGN), TRUE))) {
			DBUSERR(("%s: dbus_sdio_pktget failed!\n", __FUNCTION__));
			break;
		}
		PKTALIGN(sdio_info->pub->osh, pkt,
			(len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN), SDALIGN);
		data = (uint8*)PKTDATA(sdio_info->pub->osh, pkt) + SDPCM_HDRLEN;

		/* Write test header cmd and extra based on mode */
		switch (sdio_info->pktgen_mode) {
		case DHD_PKTGEN_ECHO:
			*data++ = SDPCM_TEST_ECHOREQ;
			*data++ = (uint8)sdio_info->pktgen_sent;
			break;

		case DHD_PKTGEN_SEND:
			*data++ = SDPCM_TEST_DISCARD;
			*data++ = (uint8)sdio_info->pktgen_sent;
			break;

		case DHD_PKTGEN_RXBURST:
			*data++ = SDPCM_TEST_BURST;
			*data++ = (uint8)sdio_info->pktgen_count;
			break;

		default:
			DBUSERR(("Unrecognized pktgen mode %d\n", sdio_info->pktgen_mode));
			dbus_sdcb_pktfree(sdio_info, pkt, TRUE);
			sdio_info->pktgen_count = 0;
			return;
		}

		/* Write test header length field */
		*data++ = (len >> 0);
		*data++ = (len >> 8);

		/* Then fill in the remainder -- N/A for burst, but who cares... */
		for (fillbyte = 0; fillbyte < len; fillbyte++)
			*data++ = SDPCM_TEST_FILL(fillbyte, (uint8)sdio_info->pktgen_sent);

		/* Send it */
		if (dbus_sdio_txpkt(sdio_info, pkt, SDPCM_TEST_CHANNEL)) {
			sdio_info->pktgen_fail++;
			if (sdio_info->pktgen_stop &&
				sdio_info->pktgen_stop == sdio_info->pktgen_fail)
				sdio_info->pktgen_count = 0;
		}
		sdio_info->pktgen_sent++;

		/* Bump length if not fixed, wrap at max */
		if (++sdio_info->pktgen_len > sdio_info->pktgen_maxlen)
			sdio_info->pktgen_len = (uint16)sdio_info->pktgen_minlen;

		/* Special case for burst mode: just send one request! */
		if (sdio_info->pktgen_mode == DHD_PKTGEN_RXBURST)
			break;
	}
}

static void
dbus_sdio_sdtest_set(sdio_info_t *sdio_info, bool start)
{
	void *pkt;
	uint8 *data;

	/* Allocate the packet */
	if (!(pkt = dbus_sdcb_pktget(sdio_info,
		SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + SDALIGN, TRUE))) {
		DBUSERR(("%s: dbus_sdio_pktget failed!\n", __FUNCTION__));
		return;
	}
	PKTALIGN(sdio_info->pub->osh, pkt, (SDPCM_HDRLEN + SDPCM_TEST_HDRLEN), SDALIGN);
	data = (uint8*)PKTDATA(sdio_info->pub->osh, pkt) + SDPCM_HDRLEN;

	/* Fill in the test header */
	*data++ = SDPCM_TEST_SEND;
	*data++ = start;
	*data++ = (sdio_info->pktgen_maxlen >> 0);
	*data++ = (sdio_info->pktgen_maxlen >> 8);

	/* Send it */
	if (dbus_sdio_txpkt(sdio_info, pkt, SDPCM_TEST_CHANNEL))
		sdio_info->pktgen_fail++;
}


static void
dbus_sdio_testrcv(sdio_info_t *sdio_info, void *pkt, uint seq)
{
	osl_t *osh;
	uint8 *data;
	uint pktlen;
	uint8 cmd;
	uint8 extra;
	uint16 len;
	uint16 offset;

	osh = sdio_info->pub->osh;

	/* Check for min length */
	if ((pktlen = PKTLEN(sdio_info->pub->osh, pkt)) < SDPCM_TEST_HDRLEN) {
		DBUSERR(("dbus_sdio_restrcv: toss runt frame, pktlen %d\n", pktlen));
		dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
		return;
	}

	/* Extract header fields */
	data = PKTDATA(sdio_info->pub->osh, pkt);
	cmd = *data++;
	extra = *data++;
	len = *data++; len += *data++ << 8;

	/* Check length for relevant commands */
	if (cmd == SDPCM_TEST_DISCARD || cmd == SDPCM_TEST_ECHOREQ || cmd == SDPCM_TEST_ECHORSP) {
		if (pktlen != len + SDPCM_TEST_HDRLEN) {
			DBUSERR(("dbus_sdio_testrcv: frame length mismatch, pktlen %d seq %d"
			           " cmd %d extra %d len %d\n", pktlen, seq, cmd, extra, len));
			dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
			return;
		}
	}

	/* Process as per command */
	switch (cmd) {
	case SDPCM_TEST_ECHOREQ:
		/* Rx->Tx turnaround ok (even on NDIS w/current implementation) */
		*(uint8 *)(PKTDATA(sdio_info->pub->osh, pkt)) = SDPCM_TEST_ECHORSP;
		if (dbus_sdio_txpkt(sdio_info, pkt, SDPCM_TEST_CHANNEL) == 0) {
			sdio_info->pktgen_sent++;
		} else {
			sdio_info->pktgen_fail++;
			dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
		}
		sdio_info->pktgen_rcvd++;
		break;

	case SDPCM_TEST_ECHORSP:
		if (sdio_info->ext_loop) {
			dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
			sdio_info->pktgen_rcvd++;
			break;
		}

		for (offset = 0; offset < len; offset++, data++) {
			if (*data != SDPCM_TEST_FILL(offset, extra)) {
				DBUSERR(("dbus_sdio_testrcv: echo data mismatch: "
				           "offset %d (len %d) expect 0x%02x rcvd 0x%02x\n",
				           offset, len, SDPCM_TEST_FILL(offset, extra), *data));
				break;
			}
		}
		dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
		sdio_info->pktgen_rcvd++;
		break;

	case SDPCM_TEST_DISCARD:
		dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
		sdio_info->pktgen_rcvd++;
		break;

	case SDPCM_TEST_BURST:
	case SDPCM_TEST_SEND:
	default:
		DBUSINFO(("dbus_sdio_testrcv: unsupported or unknown command, pktlen %d seq %d"
		          " cmd %d extra %d len %d\n", pktlen, seq, cmd, extra, len));
		dbus_sdcb_pktfree(sdio_info, pkt, FALSE);
		break;
	}

	/* For recv mode, stop at limie (and tell dongle to stop sending) */
	if (sdio_info->pktgen_mode == DHD_PKTGEN_RECV) {
		if (sdio_info->pktgen_total &&
			(sdio_info->pktgen_rcvd >= sdio_info->pktgen_total)) {
			sdio_info->pktgen_count = 0;
			dbus_sdio_sdtest_set(sdio_info, FALSE);
		}
	}
}
#endif /* SDTEST */

#ifdef SDTEST
static int
dbus_sdio_pktgen_get(sdio_info_t *sdio_info, uint8 *arg)
{
	dhd_pktgen_t pktgen;

	pktgen.version = DHD_PKTGEN_VERSION;
	pktgen.freq = sdio_info->pktgen_freq;
	pktgen.count = sdio_info->pktgen_count;
	pktgen.print = sdio_info->pktgen_print;
	pktgen.total = sdio_info->pktgen_total;
	pktgen.minlen = sdio_info->pktgen_minlen;
	pktgen.maxlen = sdio_info->pktgen_maxlen;
	pktgen.numsent = sdio_info->pktgen_sent;
	pktgen.numrcvd = sdio_info->pktgen_rcvd;
	pktgen.numfail = sdio_info->pktgen_fail;
	pktgen.mode = sdio_info->pktgen_mode;
	pktgen.stop = sdio_info->pktgen_stop;

	bcopy(&pktgen, arg, sizeof(pktgen));

	return 0;
}

static int
dbus_sdio_pktgen_set(sdio_info_t *sdio_info, uint8 *arg)
{
	dhd_pktgen_t pktgen;
	uint oldcnt, oldmode;

	bcopy(arg, &pktgen, sizeof(pktgen));
	if (pktgen.version != DHD_PKTGEN_VERSION)
		return BCME_BADARG;

	oldcnt = sdio_info->pktgen_count;
	oldmode = sdio_info->pktgen_mode;

	sdio_info->pktgen_freq = pktgen.freq;
	sdio_info->pktgen_count = pktgen.count;
	sdio_info->pktgen_print = pktgen.print;
	sdio_info->pktgen_total = pktgen.total;
	sdio_info->pktgen_minlen = pktgen.minlen;
	sdio_info->pktgen_maxlen = pktgen.maxlen;
	sdio_info->pktgen_mode = pktgen.mode;
	sdio_info->pktgen_stop = pktgen.stop;

	sdio_info->pktgen_tick = sdio_info->pktgen_ptick = 0;
	sdio_info->pktgen_len = MAX(sdio_info->pktgen_len, sdio_info->pktgen_minlen);
	sdio_info->pktgen_len = MIN(sdio_info->pktgen_len, sdio_info->pktgen_maxlen);

	/* Clear counts for a new pktgen (mode change, or was stopped) */
	if (sdio_info->pktgen_count && (!oldcnt || oldmode != sdio_info->pktgen_mode))
		sdio_info->pktgen_sent = sdio_info->pktgen_rcvd = sdio_info->pktgen_fail = 0;

	return 0;
}
#endif /* SDTEST */

static int
dbus_sdio_membytes(probe_sdh_info_t *pinfo, bool write, uint32 address, uint8 *data, uint size)
{
	int bcmerror = 0;
	uint32 sdaddr;
	uint dsize;
	bcmsdh_info_t *sdh;

	ASSERT(pinfo->sdh);
	sdh = pinfo->sdh;

	/* Determine initial transfer parameters */
	sdaddr = address & SBSDIO_SB_OFT_ADDR_MASK;
	if ((sdaddr + size) & SBSDIO_SBWINDOW_MASK)
		dsize = (SBSDIO_SB_OFT_ADDR_LIMIT - sdaddr);
	else
		dsize = size;

	/* Set the backplane window to include the start address */
	if ((bcmerror = dbus_sdio_set_siaddr_window(sdh, address))) {
		DBUSERR(("%s: window change failed\n", __FUNCTION__));
		goto xfer_done;
	}

	/* Do the transfer(s) */
	while (size) {
		DBUSINFO(("%s: %s %d bytes at offset 0x%08x in window 0x%08x\n",
		          __FUNCTION__, (write ? "write" : "read"), dsize, sdaddr,
		          (address & SBSDIO_SBWINDOW_MASK)));
		if ((bcmerror = bcmsdh_rwdata(sdh, write, sdaddr, data, dsize))) {
			DBUSERR(("%s: membytes transfer failed\n", __FUNCTION__));
			break;
		}

		/* Adjust for next transfer (if any) */
		if ((size -= dsize)) {
			data += dsize;
			address += dsize;
			if ((bcmerror = dbus_sdio_set_siaddr_window(sdh, address))) {
				DBUSERR(("%s: window change failed\n", __FUNCTION__));
				break;
			}
			sdaddr = 0;
			dsize = MIN(SBSDIO_SB_OFT_ADDR_LIMIT, size);
		}
	}

xfer_done:
	/* Return the window to backplane enumeration space for core access */
	if (dbus_sdio_set_siaddr_window(sdh, SI_ENUM_BASE)) {
		DBUSERR(("%s: FAILED to return to SI_ENUM_BASE\n", __FUNCTION__));
	}

	return bcmerror;
}

static int
dbus_sdio_downloadvars(probe_sdh_info_t *pinfo, void *arg, int len)
{
	int bcmerror = BCME_OK;

	if (!len) {
		bcmerror = BCME_BUFTOOSHORT;
		goto err;
	}

	if (pinfo->vars) {
		MFREE(pinfo->osh, pinfo->vars, pinfo->varsz);
		pinfo->vars = NULL;
		pinfo->varsz = 0;
	}
	pinfo->vars = MALLOC(pinfo->osh, len);
	pinfo->varsz = pinfo->vars ? len : 0;
	if (pinfo->vars == NULL) {
		pinfo->varsz = 0;
		bcmerror = BCME_NOMEM;
		goto err;
	}
	bcopy(arg, pinfo->vars, pinfo->varsz);
err:
	return bcmerror;
}

static int
dbus_sdio_doiovar(sdio_info_t *sdio_info, const bcm_iovar_t *vi, uint32 actionid, const char *name,
                void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;
	bool bool_val;

	DBUSTRACE(("%s: Enter, action %d name %s params %p plen %d arg %p len %d val_size %d\n",
	           __FUNCTION__, actionid, name, params, plen, arg, len, val_size));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	bool_val = (int_val != 0) ? TRUE : FALSE;


	/* Some ioctls use the bus */
	/* Check if dongle is in reset. If so, only allow DEVRESET iovars */
	if (sdio_info->dongle_reset && !(actionid == IOV_SVAL(IOV_DEVRESET) ||
	                                actionid == IOV_GVAL(IOV_DEVRESET))) {
		bcmerror = BCME_NOTREADY;
		goto exit;
	}

	/* Handle sleep stuff before any clock mucking */
	if (vi->varid == IOV_SLEEP) {
		if (IOV_ISSET(actionid)) {
			bcmerror = dbus_sdio_bussleep(sdio_info, bool_val);
		} else {
			int_val = (int32)sdio_info->sleeping;
			bcopy(&int_val, arg, val_size);
		}
		goto exit;
	}

	/* Request clock to allow SDIO accesses */
	if (!sdio_info->dongle_reset) {
		BUS_WAKE(sdio_info);
		dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);
	}

	switch (actionid) {
	case IOV_GVAL(IOV_INTR):
		int_val = (int32)sdio_info->intr;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_INTR):
		sdio_info->intr = bool_val;
		sdio_info->intdis = FALSE;

		/* FIX: Change to use busstate instead of up flag */
		if (sdio_info->up) {
			if (sdio_info->intr) {
				DBUSINTR(("%s: enable SDIO device interrupts\n", __FUNCTION__));
				bcmsdh_intr_enable(sdio_info->sdh);
			} else {
				DBUSINTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
				bcmsdh_intr_disable(sdio_info->sdh);
			}
		}
		break;

	case IOV_GVAL(IOV_POLLRATE):
		int_val = (int32)sdio_info->pollrate;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_POLLRATE):
		sdio_info->pollrate = (uint)int_val;
		sdio_info->poll = (sdio_info->pollrate != 0);
		break;

	case IOV_GVAL(IOV_IDLETIME):
		int_val = sdio_info->idletime;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLETIME):
		if ((int_val < 0) && (int_val != IDLE_IMMEDIATE)) {
			bcmerror = BCME_BADARG;
		} else {
			sdio_info->idletime = int_val;
		}
		break;

	case IOV_GVAL(IOV_IDLECLOCK):
		int_val = (int32)sdio_info->idleclock;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLECLOCK):
		sdio_info->idleclock = int_val;
		break;

	case IOV_GVAL(IOV_SD1IDLE):
		int_val = (int32)sd1idle;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SD1IDLE):
		sd1idle = bool_val;
		break;

	case IOV_SVAL(IOV_MEMBYTES):
	case IOV_GVAL(IOV_MEMBYTES):
	{
		uint32 address;
		uint size, dsize;
		uint8 *data;

		bool set = (actionid == IOV_SVAL(IOV_MEMBYTES));

		ASSERT(plen >= 2*sizeof(int));

		address = (uint32)int_val;
		bcopy((char *)params + sizeof(int_val), &int_val, sizeof(int_val));
		size = (uint)int_val;

		/* Do some validation */
		dsize = set ? plen - (2 * sizeof(int)) : len;
		if (dsize < size) {
			DBUSERR(("%s: error on %s membytes, addr 0x%08x size %d dsize %d\n",
			           __FUNCTION__, (set ? "set" : "get"), address, size, dsize));
			bcmerror = BCME_BADARG;
			break;
		}

		DBUSINFO(("%s: Request to %s %d bytes at address 0x%08x\n", __FUNCTION__,
		          (set ? "write" : "read"), size, address));

		/* If we know about SOCRAM, check for a fit */
		if ((sdio_info->orig_ramsize) &&
		    ((address > sdio_info->orig_ramsize) ||
			(address + size > sdio_info->orig_ramsize))) {
			DBUSERR(("%s: ramsize 0x%08x doesn't have %d bytes at 0x%08x\n",
			           __FUNCTION__, sdio_info->orig_ramsize, size, address));
			bcmerror = BCME_BADARG;
			break;
		}

		/* Generate the actual data pointer */
		data = set ? (uint8*)params + 2 * sizeof(int): (uint8*)arg;

		/* Call to do the transfer */
		bcmerror = dbus_sdio_membytes(&g_probe_info, set, address, data, size);

		break;
	}

	case IOV_GVAL(IOV_MEMSIZE):
		int_val = (int32)sdio_info->ramsize;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_SDIOD_DRIVE):
		int_val = (int32)dhd_sdiod_drive_strength;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDIOD_DRIVE):
		dhd_sdiod_drive_strength = int_val;
		si_sdiod_drive_strength_init(sdio_info->sih,
			sdio_info->pub->osh, dhd_sdiod_drive_strength);
		break;

	case IOV_SVAL(IOV_DOWNLOAD):
		bcmerror = dbus_sdio_download_state(&g_probe_info, bool_val);

#ifndef BCM_DNGL_EMBEDIMAGE
		if ((bool_val == FALSE) && (delay_eth == 0)) {
			g_probe_info.devready = TRUE;
			sdio_info->pub->busstate = DBUS_STATE_DL_DONE;
		}
#endif
		break;

	case IOV_SVAL(IOV_VARS):
		bcmerror = dbus_sdio_downloadvars(&g_probe_info, arg, len);
		break;

	case IOV_GVAL(IOV_READAHEAD):
		int_val = (int32)dhd_readahead;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_READAHEAD):
		if (bool_val && !dhd_readahead)
			sdio_info->nextlen = 0;
		dhd_readahead = bool_val;
		break;

	case IOV_GVAL(IOV_SDRXCHAIN):
		int_val = (int32)sdio_info->use_rxchain;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDRXCHAIN):
		if (bool_val && !sdio_info->sd_rxchain)
			bcmerror = BCME_UNSUPPORTED;
		else
			sdio_info->use_rxchain = bool_val;
		break;

	case IOV_GVAL(IOV_ALIGNCTL):
		int_val = (int32)dhd_alignctl;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_ALIGNCTL):
		dhd_alignctl = bool_val;
		break;

	case IOV_GVAL(IOV_SDALIGN):
		int_val = SDALIGN;
		bcopy(&int_val, arg, val_size);
		break;




#ifdef SDTEST
	case IOV_GVAL(IOV_EXTLOOP):
		int_val = (int32)sdio_info->ext_loop;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_EXTLOOP):
		sdio_info->ext_loop = bool_val;
		break;

	case IOV_GVAL(IOV_PKTGEN):
		bcmerror = dbus_sdio_pktgen_get(sdio_info, arg);
		break;

	case IOV_SVAL(IOV_PKTGEN):
		bcmerror = dbus_sdio_pktgen_set(sdio_info, arg);
		break;
#endif /* SDTEST */


	case IOV_SVAL(IOV_DEVRESET):
		DBUSTRACE(("%s: Called set IOV_DEVRESET=%d dongle_reset=%d busstate=%d\n",
		           __FUNCTION__, bool_val, sdio_info->dongle_reset,
		           sdio_info->pub->busstate));

		ASSERT(sdio_info->pub->osh);

		/* FIX: Need to change to support async probe callback.
		 */
		DBUSERR(("DEVRESET unsupported for async probe callback \n"));
		break;

		if (bool_val == TRUE) {
			if (sdio_info->dongle_reset)
				break;
			/* Expect app to have torn down any connection before calling */
			/* Stop the bus, disable F2 */
			dbus_bus_stop(sdio_info);

			/* Release tx/rx buffer, detach from the dongle */
			dbus_sdio_release_dongle(sdio_info, sdio_info->pub->osh);
			dbus_sdio_probe_deinit(&g_probe_info);

			sdio_info->dongle_reset = TRUE;
			sdio_info->up = FALSE;

			DBUSTRACE(("%s:  WLAN OFF DONE\n", __FUNCTION__));
			/* App can now remove power from device */
		} else {
			/* App must have restored power to device before calling */

			DBUSTRACE(("\n\n%s: == WLAN ON ==\n", __FUNCTION__));

			if (!sdio_info->dongle_reset) {
				bcmerror = BCME_NOTDOWN;
				DBUSERR(("%s: Set DEVRESET=FALSE invoked when device is on\n",
				           __FUNCTION__));
				break;
			}

			/* Turn on WLAN */
			/* Reset SD client */
			bcmsdh_reset(sdio_info->sdh);

			/* Attempt to re-attach & download */
			if (dbus_sdio_probe_init(&g_probe_info)) {
				/* Attempt to download binary to the dongle */
				if ((dbus_sdio_attach_init(sdio_info, sdio_info->sdh,
					sdio_info->firmware_path, sdio_info->nvram_path))) {
					/* Re-init bus, enable F2 transfer */
					dbus_sdio_init(sdio_info);

					sdio_info->dongle_reset = FALSE;
					sdio_info->up = TRUE;
					DBUSTRACE(("%s: == WLAN ON DONE ===\n",
					           __FUNCTION__));
				} else
					bcmerror = BCME_SDIO_ERROR;
			} else
				bcmerror = BCME_SDIO_ERROR;
		}
		break;

	case IOV_GVAL(IOV_DEVRESET):
		DBUSTRACE(("%s: Called get IOV_DEVRESET\n", __FUNCTION__));

		/* Get its status */
		int_val = (bool) sdio_info->dongle_reset;
		bcopy(&int_val, arg, val_size);

		break;

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	if ((sdio_info->idletime == IDLE_IMMEDIATE) && !sdio_info->dpc_sched) {
		sdio_info->activity = FALSE;
		dbus_sdio_clkctl(sdio_info, CLK_NONE, TRUE);
	}

	return bcmerror;
}

static int
dbus_sdio_write_vars(probe_sdh_info_t *pinfo)
{
	int bcmerror = 0;
	uint32 varsize;
	uint32 varaddr;
	char *vbuffer;
	uint32 varsizew;

	if (!pinfo->varsz || !pinfo->vars)
		return BCME_OK;

	varsize = ROUNDUP(pinfo->varsz, 4);
	varaddr = (pinfo->ramsize - 4) - varsize;

	vbuffer = (char*)MALLOC(pinfo->osh, varsize);
	if (!vbuffer)
		return BCME_NOMEM;

	bzero(vbuffer, varsize);
	bcopy(pinfo->vars, vbuffer, pinfo->varsz);

	/* Write the vars list */
	bcmerror = dbus_sdio_membytes(pinfo, TRUE, varaddr, vbuffer, varsize);

	MFREE(pinfo->osh, vbuffer, varsize);

	/* adjust to the user specified RAM */
	DBUSINFO(("origram size is %d and used ramsize is %d, vars are at %d, orig varsize is %d\n",
		pinfo->orig_ramsize, pinfo->ramsize, varaddr, varsize));
	varsize = ((pinfo->orig_ramsize - 4) - varaddr);
	varsizew = varsize >> 2;
	DBUSINFO(("new varsize is %d, varsizew is %d\n", varsize, varsizew));

	/* Write the length to the last word */
	if (bcmerror) {
		varsizew = 0;
		DBUSINFO(("bcmerror : Varsizew is being written as %d\n", varsizew));
		dbus_sdio_membytes(pinfo, TRUE, (pinfo->orig_ramsize - 4), (uint8*)&varsizew, 4);
	} else {
		DBUSINFO(("Varsize is %d and varsizew is %d\n", varsize, varsizew));
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		varsizew = htol32(varsizew);
		DBUSINFO(("Varsizew is 0x%x and htol is 0x%x\n", varsizew, htol32(varsizew)));
		bcmerror = dbus_sdio_membytes(pinfo, TRUE, (pinfo->orig_ramsize - 4),
			(uint8*)&varsizew, 4);
	}

	return bcmerror;
}

static int
dbus_sdio_download_state(probe_sdh_info_t *pinfo, bool enter)
{
	int bcmerror = 0;
	si_t *sih;

	ASSERT(pinfo->sih);
	ASSERT(pinfo->sdh);

	sih = pinfo->sih;

	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {

		pinfo->alp_only = TRUE;

		if (!(si_setcore(sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(sih, ARMCM3_CORE_ID, 0))) {
			DBUSERR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		si_core_disable(sih, 0);
		if (bcmsdh_regfail(pinfo->sdh)) {
			DBUSERR(("%s: Failed to disable ARM core!\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		if (!(si_setcore(sih, SOCRAM_CORE_ID, 0))) {
			DBUSERR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		si_core_reset(sih, 0, 0);
		if (bcmsdh_regfail(pinfo->sdh)) {
			DBUSERR(("%s: Failure trying reset SOCRAM core?\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		/* Clear the top bit of memory */
		if (pinfo->ramsize) {
			uint32 zeros = 0;
			dbus_sdio_membytes(pinfo, TRUE, pinfo->ramsize - 4, (uint8*)&zeros, 4);
		}
	} else {
		if (!(si_setcore(sih, SOCRAM_CORE_ID, 0))) {
			DBUSERR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		if (!si_iscoreup(sih)) {
			DBUSERR(("%s: SOCRAM core is down after reset?\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		if ((bcmerror = dbus_sdio_write_vars(pinfo))) {
			DBUSERR(("%s: could not write vars to ram\n", __FUNCTION__));
			goto fail;
		}

		if (!si_setcore(sih, PCMCIA_CORE_ID, 0) &&
		    !si_setcore(sih, SDIOD_CORE_ID, 0)) {
			DBUSERR(("%s: Can't change back to SDIO core?\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}
		W_REG(pinfo->osh, &pinfo->chinfo->sdregs->intstatus, 0xFFFFFFFF);


		if (!(si_setcore(sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(sih, ARMCM3_CORE_ID, 0))) {
			DBUSERR(("%s: Failed to find ARM core!\n", __FUNCTION__));
			bcmerror = BCME_ERROR;
			goto fail;
		}

		si_core_reset(sih, 0, 0);
		if (bcmsdh_regfail(pinfo->sdh)) {
			DBUSERR(("%s: Failure trying to reset ARM core?\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		/* Allow HT Clock now that the ARM is running. */
		pinfo->alp_only = FALSE;
	}

fail:
	/* Always return to SDIOD core */
	if (!si_setcore(sih, PCMCIA_CORE_ID, 0))
		si_setcore(sih, SDIOD_CORE_ID, 0);

	return bcmerror;
}

static int
dbus_iovar_process(sdio_info_t *sdio_info, const char *name,
                 void *params, int plen, void *arg, int len, bool set)
{
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	uint32 actionid;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	/* Look up var locally; if not found pass to host driver */
	if ((vi = bcm_iovar_lookup(dbus_sdio_iovars, name)) == NULL) {
		BUS_WAKE(sdio_info);

		/* Turn on clock in case SD command needs backplane */
		dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);

		bcmerror = bcmsdh_iovar_op(sdio_info->sdh, name, params, plen, arg, len, set);

		/* Check for bus configuration changes of interest */

		/* If it was divisor change, read the new one */
		if (set && strcmp(name, "sd_divisor") == 0) {
			if (bcmsdh_iovar_op(sdio_info->sdh, "sd_divisor", NULL, 0,
				&sdio_info->sd_divisor, sizeof(int32), FALSE) != BCME_OK) {
				sdio_info->sd_divisor = -1;
				DBUSERR(("%s: fail on %s get\n", __FUNCTION__, name));
			} else {
				DBUSINFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, name, sdio_info->sd_divisor));
			}
		}
		/* If it was a mode change, read the new one */
		if (set && strcmp(name, "sd_mode") == 0) {
			if (bcmsdh_iovar_op(sdio_info->sdh, "sd_mode", NULL, 0,
			                    &sdio_info->sd_mode, sizeof(int32), FALSE) != BCME_OK) {
				sdio_info->sd_mode = -1;
				DBUSERR(("%s: fail on %s get\n", __FUNCTION__, name));
			} else {
				DBUSINFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, name, sdio_info->sd_mode));
			}
		}
		/* Similar check for blocksize change */
		if (set && strcmp(name, "sd_blocksize") == 0) {
			int32 fnum = 2;
			if (bcmsdh_iovar_op(sdio_info->sdh, "sd_blocksize", &fnum, sizeof(int32),
				&sdio_info->blocksize, sizeof(int32), FALSE) != BCME_OK) {
				sdio_info->blocksize = 0;
				DBUSERR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
			} else {
				DBUSINFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, "sd_blocksize", sdio_info->blocksize));
			}
		}
		sdio_info->roundup = MIN(max_roundup, sdio_info->blocksize);

		if ((sdio_info->idletime == IDLE_IMMEDIATE) && !sdio_info->dpc_sched) {
			sdio_info->activity = FALSE;
			dbus_sdio_clkctl(sdio_info, CLK_NONE, TRUE);
		}

		goto exit;
	}

	DBUSCTL(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
	         name, (set ? "set" : "get"), len, plen));

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	bcmerror = dbus_sdio_doiovar(sdio_info, vi, actionid,
		name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

static int
dbus_sdio_txctlq_process(void *bus)
{
	sdio_info_t *sdio_info = bus;
	int err = DBUS_OK;

	if (sdio_info->txctl_req.pending == TRUE) {
		if (sdio_info->txctl_req.is_iovar == FALSE) {
			ASSERT(sdio_info->txctl_req.ctl.buf);
			ASSERT(sdio_info->txctl_req.ctl.len);

			err = dbus_sdio_txctl(sdio_info, sdio_info->txctl_req.ctl.buf,
				sdio_info->txctl_req.ctl.len);
		} else {
			err = dbus_iovar_process(sdio_info,
				sdio_info->txctl_req.iovar.name,
				sdio_info->txctl_req.iovar.params,
				sdio_info->txctl_req.iovar.plen,
				sdio_info->txctl_req.iovar.arg,
				sdio_info->txctl_req.iovar.len,
				sdio_info->txctl_req.iovar.set);
		}

		bzero(&sdio_info->txctl_req, sizeof(sdio_info->txctl_req));
		dbus_sdio_ctl_complete(sdio_info, DBUS_CBCTL_WRITE, err);
	}

	return err;
}

static void
dbus_sdio_txq_flush(sdio_info_t *sdio_info)
{
	int prec_out;
	struct exec_parms exec_args;
	pkttag_t *ptag;
	void *pkt;

	exec_args.pdeq.sdio_info = sdio_info;
	exec_args.pdeq.tx_prec_map = ALLPRIO;
	exec_args.pdeq.prec_out = &prec_out;

	/* Cancel all pending pkts */
	while ((pkt = dbus_sdos_exec_txlock(sdio_info,
		(exec_cb_t) dbus_prec_pkt_deq_exec, &exec_args)) != NULL) {
		ptag = (pkttag_t *) PKTTAG(pkt);
		ASSERT(ptag);

		dbus_sdio_send_irb_complete(sdio_info, ptag->txirb, DBUS_STATUS_CANCELLED);
		dbus_sdcb_pktfree(sdio_info, pkt, TRUE);
	}
}

int
dbus_sdio_txq_process(void *bus)
{
	sdio_info_t *sdio_info = bus;
	bcmsdh_info_t *sdh;
	uint framecnt = 0;		  /* Temporary counter of tx/rx frames */
	uint txlimit = dhd_txbound; /* Tx frames to send before resched */

	dbus_sdos_lock(sdio_info);

	sdh = sdio_info->sdh;

	if (sdio_info->pub->busstate == DBUS_STATE_DOWN) {
		dbus_sdio_txq_flush(sdio_info);
		goto exit;
	}

	/* Send ctl requests first */
	dbus_sdio_txctlq_process(sdio_info);

	/* If waiting for HTAVAIL, check status */
	if (sdio_info->clkstate == CLK_PENDING) {
		int err;
		uint8 clkctl, devctl = 0;

		/* Read CSR, if clock on switch to AVAIL, else ignore */
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DBUSERR(("%s: error reading CSR: %d\n", __FUNCTION__, err));
			sdio_info->pub->busstate = DBUS_STATE_DOWN;
		}

		DBUSINFO(("DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n", devctl, clkctl));

		if (SBSDIO_HTAV(clkctl)) {
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DBUSERR(("%s: error reading DEVCTL: %d\n",
				           __FUNCTION__, err));
				sdio_info->pub->busstate = DBUS_STATE_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			if (err) {
				DBUSERR(("%s: error writing DEVCTL: %d\n",
				           __FUNCTION__, err));
				sdio_info->pub->busstate = DBUS_STATE_DOWN;
			}
			sdio_info->clkstate = CLK_AVAIL;
		}
		else {
			goto exit;
		}
	}

	BUS_WAKE(sdio_info);

	/* Make sure backplane clock is on */
	dbus_sdio_clkctl(sdio_info, CLK_AVAIL, TRUE);
	if (sdio_info->clkstate == CLK_PENDING)
		goto exit;

	/* Send queued frames (limit 1 if rx may still be pending) */
	if ((sdio_info->clkstate != CLK_PENDING) && !sdio_info->fcstate &&
	    pktq_mlen(&sdio_info->txq, ~sdio_info->flowcontrol) && txlimit && DATAOK(sdio_info)) {
		framecnt = dbus_sdio_sendfromq(sdio_info, txlimit);
	}

	/* FIX: Check ctl requests again
	 * It's possible to have ctl request while dbus_sdio_sendfromq()
	 * is active.  Possibly check for pending ctl requests before sending
	 * each pkt??
	 */
	dbus_sdio_txctlq_process(sdio_info);

#ifdef SDTEST
	/* Generate packets if configured */
	if (sdio_info->pktgen_count && (++sdio_info->pktgen_tick >= sdio_info->pktgen_freq)) {
		/* Make sure backplane clock is on */
		dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);
		sdio_info->pktgen_tick = 0;
		dbus_sdio_pktgen(sdio_info);
	}
#endif

exit:
	dbus_sdos_unlock(sdio_info);

	return DBUS_OK;
}

static int
probe_htclk(probe_sdh_info_t *pinfo)
{
	int err = 0;
	uint8 clkctl;
	bcmsdh_info_t *sdh;

	sdh = pinfo->sdh;

	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		(SBSDIO_ALP_AVAIL_REQ | SBSDIO_HT_AVAIL_REQ), &err);
	if (err) {
		DBUSERR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
		return BCME_ERROR;
	}

	clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (err) {
		DBUSERR(("%s: HT Avail read error: %d\n", __FUNCTION__, err));
		return BCME_ERROR;
	}

	if (!SBSDIO_HTAV(clkctl)) {
		SPINWAIT(((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, &err)),
			!SBSDIO_HTAV(clkctl)), PMU_MAX_TRANSITION_DLY);
	}

	return err;
}

int
probe_dlstart()
{
	int err;
	uint8 clkctl;

	/* Need at least ALP */
	clkctl = bcmsdh_cfg_read(g_probe_info.sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!SBSDIO_ALPAV(clkctl))
		err = probe_htclk(&g_probe_info);

	dbus_sdio_download_state(&g_probe_info, TRUE);
	g_probe_info.dl_addr = 0;
	return 0;
}

int
probe_dlstop()
{
	dbus_sdio_download_state(&g_probe_info, FALSE);
	g_probe_info.dl_addr = 0;
	return 0;
}

int
probe_dlwrite(uint8 *buf, int count, bool isvars)
{
	if (isvars)
		dbus_sdio_downloadvars(&g_probe_info, buf, count);
	else {
		dbus_sdio_membytes(&g_probe_info, TRUE, g_probe_info.dl_addr, buf, count);
		g_probe_info.dl_addr += count;
	}

	return 0;
}

/*
 * Download iovars
 *
 * This handles iovars before dbus_attach() and
 * before bringing up eth interface
 */
int
probe_iovar(const char *name, void *params, int plen, void *arg, int len, bool set,
	void **val, int *val_len)
{
	int actionid, err = 0;
	int32 int_val = 0;
	bool bool_val;
	uint8 clkctl;
	const bcm_iovar_t *vi = NULL;

	if (name)
		vi = bcm_iovar_lookup(dbus_sdio_iovars, (char *) name);

	if (vi == NULL) {
		DBUSERR(("Unsupported probe iovar: %s\n", name));
		return -1;
	}

	bcopy(params, &int_val, sizeof(int_val));
	bool_val = (int_val != 0) ? TRUE : FALSE;

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);

	/* Need at least ALP */
	clkctl = bcmsdh_cfg_read(g_probe_info.sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!SBSDIO_ALPAV(clkctl))
		err = probe_htclk(&g_probe_info);

	/* Handle pre-attach() requests */
	switch (actionid) {
		case IOV_SVAL(IOV_DOWNLOAD):
			err = dbus_sdio_download_state(&g_probe_info, bool_val);
			g_probe_info.dl_addr = 0;
		break;
		case IOV_SVAL(IOV_MEMBYTES): {
			uint32 address;
			uint size;
			char *image;

			address = (uint32)int_val;
			g_probe_info.dl_addr = address;

			bcopy((char *)params + sizeof(int_val), &int_val, sizeof(int_val));
			size = (uint)int_val; /* in 2048 (2K) chunks */

			image = (char *)params + sizeof(int_val) + sizeof(int_val);
			dbus_sdio_membytes(&g_probe_info, TRUE, address, image, size);
		} break;
		case IOV_SVAL(IOV_VARS):
			/* FIX: Need vars len in iovar string */
		break;
		case IOV_GVAL(IOV_MEMSIZE):
			*val = (void *)&g_probe_info.ramsize;
			*val_len = sizeof(uint32);
		break;
		default:
			DBUSERR(("Pre-attach probe actionid (%d) unsupported\n", actionid));
		break;
	}

	return err;
}


static uint
dbus_sdio_sendfromq(sdio_info_t *sdio_info, uint maxframes)
{
	void *pkt;
	int ret = 0, prec_out;
	uint cnt = 0;
	uint datalen;
	uint8 tx_prec_map;
	struct exec_parms exec_args;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	tx_prec_map = ~sdio_info->flowcontrol;

	/* Send frames until the limit or some other event */
	for (cnt = 0; (cnt < maxframes) && DATAOK(sdio_info); cnt++) {
		exec_args.pdeq.sdio_info = sdio_info;
		exec_args.pdeq.tx_prec_map = tx_prec_map;
		exec_args.pdeq.prec_out = &prec_out;
		pkt = dbus_sdos_exec_txlock(sdio_info,
			(exec_cb_t) dbus_prec_pkt_deq_exec, &exec_args);
		if (pkt == NULL)
			break;

		datalen = PKTLEN(sdio_info->pub->osh, pkt) - SDPCM_HDRLEN;

#ifndef SDTEST
		ret = dbus_sdio_txpkt(sdio_info, pkt, SDPCM_DATA_CHANNEL);
#else
		ret = dbus_sdio_txpkt(sdio_info, pkt,
			(sdio_info->ext_loop ? SDPCM_TEST_CHANNEL : SDPCM_DATA_CHANNEL));
#endif
		if (ret) {
			sdio_info->pub->stats.tx_errors++;
			if (sdio_info->pub->busstate == DBUS_STATE_DOWN)
				break;
		}
	}

	return cnt;
}

static int
dbus_sdio_txctl(sdio_info_t *sdio_info, uchar *msg, uint msglen)
{
	uint8 *frame;
	uint16 len, pad;
	uint32 swheader;
	uint retries = 0;
	bcmsdh_info_t *sdh = sdio_info->sdh;
	uint8 doff = 0;
	int ret = 0;
	int i;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (sdio_info->dongle_reset)
		return DBUS_ERR;

	/* Back the pointer to make a room for bus header */
	frame = msg - SDPCM_HDRLEN;
	len = (msglen += SDPCM_HDRLEN);

	/* Add alignment padding (optional for ctl frames) */
	if (dhd_alignctl) {
		if ((doff = ((uintptr)frame % SDALIGN))) {
			frame -= doff;
			len += doff;
			msglen += doff;
			bzero(frame, doff + SDPCM_HDRLEN);
		}
		ASSERT(doff < SDALIGN);
	}
	doff += SDPCM_HDRLEN;

	/* Round send length to next SDIO block */
	if (sdio_info->roundup && sdio_info->blocksize && (len > sdio_info->blocksize)) {
		pad = sdio_info->blocksize - (len % sdio_info->blocksize);
		if ((pad <= sdio_info->roundup) && (pad < sdio_info->blocksize))
			len += pad;
	}

	/* Satisfy length-alignment requirements */
	if (forcealign && (len & (ALIGNMENT - 1)))
		len = ROUNDUP(len, ALIGNMENT);

	ASSERT(ISALIGNED((uintptr)frame, 2));

	/* Need to lock here to protect txseq and SDIO tx calls */
	BUS_WAKE(sdio_info);

	/* Make sure backplane clock is on */
	dbus_sdio_clkctl(sdio_info, CLK_AVAIL, FALSE);

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	*(uint16*)frame = htol16((uint16)msglen);
	*(((uint16*)frame) + 1) = htol16(~msglen);

	/* Software tag: channel, sequence number, data offset */
	swheader = ((SDPCM_CONTROL_CHANNEL << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK)
	        | sdio_info->tx_seq | ((doff << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
	htol32_ua_store(swheader, frame + SDPCM_FRAMETAG_LEN);
	htol32_ua_store(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));
	sdio_info->tx_seq = (sdio_info->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

	do {
		ret = bcmsdh_send_buf(sdh, SI_ENUM_BASE, SDIO_FUNC_2, F2SYNC,
		                      frame, len, NULL, NULL, NULL);
		ASSERT(ret != BCME_PENDING);

		if (ret < 0) {
			/* On failure, abort the command and terminate the frame */
			DBUSINFO(("%s: sdio error %d, abort command and terminate frame.\n",
			          __FUNCTION__, ret));
			sdio_info->tx_sderrs++;

			ret = bcmsdh_abort(sdh, SDIO_FUNC_2);
			if (ret == BCME_NODEVICE) {
				dbus_sdio_state_change(sdio_info, DBUS_STATE_DISCONNECT);
				break;
			}

			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL,
			                 SFC_WF_TERM, NULL);
			sdio_info->f1regdata++;

			for (i = 0; i < 3; i++) {
				uint8 hi, lo;
				hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCHI, NULL);
				lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				                     SBSDIO_FUNC1_WFRAMEBCLO, NULL);
				sdio_info->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}
		}
	} while ((ret < 0) && retries++ < TXRETRIES);

	if ((sdio_info->idletime == IDLE_IMMEDIATE) && !sdio_info->dpc_sched) {
		sdio_info->activity = FALSE;
		dbus_sdio_clkctl(sdio_info, CLK_NONE, TRUE);
	}

	if (ret)
		sdio_info->tx_ctlerrs++;
	else
		sdio_info->tx_ctlpkts++;

	return ret ? DBUS_ERR : DBUS_OK;
}

static int
dbus_sdio_rxctl(sdio_info_t *sdio_info, uchar *msg, uint msglen)
{
	uint rxlen = 0;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (sdio_info->dongle_reset)
		return DBUS_ERR;

	/* FIX: Since rxctl() is async, need to fix case where ctl pkt is recevied
	 * before this function is called.  We need to buffer incoming ctl pkts.
	 */
	rxlen = sdio_info->rxlen;
	bcopy(sdio_info->rxctl, msg, MIN(msglen, rxlen));
	sdio_info->rxlen = 0;

	return DBUS_OK;
}




static void *
dbus_sdh_probe(uint16 venid, uint16 devid, uint16 bus_no, uint16 slot,
	uint16 func, uint bustype, void *regsva, osl_t * osh,
	void *sdh)
{
	int err;
	void *prarg;
	uint8 clkctl = 0;

	DBUSTRACE(("%s\n", __FUNCTION__));

	prarg = NULL;
	/* We make assumptions about address window mappings */
	ASSERT((uintptr)regsva == SI_ENUM_BASE);

	/* BCMSDH passes venid and devid based on CIS parsing -- but low-power start
	 * means early parse could fail, so here we should get either an ID
	 * we recognize OR (-1) indicating we must request power first.
	 */
	/* Check the Vendor ID */
	switch (venid) {
		case 0x0000:
		case VENDOR_BROADCOM:
			break;
		default:
			DBUSERR(("%s: unknown vendor: 0x%04x\n",
			           __FUNCTION__, venid));
			return NULL;
			break;
	}

	if (devid == 0)
		devid = bcmsdh_reg_read(sdh, SI_ENUM_BASE, 4) & CID_ID_MASK;

	/* Check the Device ID and make sure it's one that we support */
	switch (devid) {
		case BCM4325_CHIP_ID:
		case BCM4325_D11DUAL_ID:		/* 4325 802.11a/g id */
		case BCM4325_D11G_ID:			/* 4325 802.11g 2.4Ghz band id */
		case BCM4325_D11A_ID:			/* 4325 802.11a 5Ghz band id */
			DBUSERR(("%s: found 4325 Dongle\n", __FUNCTION__));
			g_probe_info.chinfo =  &chipinfo_4325_15;
			break;
		case BCM4329_D11N_ID:		/* 4329 802.11n dualband device */
		case BCM4329_D11N2G_ID:		/* 4329 802.11n 2.4G device */
		case BCM4329_D11N5G_ID:		/* 4329 802.11n 5G device */
		case BCM4321_D11N2G_ID:
			DBUSERR(("%s: found 4329 Dongle\n", __FUNCTION__));
			g_probe_info.chinfo =  &chipinfo_4329;
			break;
		case BCM4315_CHIP_ID:
		case BCM4315_D11DUAL_ID:		/* 4315 802.11a/g id */
		case BCM4315_D11G_ID:			/* 4315 802.11g id */
		case BCM4315_D11A_ID:			/* 4315 802.11a id */
			DBUSINFO(("%s: found 4315 Dongle\n", __FUNCTION__));
			g_probe_info.chinfo =  &chipinfo_4325_15;
			break;
		case BCM4336_D11N_ID:
			DBUSINFO(("%s: found 4336 Dongle\n", __FUNCTION__));
			g_probe_info.chinfo =  &chipinfo_4336;
			break;

		case BCM4330_D11N_ID:
			DBUSINFO(("%s: found 4330 Dongle\n", __FUNCTION__));
			g_probe_info.chinfo =  &chipinfo_4330;
			break;

		case BCM43237_D11N_ID:
			DBUSINFO(("%s: found 43237 Dongle\n", __FUNCTION__));
			g_probe_info.chinfo =  &chipinfo_43237;
			break;

		case 0:
			DBUSINFO(("%s: allow device id 0, will check chip internals\n",
			          __FUNCTION__));
			/* FIX: Need to query chip */
			g_probe_info.chinfo =  &chipinfo_4325_15;
			break;

		default:
			DBUSERR(("%s: skipping 0x%04x/0x%04x, not a dongle\n",
			           __FUNCTION__, venid, devid));
			return NULL;
			break;
	}

	if (osh == NULL) {
		/* FIX: This osh is needed for si_attach() and R_REG()
		 * If we simplify and do away with si_attach() at this stage,
		 * then remove this as well.
		 */
		/* FIX: Linux needs this, but not NDIS
		 * Remove this LINUX define;  Don't put
		 * OS defines in this file.
		 * Have DBUS maintain it's own osh and remove it from
		 * dbus_attach() as an argument.
		 */
		osh = osl_attach(NULL, SD_BUSTYPE, TRUE);
		g_probe_info.free_probe_osh = TRUE;
	}
	ASSERT(osh);

	g_probe_info.venid = venid;
	g_probe_info.devid = devid;
	g_probe_info.bus_no = bus_no;
	g_probe_info.slot = slot;
	g_probe_info.func = func;
	g_probe_info.bustype = bustype;
	g_probe_info.regsva = regsva;
	g_probe_info.osh = osh;
	g_probe_info.sdh = sdh;
	g_probe_info.firmware_file = NULL;
	g_probe_info.nvram_file = NULL;

	ASSERT(g_probe_info.chinfo);
	g_probe_info.ramsize = g_probe_info.orig_ramsize = g_probe_info.chinfo->socram_size;

	/* Force PLL off until si_attach() programs PLL control regs */



	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, DHD_INIT_CLKCTL1, &err);
	if (!err)
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);

	if (err || ((clkctl & ~SBSDIO_AVBITS) != DHD_INIT_CLKCTL1)) {
		DBUSERR(("dbus_sdio_probe: ChipClkCSR access: err %d wrote 0x%02x read 0x%02x\n",
			err, DHD_INIT_CLKCTL1, clkctl));
		return NULL;
	}

	/* The si_attach() will provide an SI handle, scan the 
	 * backplane, and initialize the PLL.
	 */
	if (!(g_probe_info.sih = si_attach((uint)devid, osh, regsva, SD_BUSTYPE, sdh,
	                           &g_probe_info.vars, &g_probe_info.varsz))) {
		DBUSERR(("%s: si_attach failed!\n", __FUNCTION__));
		return NULL;
	}

	ASSERT(g_probe_info.orig_ramsize == si_socram_size(g_probe_info.sih));

	/* FIX: this is needed on some boards for download.  If not, it can
	 * cause data errors if drive strength is not correct.
	 * Default is 10mA, but 6mA is optimal.
	 */
	si_sdiod_drive_strength_init(g_probe_info.sih, osh, dhd_sdiod_drive_strength);


	/* prepare dongle for download */
	if (!(dbus_sdio_probe_init(&g_probe_info))) {
		DBUSERR(("%s: dbus_sdio_probe_init failed\n", __FUNCTION__));
		return NULL;
	}

	/* Set up the interrupt mask */
	W_REG(osh, &g_probe_info.chinfo->sdregs->hostintmask, HOSTINTMASK);

#ifdef BCM_DNGL_EMBEDIMAGE
	prarg = dbus_sdio_probe_cb(&g_probe_info, "", SD_BUSTYPE, SDPCM_RESERVE);

	if (prarg != NULL)
		return &g_probe_info;
	else
		return NULL;
#else
	dbus_sdio_alpclk(sdh);

	if (delay_eth == 0) {
		dbus_sdio_probe_cb(&g_probe_info, "", SD_BUSTYPE, SDPCM_RESERVE);
	} else {
		DBUSERR(("Delay eth1 bringup\n"));
		/*
		 * Enable interrupt for DEVREADY when a valid image is downloaded
		 */
		bcmsdh_intr_disable(sdh);

		if ((err = bcmsdh_intr_reg(sdh, dbus_sdh_devrdy_isr, &g_probe_info)) != 0) {
			DBUSERR(("%s: FAILED: bcmsdh_intr_reg returned %d\n",
				__FUNCTION__, err));
		}

		bcmsdh_intr_enable(sdh);
	}

	return &g_probe_info;
#endif /* BCM_DNGL_EMBEDIMAGE */
}

static void
dbus_sdh_disconnect(void *ptr)
{
	probe_sdh_info_t *pinfo = (probe_sdh_info_t *)ptr;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (pinfo == NULL) {
		DBUSERR(("%s: pinfo is NULL\n", __FUNCTION__));
		return;
	}
	dbus_sdio_disconnect_cb(NULL);

	/* After this point, sdio_info is free'd;
	 * Clean up stuff from dbus_sdh_probe()
	 */
	dbus_sdio_probe_deinit(pinfo);

	if (pinfo->sih) {
		si_detach(pinfo->sih);
		if (pinfo->vars && pinfo->varsz) {
			MFREE(pinfo->osh, pinfo->vars, pinfo->varsz);
		}
	}

	if (pinfo->osh && (pinfo->free_probe_osh == TRUE)) {
		if (MALLOCED(pinfo->osh)) {
			DBUSERR(("%s: PROBE MEMORY LEAK %d bytes\n", __FUNCTION__,
				MALLOCED(pinfo->osh)));
		}
		osl_detach(pinfo->osh);
	}
}

static bool
dbus_sdio_probe_init(probe_sdh_info_t *pinfo)
{
	bcmsdh_info_t	*sdh;	/* Handle for BCMSDH calls */
	osl_t *osh;

	DBUSTRACE(("%s\n", __FUNCTION__));

	ASSERT(pinfo);
	ASSERT(pinfo->sdh);
	ASSERT(pinfo->osh);

	sdh = pinfo->sdh;
	osh = pinfo->osh;

	pinfo->alp_only = TRUE;
	pinfo->devready = FALSE;


	/* Set core control so an SDIO reset does a backplane reset */
	OR_REG(osh, &pinfo->chinfo->sdregs->corecontrol, CC_BPRESEN);

	return TRUE;

	return FALSE;
}

static void
dbus_sdio_probe_deinit(probe_sdh_info_t *pinfo)
{
	int err;

	ASSERT(pinfo);
	/* FIX: Not consolidating this with dbus_sdh_disconnect
	 * because it's used during DEVRESET.  Need to resolve.
	 */
	if (pinfo->sih) {
		bcmsdh_cfg_write(pinfo->sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_HT_AVAIL_REQ, &err);
		si_watchdog(pinfo->sih, 4);
		bcmsdh_cfg_write(pinfo->sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, 0, &err);
	}
}


bool
dbus_sdio_attach_init(sdio_info_t *sdio_info, void *sdh, char *firmware_path,
                       char * nvram_path)
{
	int ret;
	int32 fnum;

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

#ifdef SDTEST
	dbus_sdio_pktgen_init(sdio_info);
#endif /* SDTEST */

	/* Disable F2 to clear any intermediate frame state on the dongle */
	bcmsdh_cfg_write(sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);

	/* Done with backplane-dependent accesses, can drop clock... */
	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);
	sdio_info->clkstate = CLK_SDONLY;

	/* Query the SD clock speed */
	if (bcmsdh_iovar_op(sdh, "sd_divisor", NULL, 0,
	                    &sdio_info->sd_divisor, sizeof(int32), FALSE) != BCME_OK) {
		DBUSERR(("%s: fail on %s get\n", __FUNCTION__, "sd_divisor"));
		sdio_info->sd_divisor = -1;
	} else {
		DBUSINFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_divisor", sdio_info->sd_divisor));
	}

	/* Query the SD bus mode */
	if (bcmsdh_iovar_op(sdh, "sd_mode", NULL, 0,
	                    &sdio_info->sd_mode, sizeof(int32), FALSE) != BCME_OK) {
		DBUSERR(("%s: fail on %s get\n", __FUNCTION__, "sd_mode"));
		sdio_info->sd_mode = -1;
	} else {
		DBUSINFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_mode", sdio_info->sd_mode));
	}

	/* Query the F2 block size, set roundup accordingly */
	fnum = 2;
	if (bcmsdh_iovar_op(sdh, "sd_blocksize", &fnum, sizeof(int32),
	                    &sdio_info->blocksize, sizeof(int32), FALSE) != BCME_OK) {
		sdio_info->blocksize = 0;
		DBUSERR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
	} else {
		DBUSINFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_blocksize", sdio_info->blocksize));
	}
	sdio_info->roundup = MIN(max_roundup, sdio_info->blocksize);

	/* Query if bus module supports packet chaining, default to use if supported */
	if (bcmsdh_iovar_op(sdh, "sd_rxchain", NULL, 0,
	                    &sdio_info->sd_rxchain, sizeof(int32), FALSE) != BCME_OK) {
		sdio_info->sd_rxchain = FALSE;
	} else {
		DBUSINFO(("%s: bus module (through bcmsdh API) %s chaining\n",
		          __FUNCTION__, (sdio_info->sd_rxchain ? "supports" : "does not support")));
	}
	sdio_info->use_rxchain = (bool)sdio_info->sd_rxchain;

	/* Register interrupt callback, but mask it (not operational yet). */
	DBUSINTR(("%s: disable SDIO interrupts (not interested yet)\n", __FUNCTION__));
	bcmsdh_intr_disable(sdh);
	if ((ret = bcmsdh_intr_reg(sdh, dbus_sdh_isr, sdio_info)) != 0) {
		DBUSERR(("%s: FAILED: bcmsdh_intr_reg returned %d\n",
		           __FUNCTION__, ret));
		goto fail;
	}
	DBUSINTR(("%s: registered SDIO interrupt function ok\n", __FUNCTION__));

	return TRUE;

fail:
	return FALSE;
}

static void
dbus_sdio_release(sdio_info_t *sdio_info, osl_t *osh)
{
	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (sdio_info) {
		ASSERT(osh);
		dbus_sdio_release_dongle(sdio_info, osh);
	}
}

static void
dbus_sdio_release_dongle(sdio_info_t *sdio_info, osl_t *osh)
{
	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (sdio_info->dongle_reset)
		return;

	if (sdio_info->rxbuf) {
		MFREE(osh, sdio_info->rxbuf, sdio_info->rxblen);
		sdio_info->rxctl = sdio_info->rxbuf = NULL;
		sdio_info->rxlen = 0;
	}

	if (sdio_info->databuf) {
		MFREE(osh, sdio_info->databuf, MAX_DATA_BUF);
		sdio_info->databuf = NULL;
	}
}

#ifdef BCM_DNGL_EMBEDIMAGE
int
dhd_bus_download_image_array(probe_sdh_info_t *pinfo, char * nvram_path, uint8 *fw, int len)
{
	int bcmerror = -1;
	int offset = 0;

	/* Download image */
	while ((offset + MEMBLOCK) < len) {
		bcmerror = dbus_sdio_membytes(pinfo, TRUE,
			offset, fw + offset, MEMBLOCK);
		if (bcmerror) {
			DBUSERR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

	if (offset < len) {
		bcmerror = dbus_sdio_membytes(pinfo, TRUE, offset,
			fw + offset, len - offset);
		if (bcmerror) {
			DBUSERR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, len - offset, offset));
			goto err;
		}
	}

	/* Download SROM if provided externally through file */
	dhd_bus_download_nvram_file(pinfo, nvram_path);
err:
	return bcmerror;
}
#endif /* BCM_DNGL_EMBEDIMAGE */


/* 
 * ProcessVars:Takes a buffer of "<var>=<value>\n" lines read from a file and ending in a NUL.
 * Removes carriage returns, empty lines, comment lines, and converts newlines to NULs.
 * Shortens buffer as needed and pads with NULs.  End of buffer is marked by two NULs.
*/

#if defined(BCM_DNGL_EMBEDIMAGE)
int
dhd_bus_download_nvram_file(probe_sdh_info_t *pinfo, char * nvram_path)
{
	int bcmerror = -1;
	uint len = 0;
	void * image = NULL;
	uint8 * memblock = NULL;
	char *bufp;

	if (!nvram_path[0])
		return 0;

	/* FIX: Need to implement dhd_os_open_image() */
	/* image = dhd_os_open_image(nvram_path); */
	if (image == NULL)
		goto err;

	memblock = MALLOC(pinfo->osh, MEMBLOCK);
	if (memblock == NULL) {
		DBUSERR(("%s: Failed to allocate memory %d bytes\n",
		           __FUNCTION__, MEMBLOCK));
		goto err;
	}

	/* Download variables */
	/* FIX: Need to implement dhd_os_get_image_block() */
	/* len = dhd_os_get_image_block(memblock, MEMBLOCK, image); */

	if (len != MEMBLOCK && len > 0) {
		bufp = (char *)memblock;
		len = process_nvram_vars(bufp, len);
		if (len % 4)
			len += (4 - len % 4);
		bufp += len;
		*bufp++ = 0;
		if (len)
			bcmerror = dbus_sdio_downloadvars(pinfo, memblock, len + 1);
		if (bcmerror) {
			DBUSERR(("%s: error downloading vars: %d\n",
			           __FUNCTION__, bcmerror));
		}
	} else {
		DBUSERR(("%s: error reading nvram file: %d\n",
		           __FUNCTION__, len));
		bcmerror = BCME_SDIO_ERROR;
	}

err:
	if (memblock)
		MFREE(pinfo->osh, memblock, MEMBLOCK);

	/* FIX: Need to implement dhd_os_close_image() */

	return bcmerror;
}
#endif /* BCM_DNGL_EMBEDIMAGE */

static void
dbus_sdos_lock(sdio_info_t *sdio_info)
{
	if (sdio_info == NULL)
		return;

	if (sdio_info->drvintf && sdio_info->drvintf->lock)
		sdio_info->drvintf->lock(sdio_info->sdos_info);
	else
		ASSERT(0);

	ASSERT(sdio_info->sdlocked == FALSE);
	sdio_info->sdlocked = TRUE;
}

static void
dbus_sdos_unlock(sdio_info_t *sdio_info)
{
	ASSERT(sdio_info->sdlocked == TRUE);
	sdio_info->sdlocked = FALSE;

	if (sdio_info->drvintf && sdio_info->drvintf->unlock)
		sdio_info->drvintf->unlock(sdio_info->sdos_info);
	else
		ASSERT(0);
}

static void *
dbus_sdos_exec_txlock(sdio_info_t *sdio_info, exec_cb_t cb, struct exec_parms *args)
{
	ASSERT(cb);
	if (sdio_info->drvintf && sdio_info->drvintf->exec_txlock)
		return sdio_info->drvintf->exec_txlock(sdio_info->sdos_info, cb, args);

	return NULL;
}

static void *
dbus_sdcb_pktget(sdio_info_t *sdio_info, uint len, bool send)
{
	void *p = NULL;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (sdio_info == NULL)
		return NULL;

	if (sdio_info->cbs && sdio_info->cbs->pktget)
		p = sdio_info->cbs->pktget(sdio_info->cbarg, len, send);

	return p;
}

static void
dbus_sdcb_pktfree(sdio_info_t *sdio_info, void *p, bool send)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->pktfree)
		sdio_info->cbs->pktfree(sdio_info->cbarg, p, send);
}

static dbus_irb_t *
dbus_sdcb_getirb(sdio_info_t *sdio_info, bool send)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (sdio_info == NULL)
		return NULL;

	if (sdio_info->cbs && sdio_info->cbs->getirb)
		return sdio_info->cbs->getirb(sdio_info->cbarg, send);

	return NULL;
}

/*
 * Interface functions
 */
static int
dbus_sdif_send_irb(void *bus, dbus_irb_tx_t *txirb)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err;

	if (sdio_info == NULL)
		return DBUS_ERR;

	err = dbus_sdio_txbuf_submit(sdio_info, txirb);
	if (err != DBUS_OK) {
		err = DBUS_ERR_TXFAIL;
	}

	return err;
}

static int
dbus_sdif_send_ctl(void *bus, uint8 *buf, int len)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err = DBUS_OK;

	if (sdio_info == NULL)
		return DBUS_ERR;

	if (sdio_info->txctl_req.pending == TRUE) {
		DBUSERR(("%s: ctl is pending!\n", __FUNCTION__));
		return DBUS_ERR_PENDING;
	}

	sdio_info->txctl_req.ctl.buf = buf;
	sdio_info->txctl_req.ctl.len = len;
	sdio_info->txctl_req.is_iovar = FALSE;
	sdio_info->txctl_req.pending = TRUE;
	dbus_sdio_txq_sched(sdio_info->sdos_info);
	return err;
}

static int
dbus_sdif_recv_ctl(void *bus, uint8 *buf, int len)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);

	if (sdio_info == NULL)
		return DBUS_ERR;

	if (sdio_info->rxctl_req.pending == TRUE) {
		DBUSERR(("%s: ctl is pending!\n", __FUNCTION__));
		return DBUS_ERR_PENDING;
	}

	/* Do have a rxctl pkt available? */
	if (sdio_info->rxlen > 0) {
		dbus_sdio_rxctl(sdio_info, buf, len);
		dbus_sdio_ctl_complete(sdio_info, DBUS_CBCTL_READ, DBUS_OK);
	} else {
		sdio_info->rxctl_req.ctl.buf = buf;
		sdio_info->rxctl_req.ctl.len = len;
		sdio_info->rxctl_req.pending = TRUE;
	}
	return DBUS_OK;
}

static int
dbus_sdif_up(void *bus)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err = DBUS_ERR;

	if (sdio_info == NULL)
		return DBUS_ERR;

	if (sdio_info->drvintf && sdio_info->drvintf->up) {
		err = sdio_info->drvintf->up(sdio_info->sdos_info);
	}

	dbus_sdos_lock(sdio_info);
	err = dbus_sdio_init(sdio_info);
	if (err != 0)
		err = DBUS_ERR;
	dbus_sdos_unlock(sdio_info);

	return err;
}

static int
dbus_sdif_iovar_op(void *bus, const char *name,
	void *params, int plen, void *arg, int len, bool set)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err = DBUS_OK;

	if (sdio_info == NULL)
		return DBUS_ERR;

	err = dbus_iovar_process(sdio_info, name, params, plen, arg, len, set);
	return err;
}

static bool
dbus_sdif_device_exists(void *bus)
{
	return TRUE;
}

static bool
dbus_sdif_dlneeded(void *bus)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);

	if (sdio_info == NULL)
		return FALSE;

#ifdef BCM_DNGL_EMBEDIMAGE
	return (g_probe_info.devready == FALSE);
#else
	return FALSE;
#endif
}

static int
dbus_sdif_dlstart(void *bus, uint8 *fw, int len)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err = DBUS_ERR;

	if (sdio_info == NULL)
		return DBUS_ERR;

	dbus_sdio_alpclk(g_probe_info.sdh);
	sdio_info->clkstate = CLK_AVAIL;

	/* Put ARM in reset for download */
	err = dbus_sdio_download_state(&g_probe_info, TRUE);
	if (err) {
		DBUSERR(("%s: error placing ARM core in reset\n", __FUNCTION__));
		err = DBUS_ERR;
		goto err;
	}

	/* FIX: Which embedded image has priority?
	 */
#ifdef BCM_DNGL_EMBEDIMAGE
	if (dhd_bus_download_image_array(&g_probe_info, nv_path, fw, len)) {
		DBUSERR(("%s: dongle image download failed\n", __FUNCTION__));
		err = DBUS_ERR;
		goto err;
	}
#endif /* BCM_DNGL_EMBEDIMAGE */

	/* FIX: Skip this for now
	 * If above succeeds, do we still download this one?
	 */

	err = DBUS_OK;
	g_probe_info.devready = TRUE;
	sdio_info->pub->busstate = DBUS_STATE_DL_DONE;
err:
	return err;
}

static int
dbus_sdif_dlrun(void *bus)
{
	sdio_info_t *sdio_info;
	int err = DBUS_ERR;

	sdio_info = BUS_INFO(bus, sdio_info_t);

	/* Take ARM out of reset */
	err = dbus_sdio_download_state(&g_probe_info, FALSE);
	if (err) {
		DBUSERR(("%s: error getting out of ARM reset\n", __FUNCTION__));
		err = DBUS_ERR;
	} else
		err = DBUS_OK;

	return err;
}

static int
dbus_sdif_stop(void *bus)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err;

	if (sdio_info->drvintf && sdio_info->drvintf->stop)
		err = sdio_info->drvintf->stop(sdio_info->sdos_info);

	dbus_bus_stop(sdio_info);
	return DBUS_OK;
}

static int
dbus_sdif_down(void *bus)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);
	int err;

	if (sdio_info->drvintf && sdio_info->drvintf->down)
		err = sdio_info->drvintf->down(sdio_info->sdos_info);

	dbus_bus_stop(sdio_info);
	return DBUS_OK;
}

static int
dbus_sdif_get_attrib(void *bus, dbus_attrib_t *attrib)
{
	sdio_info_t *sdio_info = BUS_INFO(bus, sdio_info_t);

	if ((sdio_info == NULL) || (attrib == NULL))
		return DBUS_ERR;

	attrib->bustype = DBUS_SDIO;
	attrib->vid = g_probe_info.venid;
	attrib->pid = 0;
	attrib->devid = g_probe_info.devid;
	attrib->nchan = 1;
	attrib->mtu = 512;

	return DBUS_OK;
}

static int
dbus_sdos_sched_dpc(sdio_info_t *sdio_info)
{
	if (sdio_info->pub->busstate == DBUS_STATE_DOWN) {
		DBUSERR(("Bus down. Do not sched dpc\n"));
		return DBUS_ERR;
	}

	if (sdio_info && sdio_info->drvintf && sdio_info->drvintf->sched_dpc)
		return sdio_info->drvintf->sched_dpc(sdio_info->sdos_info);
	else
		return DBUS_ERR;
}

#ifndef BCM_DNGL_EMBEDIMAGE
static int
dbus_sdos_sched_probe_cb()
{
	if (g_dbusintf && g_dbusintf->sched_probe_cb)
		return g_dbusintf->sched_probe_cb(NULL);

	return DBUS_ERR;
}
#endif


/* This callback is overloaded to also handle pre-attach() requests
 * such as downloading an image to the dongle.
 * Before attach(), we're limited to what can be done since
 * sdio_info handle is not available yet:
 * 	- Reading/writing registers
 * 	- Querying cores using si handle
 */
static void *
dbus_sdio_probe_cb(void *handle, const char *desc, uint32 bustype, uint32 hdrlen)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (handle == &g_probe_info) {

		if (g_dbusintf != NULL) {

			/* First, initialize all lower-level functions as default
			 * so that dbus.c simply calls directly to dbus_sdio_os.c.
			 */
			bcopy(g_dbusintf, &dbus_sdio_intf, sizeof(dbus_intf_t));

			/* Second, selectively override functions we need.
			 */
			dbus_sdio_intf.attach = dbus_sdif_attach;
			dbus_sdio_intf.detach = dbus_sdif_detach;
			dbus_sdio_intf.send_irb = dbus_sdif_send_irb;
			/* SDIO does not need pre-submitted IRBs like USB
			 * so set recv_irb() to NULL so dbus.c would not call
			 * this function.
			 */
			dbus_sdio_intf.recv_irb = NULL;
			dbus_sdio_intf.send_ctl = dbus_sdif_send_ctl;
			dbus_sdio_intf.recv_ctl = dbus_sdif_recv_ctl;
			dbus_sdio_intf.up = dbus_sdif_up;
			dbus_sdio_intf.iovar_op = dbus_sdif_iovar_op;
			dbus_sdio_intf.device_exists = dbus_sdif_device_exists;
			dbus_sdio_intf.dlneeded = dbus_sdif_dlneeded;
			dbus_sdio_intf.dlstart = dbus_sdif_dlstart;
			dbus_sdio_intf.dlrun = dbus_sdif_dlrun;
			dbus_sdio_intf.stop = dbus_sdif_stop;
			dbus_sdio_intf.down = dbus_sdif_down;
			dbus_sdio_intf.get_attrib = dbus_sdif_get_attrib;
		}

		/* Assume a valid image has been downloaded when
		 * the handle matches ours so propagate probe callback to upper
		 * layer
		 */
		if (probe_cb) {
			disc_arg = probe_cb(probe_arg, "DBUS SDIO", SD_BUSTYPE, SDPCM_RESERVE);
			return disc_arg;
		}
	}

	return NULL;
}

static void
dbus_sdio_disconnect_cb(void *handle)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (disconnect_cb)
		disconnect_cb(disc_arg);
}

int
dbus_bus_register(int vid, int pid, probe_cb_t prcb,
	disconnect_cb_t discb, void *prarg, dbus_intf_t **intf, void *param1, void *param2)
{
	int err;

	probe_cb = prcb;
	disconnect_cb = discb;
	probe_arg = prarg;

	DBUSTRACE(("%s\n", __FUNCTION__));

	bzero(&g_probe_info, sizeof(probe_sdh_info_t));
	*intf = &dbus_sdio_intf;

	err = dbus_bus_osl_register(vid, pid, dbus_sdio_probe_cb,
		dbus_sdio_disconnect_cb, &g_probe_info, &g_dbusintf, param1, param2);

	ASSERT(g_dbusintf);

	return err;
}

int
dbus_bus_deregister()
{
	dbus_bus_osl_deregister();
	return DBUS_OK;
}

void *
dbus_sdif_attach(dbus_pub_t *pub, void *cbarg, dbus_intf_callbacks_t *cbs)
{
	sdio_info_t *sdio_info;

	DBUSTRACE(("%s\n", __FUNCTION__));
	if ((g_dbusintf == NULL) || (g_dbusintf->attach == NULL))
		return NULL;

	/* Sanity check for BUS_INFO() */
	ASSERT(OFFSETOF(sdio_info_t, pub) == 0);

	sdio_info = MALLOC(pub->osh, sizeof(sdio_info_t));
	if (sdio_info == NULL)
		return NULL;

	bzero(sdio_info, sizeof(sdio_info_t));

	sdio_info->pub = pub;
	sdio_info->cbarg = cbarg;
	sdio_info->cbs = cbs;
	sdio_info->bus = SD_BUSTYPE;
	/* Use bufpool if allocated, else use locally malloced rxbuf */
	sdio_info->usebufpool = FALSE;

	/* Update sdio_info with probe info */
	sdio_info->sdh = g_probe_info.sdh;
	sdio_info->sih = g_probe_info.sih;
	sdio_info->ramsize = g_probe_info.ramsize;
	sdio_info->orig_ramsize = g_probe_info.orig_ramsize;

	ASSERT(g_probe_info.chinfo);
	sdio_info->regs = g_probe_info.chinfo->sdregs;
	sdio_info->vars = g_probe_info.vars;
	sdio_info->varsz = g_probe_info.varsz;
	sdio_info->hostintmask = HOSTINTMASK;

	if (g_probe_info.firmware_file)
		sdio_info->firmware_path = g_probe_info.firmware_file;
	else
		sdio_info->firmware_path = fw_path;

	if (g_probe_info.nvram_file)
		sdio_info->nvram_path = g_probe_info.nvram_file;
	else
		sdio_info->nvram_path = nv_path;

	/* FIX: Need to redo this maxctl stuff since we don't want cdc and IOCTL
	 * info in DBUS.  maxctl is used by rxbuf for static allocation.
	 *
	 * sdio_info->maxctl = WLC_IOCTL_MAXLEN + sizeof(cdc_ioctl_t) + ROUND_UP_MARGIN;
	 */
	sdio_info->maxctl = 8192 + 16 + 2048;
	if (sdio_info->maxctl) {
		sdio_info->rxblen =
			ROUNDUP((sdio_info->maxctl + SDPCM_HDRLEN), ALIGNMENT) + SDALIGN;
		if (!(sdio_info->rxbuf = MALLOC(pub->osh, sdio_info->rxblen))) {
			DBUSERR(("%s: MALLOC of %d-byte rxbuf failed\n",
			           __FUNCTION__, sdio_info->rxblen));
			goto err;
		}
	}

	/* Allocate buffer to receive glomed packet */
	if (!(sdio_info->databuf = MALLOC(pub->osh, MAX_DATA_BUF))) {
		DBUSERR(("%s: MALLOC of %d-byte databuf failed\n",
			__FUNCTION__, MAX_DATA_BUF));
		goto err;
	}

	/* Align the buffer */
	if ((uintptr)sdio_info->databuf % SDALIGN)
		sdio_info->dataptr =
			sdio_info->databuf + (SDALIGN - ((uintptr)sdio_info->databuf % SDALIGN));
	else
		sdio_info->dataptr = sdio_info->databuf;

	/* ...and initialize clock/power states */
	sdio_info->sleeping = FALSE;
	sdio_info->idletime = (int32)dhd_idletime;
	sdio_info->idleclock = IDLE_ACTIVE;

	if (!(dbus_sdio_attach_init(sdio_info, sdio_info->sdh,
		sdio_info->firmware_path, sdio_info->nvram_path))) {
		DBUSERR(("%s: dbus_sdio_attach_init failed\n", __FUNCTION__));
		goto err;
	}

	ASSERT(sdio_info->pub->ntxq > 0);
	pktq_init(&sdio_info->txq, (PRIOMASK+1), sdio_info->pub->ntxq);

	/* Locate an appropriately-aligned portion of hdrbuf */
	sdio_info->rxhdr = (uint8*)ROUNDUP((uintptr)&sdio_info->hdrbuf[0], SDALIGN);

	/* Set the poll and/or interrupt flags */
	sdio_info->intr = (bool)dhd_intr;
	if ((sdio_info->poll = (bool)dhd_poll))
		sdio_info->pollrate = 1;

	sdio_info->sdos_info = (dbus_pub_t *)g_dbusintf->attach(pub,
		sdio_info, &dbus_sdio_intf_cbs);
	if (sdio_info->sdos_info == NULL)
		goto err;

	/* Save SDIO OS-specific driver entry points */
	sdio_info->drvintf = g_dbusintf;

	if (g_probe_info.devready == TRUE)
		sdio_info->pub->busstate = DBUS_STATE_DL_DONE;

	pub->bus = sdio_info;

	return (void *) sdio_info->sdos_info; /* Return Lower layer info */
err:
	if (sdio_info) {
		MFREE(pub->osh, sdio_info, sizeof(sdio_info_t));
	}
	return NULL;

}

void
dbus_sdif_detach(dbus_pub_t *pub, void *info)
{
	sdio_info_t *sdio_info = pub->bus;
	osl_t *osh = pub->osh;

	dbus_bus_stop(sdio_info);

	if (sdio_info->drvintf && sdio_info->drvintf->detach)
		sdio_info->drvintf->detach(pub, sdio_info->sdos_info);

	dbus_sdio_release(sdio_info, sdio_info->pub->osh);
	MFREE(osh, sdio_info, sizeof(sdio_info_t));
}

static void
dbus_sdio_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->send_irb_timeout)
		sdio_info->cbs->send_irb_timeout(sdio_info->cbarg, txirb);
}

static void
dbus_sdio_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->send_irb_complete)
		sdio_info->cbs->send_irb_complete(sdio_info->cbarg, txirb, status);
}

static void
dbus_sdio_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->recv_irb_complete)
		sdio_info->cbs->recv_irb_complete(sdio_info->cbarg, rxirb, status);
}

static void
dbus_sdio_errhandler(void *handle, int err)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->errhandler)
		sdio_info->cbs->errhandler(sdio_info->cbarg, err);
}

static void
dbus_sdio_ctl_complete(void *handle, int type, int status)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->ctl_complete)
		sdio_info->cbs->ctl_complete(sdio_info->cbarg, type, status);
}

static void
dbus_sdio_state_change(void *handle, int state)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;

	if (sdio_info == NULL)
		return;

	if (sdio_info->cbs && sdio_info->cbs->state_change)
		sdio_info->cbs->state_change(sdio_info->cbarg, state);

	if (state == DBUS_STATE_DISCONNECT) {
		if (sdio_info->drvintf && sdio_info->drvintf->remove)
			sdio_info->drvintf->remove(sdio_info->sdos_info);

		sdio_info->pub->busstate = DBUS_STATE_DOWN;
	}

}

static bool
dbus_sdio_dpc(void *handle, bool bounded)
{
	sdio_info_t *sdio_info = (sdio_info_t *) handle;
	bcmsdh_info_t *sdh;
	sdpcmd_regs_t *regs;
	uint32 intstatus, newstatus = 0;
	uint retries = 0;

	uint rxlimit = dhd_rxbound; /* Rx frames to read before resched */
	uint framecnt = 0;		  /* Temporary counter of tx/rx frames */
	bool rxdone = TRUE;		  /* Flag for no more read data */
	bool resched = FALSE;	  /* Flag indicating resched wanted */

	DBUSTRACE(("%s: Enter\n", __FUNCTION__));

	if (sdio_info == NULL) {
		DBUSERR(("%s: sdio_info == NULL!\n", __FUNCTION__));
		return FALSE;
	}

	dbus_sdos_lock(sdio_info);

	sdh = sdio_info->sdh;
	regs = sdio_info->regs;

	/* Start with leftover status bits */
	intstatus = sdio_info->intstatus;

	/* If waiting for HTAVAIL, check status */
	if (sdio_info->clkstate == CLK_PENDING) {
		int err;
		uint8 clkctl, devctl = 0;


		/* Read CSR, if clock on switch to AVAIL, else ignore */
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DBUSERR(("%s: error reading CSR: %d\n", __FUNCTION__, err));
			sdio_info->pub->busstate = DBUS_STATE_DOWN;
		}

		DBUSINFO(("DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n", devctl, clkctl));

		if (SBSDIO_HTAV(clkctl)) {
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DBUSERR(("%s: error reading DEVCTL: %d\n",
				           __FUNCTION__, err));
				sdio_info->pub->busstate = DBUS_STATE_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			if (err) {
				DBUSERR(("%s: error writing DEVCTL: %d\n",
				           __FUNCTION__, err));
				sdio_info->pub->busstate = DBUS_STATE_DOWN;
			}
			sdio_info->clkstate = CLK_AVAIL;
		} else {
			goto clkwait;
		}
	}

	BUS_WAKE(sdio_info);

	/* Make sure backplane clock is on */
	dbus_sdio_clkctl(sdio_info, CLK_AVAIL, TRUE);
	if (sdio_info->clkstate == CLK_PENDING)
		goto clkwait;

	/* Pending interrupt indicates new device status */
	if (sdio_info->ipend) {
		sdio_info->ipend = FALSE;
		R_SDREG(newstatus, &regs->intstatus, retries);
		sdio_info->f1regdata++;
		if (bcmsdh_regfail(sdio_info->sdh))
			newstatus = 0;
		newstatus &= sdio_info->hostintmask;
		sdio_info->fcstate = !!(newstatus & I_HMB_FC_STATE);
		if (newstatus) {
			W_SDREG(newstatus, &regs->intstatus, retries);
			sdio_info->f1regdata++;
		}
	}

	/* Merge new bits with previous */
	intstatus |= newstatus;
	sdio_info->intstatus = 0;

	/* Handle flow-control change: read new state in case our ack
	 * crossed another change interrupt.  If change still set, assume
	 * FC ON for safety, let next loop through do the debounce.
	 */
	if (intstatus & I_HMB_FC_CHANGE) {
		intstatus &= ~I_HMB_FC_CHANGE;
		W_SDREG(I_HMB_FC_CHANGE, &regs->intstatus, retries);
		R_SDREG(newstatus, &regs->intstatus, retries);
		sdio_info->f1regdata += 2;
		sdio_info->fcstate = !!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE));
		intstatus |= (newstatus & sdio_info->hostintmask);
	}

	/* Just being here means nothing more to do for chipactive */
	if (intstatus & I_CHIPACTIVE) {
		/* ASSERT(sdio_info->clkstate == CLK_AVAIL); */
		intstatus &= ~I_CHIPACTIVE;
	}

	/* Handle host mailbox indication */
	if (intstatus & I_HMB_HOST_INT) {
		intstatus &= ~I_HMB_HOST_INT;
		intstatus |= dbus_sdio_hostmail(sdio_info);
	}

	/* Generally don't ask for these, can get CRC errors... */
	if (intstatus & I_WR_OOSYNC) {
		DBUSERR(("Dongle reports WR_OOSYNC\n"));
		intstatus &= ~I_WR_OOSYNC;
	}

	if (intstatus & I_RD_OOSYNC) {
		DBUSERR(("Dongle reports RD_OOSYNC\n"));
		intstatus &= ~I_RD_OOSYNC;
	}

	if (intstatus & I_SBINT) {
		DBUSERR(("Dongle reports SBINT\n"));
		intstatus &= ~I_SBINT;
	}

	/* Would be active due to wake-wlan in gSPI */
	if (intstatus & I_CHIPACTIVE) {
		DBUSERR(("Dongle reports CHIPACTIVE\n"));
		intstatus &= ~I_CHIPACTIVE;
	}

	/* Ignore frame indications if rxskip is set */
	if (sdio_info->rxskip)
		intstatus &= ~I_HMB_FRAME_IND;

	/* On frame indication, read available frames */
	if (PKT_AVAILABLE()) {
		framecnt = dbus_sdio_readframes(sdio_info, rxlimit, &rxdone);
		if (rxdone || sdio_info->rxskip)
			intstatus &= ~I_HMB_FRAME_IND;
		rxlimit -= MIN(framecnt, rxlimit);
	}

	if (pktq_mlen(&sdio_info->txq, ~sdio_info->flowcontrol) > 0) {
		/* reschedule txq */
		dbus_sdio_txq_sched(sdio_info->sdos_info);
	}

	/* Keep still-pending events for next scheduling */
	sdio_info->intstatus = intstatus;

clkwait:
	/* Re-enable interrupts to detect new device events (mailbox, rx frame)
	 * or clock availability.  (Allows tx loop to check ipend if desired.)
	 * (Unless register access seems hosed, as we may not be able to ACK...)
	 */
	if (sdio_info->intr && sdio_info->intdis && !bcmsdh_regfail(sdh)) {
		DBUSINTR(("%s: enable SDIO interrupts, rxdone %d framecnt %d\n",
		          __FUNCTION__, rxdone, framecnt));
		sdio_info->intdis = FALSE;
		bcmsdh_intr_enable(sdh);
	}

	/* Resched if events or tx frames are pending, else await next interrupt */
	/* On failed register access, all bets are off: no resched or interrupts */
	if ((sdio_info->pub->busstate == DBUS_STATE_DOWN) || bcmsdh_regfail(sdh)) {
		DBUSERR(("%s: failed backplane access over SDIO, halting operation\n",
		           __FUNCTION__));
		dbus_sdio_state_change(sdio_info, DBUS_STATE_DISCONNECT);
		sdio_info->intstatus = 0;
		dbus_bus_stop(sdio_info);
	} else if (sdio_info->clkstate == CLK_PENDING) {
		/* Awaiting I_CHIP_ACTIVE, don't resched */
	} else if (sdio_info->intstatus || sdio_info->ipend || PKT_AVAILABLE()) {
		resched = TRUE;
	}

	dbus_sdos_unlock(sdio_info);
	sdio_info->dpc_sched = resched;

	/* If we're done for now, turn off clock request. */
	if (sdio_info->idletime == IDLE_IMMEDIATE) {
		sdio_info->activity = FALSE;
		dbus_sdio_clkctl(sdio_info, CLK_NONE, FALSE);
	}

	return resched;
}

static void
dbus_sdio_watchdog(void *handle)
{
}
