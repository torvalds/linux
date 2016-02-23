#define DSP_RESET	(devc->base + 0x6)
#define DSP_READ	(devc->base + 0xA)
#define DSP_WRITE	(devc->base + 0xC)
#define DSP_COMMAND	(devc->base + 0xC)
#define DSP_STATUS	(devc->base + 0xC)
#define DSP_DATA_AVAIL	(devc->base + 0xE)
#define DSP_DATA_AVL16	(devc->base + 0xF)
#define MIXER_ADDR	(devc->base + 0x4)
#define MIXER_DATA	(devc->base + 0x5)
#define OPL3_LEFT	(devc->base + 0x0)
#define OPL3_RIGHT	(devc->base + 0x2)
#define OPL3_BOTH	(devc->base + 0x8)
/* DSP Commands */

#define DSP_CMD_SPKON		0xD1
#define DSP_CMD_SPKOFF		0xD3
#define DSP_CMD_DMAON		0xD0
#define DSP_CMD_DMAOFF		0xD4

#define IMODE_NONE		0
#define IMODE_OUTPUT		PCM_ENABLE_OUTPUT
#define IMODE_INPUT		PCM_ENABLE_INPUT
#define IMODE_INIT		3
#define IMODE_MIDI		4

#define NORMAL_MIDI	0
#define UART_MIDI	1


/*
 * Device models
 */
#define MDL_NONE	0
#define MDL_SB1		1	/* SB1.0 or 1.5 */
#define MDL_SB2		2	/* SB2.0 */
#define MDL_SB201	3	/* SB2.01 */
#define MDL_SBPRO	4	/* SB Pro */
#define MDL_SB16	5	/* SB16/32/AWE */
#define MDL_SBPNP 	6	/* SB16/32/AWE PnP */
#define MDL_JAZZ	10	/* Media Vision Jazz16 */
#define MDL_SMW		11	/* Logitech SoundMan Wave (Jazz16) */
#define MDL_ESS		12	/* ESS ES688 and ES1688 */
#define MDL_AZTECH	13	/* Aztech Sound Galaxy family */
#define MDL_ES1868MIDI	14	/* MIDI port of ESS1868 */
#define MDL_AEDSP	15	/* Audio Excel DSP 16 */
#define MDL_ESSPCI	16	/* ESS PCI card */
#define MDL_YMPCI	17	/* Yamaha PCI sb in emulation */

#define SUBMDL_ALS007	42	/* ALS-007 differs from SB16 only in mixer */
				/* register assignment */
#define SUBMDL_ALS100	43	/* ALS-100 allows sampling rates of up */
				/* to 48kHz */
				
/*
 * Config flags
 */
#define SB_NO_MIDI	0x00000001
#define SB_NO_MIXER	0x00000002
#define SB_NO_AUDIO	0x00000004
#define SB_NO_RECORDING	0x00000008 /* No audio recording */
#define SB_MIDI_ONLY	(SB_NO_AUDIO|SB_NO_MIXER)
#define SB_PCI_IRQ	0x00000010 /* PCI shared IRQ */

struct mixer_def {
	unsigned int regno: 8;
	unsigned int bitoffs:4;
	unsigned int nbits:4;
};

typedef struct mixer_def mixer_tab[32][2];
typedef struct mixer_def mixer_ent;

struct sb_module_options
{
	int  esstype;	/* ESS chip type */
	int  acer;	/* Do acer notebook init? */
	int  sm_games;	/* Logitech soundman games? */
};

typedef struct sb_devc {
	   int dev;

	/* Hardware parameters */
	   int *osp;
	   int minor, major;
	   int type;
	   int model, submodel;
	   int caps;
#	define SBCAP_STEREO	0x00000001
#	define SBCAP_16BITS	0x00000002

	/* Hardware resources */
	   int base;
	   int irq;
	   int dma8, dma16;
	   
	   int pcibase;		/* For ESS Maestro etc */

	/* State variables */
 	   int opened;
	/* new audio fields for full duplex support */
	   int fullduplex;
	   int duplex;
	   int speed, bits, channels;
	   volatile int irq_ok;
	   volatile int intr_active, irq_mode;
	/* duplicate audio fields for full duplex support */
	   volatile int intr_active_16, irq_mode_16;

	/* Mixer fields */
	   int *levels;
	   mixer_tab *iomap;
	   size_t iomap_sz; /* number or records in the iomap table */
	   int mixer_caps, recmask, outmask, supported_devices;
	   int supported_rec_devices, supported_out_devices;
	   int my_mixerdev;
	   int sbmixnum;

	/* Audio fields */
	   unsigned long trg_buf;
	   int      trigger_bits;
	   int      trg_bytes;
	   int      trg_intrflag;
	   int      trg_restart;
	/* duplicate audio fields for full duplex support */
	   unsigned long trg_buf_16;
	   int      trigger_bits_16;
	   int      trg_bytes_16;
	   int      trg_intrflag_16;
	   int      trg_restart_16;

	   unsigned char tconst;
	
	/* MIDI fields */
	   int my_mididev;
	   int input_opened;
	   int midi_broken;
	   void (*midi_input_intr) (int dev, unsigned char data);
	   void *midi_irq_cookie;		/* IRQ cookie for the midi */

	   spinlock_t lock;

	   struct sb_module_options sbmo;	/* Module options */

	} sb_devc;
	
/*
 *	PCI card types
 */

#define	SB_PCI_ESSMAESTRO	1	/* ESS Maestro Legacy */
#define	SB_PCI_YAMAHA		2	/* Yamaha Legacy */

/* 
 *	Functions
 */
 
int sb_dsp_command (sb_devc *devc, unsigned char val);
int sb_dsp_get_byte(sb_devc * devc);
int sb_dsp_reset (sb_devc *devc);
void sb_setmixer (sb_devc *devc, unsigned int port, unsigned int value);
unsigned int sb_getmixer (sb_devc *devc, unsigned int port);
int sb_dsp_detect (struct address_info *hw_config, int pci, int pciio, struct sb_module_options *sbmo);
int sb_dsp_init (struct address_info *hw_config, struct module *owner);
void sb_dsp_unload(struct address_info *hw_config, int sbmpu);
int sb_mixer_init(sb_devc *devc, struct module *owner);
void sb_mixer_unload(sb_devc *devc);
void sb_mixer_set_stereo (sb_devc *devc, int mode);
void smw_mixer_init(sb_devc *devc);
void sb_dsp_midi_init (sb_devc *devc, struct module *owner);
void sb_audio_init (sb_devc *devc, char *name, struct module *owner);
void sb_midi_interrupt (sb_devc *devc);
void sb_chgmixer (sb_devc * devc, unsigned int reg, unsigned int mask, unsigned int val);
int sb_common_mixer_set(sb_devc * devc, int dev, int left, int right);

int sb_audio_open(int dev, int mode);
void sb_audio_close(int dev);

/*	From sb_common.c */
void sb_dsp_disable_midi(int port);
int probe_sbmpu (struct address_info *hw_config, struct module *owner);
void unload_sbmpu (struct address_info *hw_config);

void unload_sb16(struct address_info *hw_info);
void unload_sb16midi(struct address_info *hw_info);
