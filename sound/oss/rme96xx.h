/* (C) 2000 Guenter Geiger <geiger@debian.org>
   with copy/pastes from the driver of Winfried Ritsch <ritsch@iem.kug.ac.at>

Modifications - Heiko Purnhagen <purnhage@tnt.uni-hannover.de>
   HP20020116 towards REV 1.5 support, based on ALSA's card-rme9652.c
   HP20020201 completed?

A text/graphic control panel (rmectrl/xrmectrl) is available from
   http://gige.xdv.org/pages/soft/pages/rme
*/


#ifndef AFMT_S32_BLOCKED
#define AFMT_S32_BLOCKED 0x0000400
#endif

/* AFMT_S16_BLOCKED not yet supported */
#ifndef AFMT_S16_BLOCKED 
#define AFMT_S16_BLOCKED 0x0000800
#endif


typedef struct rme_status {
	unsigned int irq:1;
	unsigned int lockmask:3;     /* ADAT input PLLs locked */
	                             /*   100=ADAT1, 010=ADAT2, 001=ADAT3 */
	unsigned int sr48:1;         /* sample rate: 0=44.1/88.2 1=48/96 kHz */
	unsigned int wclock:1;       /* 1=wordclock used */
	unsigned int bufpoint:10;
	unsigned int syncmask:3;     /* ADAT input in sync with system clock */
	                             /* 100=ADAT1, 010=ADAT2, 001=ADAT3 */
	unsigned int doublespeed:1;  /* sample rate: 0=44.1/48 1=88.2/96 kHz */
	unsigned int tc_busy:1;
	unsigned int tc_out:1;
	unsigned int crystalrate:3;  /* spdif input sample rate: */
	                             /*   000=64kHz, 100=88.2kHz, 011=96kHz */
	                             /*   111=32kHz, 110=44.1kHz, 101=48kHz */
	unsigned int spdif_error:1;  /* 1=no spdif lock */
	unsigned int bufid:1;
	unsigned int tc_valid:1;     /* 1=timecode input detected */
	unsigned int spdif_read:1;
} rme_status_t;


/* only fields marked W: can be modified by writing to SOUND_MIXER_PRIVATE3 */
typedef struct rme_control {
	unsigned int start:1;
	unsigned int latency:3;      /* buffer size / latency [samples]: */
	                             /*   0=64 ... 7=8192 */
	unsigned int master:1;       /* W: clock mode: 1=master 0=slave/auto */
	unsigned int ie:1;
	unsigned int sr48:1;         /* samplerate 0=44.1/88.2, 1=48/96 kHz */
	unsigned int spare:1;
	unsigned int doublespeed:1;  /* double speed 0=44.1/48, 1=88.2/96 Khz */
	unsigned int pro:1;          /* W: SPDIF-OUT 0=consumer, 1=professional */
	unsigned int emphasis:1;     /* W: SPDIF-OUT emphasis 0=off, 1=on */
	unsigned int dolby:1;        /* W: SPDIF-OUT non-audio bit 1=set, 0=unset */
	unsigned int opt_out:1;      /* W: use 1st optical OUT as SPDIF: 1=yes, 0=no */
	unsigned int wordclock:1;    /* W: use Wordclock as sync (overwrites master) */
        unsigned int spdif_in:2;     /* W: SPDIF-IN: */
                                     /*    00=optical (ADAT1), 01=coaxial (Cinch), 10=internal CDROM */
	unsigned int sync_ref:2;     /* W: preferred sync-source in autosync */
                                     /*    00=ADAT1, 01=ADAT2, 10=ADAT3, 11=SPDIF */
	unsigned int spdif_reset:1;
	unsigned int spdif_select:1;
	unsigned int spdif_clock:1;
	unsigned int spdif_write:1;
	unsigned int adat1_cd:1;     /* W: Rev 1.5+: if set, internal CD connector carries ADAT */
} rme_ctrl_t;


typedef struct _rme_mixer {
	int i_offset;
	int o_offset;
	int devnr;
	int spare[8];
} rme_mixer;

