/*	$OpenBSD: pasreg.h,v 1.4 2025/07/15 13:40:02 jsg Exp $	*/
/*	$NetBSD: pasreg.h,v 1.2 1995/03/15 18:45:58 brezak Exp $	*/

/* Port addresses and bit fields for the Media Vision Pro AudioSpectrum
 * second generation sound cards.
 * 
 * Feel free to use this header file in any application you create that
 * has support for the Media Vision Pro AudioSpectrum second generation
 * sound cards. Other uses prohibited without prior permission.
 * 
 * - cmetz@thor.tjhsst.edu
 * 
 * Notes:
 *
 * -	All of these ports go into the MVD101 multimedia controller chip,
 *	which then signals the other chips to do the actual work. Many
 *	ports like the FM ones functionally attach directly to the
 * 	destination chip though	they don't actually have a direct connection.
 * -	The PAS2 series cards have an MVD101 multimedia controller chip,
 * 	the original PAS cards don't. The original PAS cards are pretty
 * 	defunct now, so no attempt is made here to support them.
 * -	The PAS2 series cards are all really different at the hardware level,
 * 	though the MVD101 hides some of the incompatibilities, there still
 * 	are differences that need to be accounted for.
 * 
 *		Card		CD-ROM interface	PCM chip		Mixer chip		FM chip
 * 		PAS Plus	Sony proprietary	(Crystal?) 8-bit DAC	National 		OPL3
 * 		PAS 16		Zilog SCSI		MVA416 16-bit Codec	MVA508 			OPL3
 * 		CDPC		Sony proprietary	Sony 16-bit Codec	National		OPL3
 *		Fusion CD 16	Sony proprietary	MVA416 16-bit Codec	MVA508			OPL3
 *		Fusion CD	Sony proprietary	(Crystal?) 8-bit DAC	National		OPL3
 *
 */

#define PAS_DEFAULT_BASE		0x388

/*      Symbolic Name			Value 		   R W  Subsystem	Description					*/
#define SPEAKER_CONTROL			0x61		/*   W	PC speaker 	Control register 				*/
#define SPEAKER_CONTROL_GHOST		0x738B		/* R W	PC speaker 	Control ghost register				*/
#define SPEAKER_TIMER_CONTROL		0x43		/*   W  PC speaker 	Timer control register				*/
#define SPEAKER_TIMER_CONTROL_GHOST	0x778B		/* R W  PC speaker 	Timer control register ghost			*/
#define SPEAKER_TIMER_DATA		0x42		/*   W  PC speaker 	Timer data register				*/
#define SPEAKER_TIMER_DATA_GHOST	0x138A		/* R W  PC speaker	Timer data register ghost			*/

#define WARM_BOOT			0x41		/*   W  Control		Used to detect system warm boot	  		*/
#define WARM_BOOT_GHOST			0x7789		/* ? W  Control		Use to get the card to fake warm boot		*/
#define MASTER_DECODE			0x9A01		/*   W  Control		Address >> 2 of card base address		*/
#define PRESCALE_DIVIDER		0xBF8A		/* R W	PCM		Ration between Codec clock and master clock	*/
#define WAIT_STATE			0xBF88		/* R W	Control		Four-bit bus wait-state count (~140ns ea.)	*/
#define BOARD_REV_ID			0x2789		/* R	Control		Extended Board Revision ID			*/

#define SYSTEM_CONFIGURATION_1		0x8388		/* R W	Control								*/
	#define S_C_1_PCS_ENABLE	0x01		/* R W  PC speaker	1=enable, 0=disable PC speaker emulation	*/
	#define S_C_1_PCM_CLOCK_SELECT	0x02		/* R W  PCM		1=14.31818MHz/12, 0=28.224MHz master clock	*/ 
	#define S_C_1_FM_EMULATE_CLOCK	0x04		/* R W  FM		1=use 28.224MHz/2, 0=use 14.31818MHz clock	*/
	#define S_C_1_PCS_STEREO	0x10 		/* R W  PC speaker	1=enable PC speaker stereo effect, 0=disable	*/
	#define S_C_1_PCS_REALSOUND	0x20		/* R W  PC speaker	1=enable RealSound enhancement, 0=disable	*/
	#define S_C_1_FORCE_EXT_RESET	0x40		/* R W  Control		Force external reset				*/
	#define S_C_1_FORCE_INT_RESET	0x80		/* R W  Control		Force internal reset				*/
#define SYSTEM_CONFIGURATION_2		0x8389		/* R W  Control								*/
	#define S_C_2_PCM_OVERSAMPLING	0x03		/* R W  PCM		00=0x, 01=2x, 10=4x, 11=reserved		*/
	#define S_C_2_PCM_16_BIT	0x04		/* R W	PCM		1=16-bit, 0=8-bit samples			*/
#define SYSTEM_CONFIGURATION_3		0x838A		/* R W	Control								*/
	#define S_C_3_PCM_CLOCK_SELECT	0x02		/* R W	PCM		1=use 1.008MHz clock for PCM, 0=don't		*/
#define SYSTEM_CONFIGURATION_4		0x838B		/* R W  Control		CD-ROM interface controls			*/

#define IO_CONFIGURATION_1		0xF388		/* R W	Control								*/
	#define I_C_1_BOOT_RESET_ENABLE	0x80		/* R W  Control		1=reset board on warm boot, 0=don't		*/
#define IO_CONFIGURATION_2		0xF389		/* R W  Control								*/
	#define	I_C_2_PCM_DMA_DISABLED	0x00		/* R W  PCM		PCM DMA disabled				*/
#define IO_CONFIGURATION_3		0xF38A		/* R W	Control								*/
	#define I_C_3_PCM_IRQ_DISABLED	0x00		/* R W  PCM		PCM IRQ disabled				*/

#define COMPATIBILITY_ENABLE		0xF788		/* R W  Control								*/
	#define C_E_MPU401_ENABLE	0x01		/* R W	MIDI		1=enable, 0=disable MPU401 MIDI emulation	*/
	#define C_E_SB_ENABLE		0x02		/* R W  PCM		1=enable, 0=disable Sound Blaster emulation	*/
	#define C_E_SB_ACTIVE		0x04		/* R    PCM		"Sound Blaster Interrupt active"		*/
	#define C_E_MPU401_ACTIVE	0x08		/* R	MIDI		"MPU UART mode active"				*/
	#define C_E_PCM_COMPRESSION	0x10		/* R W  PCM		1=enable, 0=disabled compression		*/
#define EMULATION_ADDRESS		0xF789		/* R W  Control								*/
	#define E_A_SB_BASE		0x0f		/* R W  PCM		bits A4-A7 for SB base port			*/
	#define E_A_MPU401_BASE		0xf0		/* R W	MIDI		bits A4-A7 for MPU401 base port 		*/
#define EMULATION_CONFIGURATION		0xFB8A		/* R W			***** Only valid on newer PAS2 cards (?) *****	*/
	#define E_C_MPU401_IRQ		0x07		/* R W	MIDI		MPU401 emulation IRQ				*/
	#define E_C_SB_IRQ		0x38		/* R W  PCM		SB emulation IRQ				*/
	#define E_C_SB_DMA		0xC0		/* R W	PCM		SB emulation DMA				*/

#define OPERATION_MODE_1		0xEF8B		/* R	Control								*/
	#define	O_M_1_CDROM_TYPE	0x03		/* R	CD-ROM		3=SCSI, 2=Sony, 0=no CD-ROM interface		*/
	#define O_M_1_FM_TYPE		0x04		/* R	FM		1=stereo, 0=mono FM chip			*/
	#define O_M_1_PCM_TYPE 		0x08		/* R	PCM		1=16-bit Codec, 0=8-bit DAC			*/
#define OPERATION_MODE_2		0xFF8B		/* R	Control								*/
	#define O_M_2_PCS_ENABLED	0x02		/* R	PC speaker	PC speaker emulation 1=enabled, 0=disabled	*/
	#define O_M_2_BUS_TIMING	0x10		/* R	Control		1=AT bus timing, 0=XT bus timing		*/
	#define O_M_2_BOARD_REVISION	0xe0		/* R	Control		Board revision					*/

#define INTERRUPT_MASK			0x0B8B		/* R W	Control								*/
	#define I_M_FM_LEFT_IRQ_ENABLE	0x01		/* R W	FM		Enable FM left interrupt			*/
	#define I_M_FM_RIGHT_IRQ_ENABLE	0x02		/* R W	FM		Enable FM right interrupt			*/
	#define I_M_PCM_RATE_IRQ_ENABLE	0x04		/* R W	PCM		Enable Sample Rate interrupt			*/
	#define I_M_PCM_BUFFER_IRQ_ENABLE 0x08		/* R W	PCM		Enable Sample Buffer interrupt			*/
	#define I_M_MIDI_IRQ_ENABLE	0x10		/* R W	MIDI		Enable MIDI interrupt				*/
	#define I_M_BOARD_REV		0xE0		/* R	Control		Board revision					*/

#define INTERRUPT_STATUS		0x0B89		/* R W	Control								*/
	#define I_S_FM_LEFT_IRQ		0x01		/* R W	FM		Left FM Interrupt Pending			*/
	#define I_S_FM_RIGHT_IRQ	0x02		/* R W	FM		Right FM Interrupt Pending			*/
	#define I_S_PCM_SAMPLE_RATE_IRQ	0x04		/* R W	PCM		Sample Rate Interrupt Pending			*/
	#define I_S_PCM_SAMPLE_BUFFER_IRQ 0x08		/* R W	PCM		Sample Buffer Interrupt Pending			*/
	#define I_S_MIDI_IRQ		0x10		/* R W	MIDI		MIDI Interrupt Pending				*/
	#define I_S_PCM_CHANNEL		0x20		/* R W	PCM		1=right, 0=left					*/
	#define I_S_RESET_ACTIVE	0x40		/* R W	Control		Reset is active (Timed pulse not finished)	*/
	#define I_S_PCM_CLIPPING	0x80		/* R W	PCM		Clipping has occurred				*/

#define FILTER_FREQUENCY		0x0B8A		/* R W	Control								*/
	#define F_F_FILTER_DISABLED	0x00		/* R W 	Mixer		No filter					*/
#if 0
	struct {					/* R W	Mixer		Filter translation				*/
		unsigned int freq:24;
		unsigned int value:8;
	} F_F_FILTER_translate[] = 
	{ { 73500, 0x01 },	/* 73500Hz - divide by  16 */
	  { 65333, 0x02 },	/* 65333Hz - divide by  18 */
	  { 49000, 0x09 },	/* 49000Hz - divide by  24 */
	  { 36750, 0x11 },	/* 36750Hz - divide by  32 */
	  { 24500, 0x19 },	/* 24500Hz - divide by  48 */
	  { 18375, 0x07 },	/* 18375Hz - divide by  64 */
	  { 12783, 0x0f }, 	/* 12783Hz - divide by  92 */
	  { 12250, 0x04 },	/* 12250Hz - divide by  96 */
	  {  9188, 0x17 }, 	/*  9188Hz - divide by 128 */
	  {  6125, 0x1f },	/*  6125Hz - divide by 192 */
	};
#endif
	#define F_F_MIXER_UNMUTE	0x20		/* R W	Mixer		1=disable, 0=enable board mute			*/
	#define F_F_PCM_RATE_COUNTER	0x40		/* R W	PCM		1=enable, 0=disable sample rate counter		*/
	#define F_F_PCM_BUFFER_COUNTER	0x80		/* R W 	PCM		1=enable, 0=disable sample buffer counter	*/

#define PAS_NONE	0
#define PAS_PLUS	1
#define PAS_CDPC	2
#define PAS_16		3
#define PAS_16BASIC     4        /* no CDrom */

#ifdef DEFINE_TRANSLATIONS
	char I_C_2_PCM_DMA_translate[] = 		/* R W  PCM		PCM DMA channel value translations		*/
			{ 4, 1, 2, 3, 0, 5, 6, 7 };
	char I_C_3_PCM_IRQ_translate[] = 		/* R W	PCM		PCM IRQ level value translation			*/
		{ 0,  0,  1,  2,  3,  4,  5,  6, 0,  0,  7,  8,  9,  0, 10, 11 };  
	char E_C_MPU401_IRQ_translate[] = 		/* R W	MIDI		MPU401 emulation IRQ value translation		*/
		{ 0x00, 0x00, 0x01, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x05, 0x06, 0x07 };
	char E_C_SB_IRQ_translate[] = 			/* R W	PCM		SB emulation IRQ translate			*/
		{ 0x00, 0x00, 0x08, 0x10, 0x00, 0x18, 0x00, 0x20, 0x00, 0x00, 0x28, 0x30, 0x38 };
	char E_C_SB_DMA_translate[] = 			/* R W	PCM		SB emulation DMA translate			*/
		{ 0x00, 0x40, 0x80, 0xC0 };
	char O_M_1_to_card[] = 				/* R W	Control		Translate (OM1 & 0x0f) to card type		*/
		{ 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 4, 0, 2, 3 };   
#else
	extern char I_C_2_PCM_DMA_translate[];		/* R W  PCM		PCM DMA channel value translations		*/
	extern char I_C_3_PCM_IRQ_translate[];		/* R W	PCM		PCM IRQ level value translation			*/
	extern char E_C_MPU401_IRQ_translate[];		/* R W	MIDI		MPU401 emulation IRQ value translation		*/
	extern char E_C_SB_IRQ_translate[];		/* R W	PCM		SB emulation IRQ translate			*/
	extern char E_C_SB_DMA_translate[];		/* R W	PCM		SB emulation DMA translate			*/
	extern char O_M_1_to_card[];			/* R W	Control		Translate (OM1 & 0x0f) to card type		*/
#endif

#define PARALLEL_MIXER			0x078B		/*   W	Mixer		Documented for MVD101 as FM Mono Right decode?? */
	#define P_M_MV508_ADDRESS	0x80		/*   W	Mixer		MVD508	Address/mixer select			*/
	#define P_M_MV508_DATA		0x00
	#define P_M_MV508_LEFT		0x20		/*   W	Mixer		MVD508	Left channel select			*/
	#define P_M_MV508_RIGHT		0x40		/*   W	Mixer		MVD508	Right channel select			*/
	#define P_M_MV508_BOTH		0x00		/*   W	Mixer		MVD508	Both channel select			*/
	#define P_M_MV508_MIXER		0x10		/*   W	Mixer		MVD508	Select a mixer (rather than a volume) 	*/
	#define P_M_MV508_VOLUME	0x00

	#define P_M_MV508_INPUTMIX	0x20		/*   W  Mixer		MVD508	Select mixer A				*/
	#define P_M_MV508_OUTPUTMIX	0x00		/*   W  Mixer		MVD508	Select mixer B				*/

	#define P_M_MV508_MASTER_A	0x01		/*   W	Mixer		MVD508	Master volume control A (output)	*/
	#define P_M_MV508_MASTER_B	0x02		/*   W	Mixer		MVD508	Master volume control B (DSP input)	*/
	#define P_M_MV508_BASS		0x03		/*   W	Mixer		MVD508	Bass control				*/
	#define P_M_MV508_TREBLE	0x04		/*   W	Mixer		MVD508	Treble control				*/
	#define P_M_MV508_MODE		0x05		/*   W	Mixer		MVD508	Master mode control			*/

	#define P_M_MV508_LOUDNESS	0x04		/*   W	Mixer		MVD508	Mode control - Loudness filter 		*/
	#define P_M_MV508_ENHANCE_NONE	0x00		/*   W	Mixer		MVD508	Mode control - No stereo enhancement	*/
	#define P_M_MV508_ENHANCE_40	0x01		/*   W	Mixer		MVD508	Mode control - 40% stereo enhancement	*/
	#define P_M_MV508_ENHANCE_60	0x02		/*   W	Mixer		MVD508	Mode control - 60% stereo enhancement	*/
	#define P_M_MV508_ENHANCE_80	0x03		/*   W	Mixer		MVD508	Mode control - 80% stereo enhancement	*/

	#define P_M_MV508_FM		0x00		/*   W	Mixer		MVD508	Channel 0 - FM				*/
	#define P_M_MV508_IMIXER	0x01		/*   W	Mixer		MVD508	Channel 1 - Input mixer (rec monitor)	*/
	#define P_M_MV508_LINE		0x02		/*   W	Mixer		MVD508	Channel 2 - Line in			*/
	#define P_M_MV508_CDROM		0x03		/*   W	Mixer		MVD508	Channel 3 - CD-ROM			*/
	#define P_M_MV508_MIC		0x04		/*   W	Mixer		MVD508	Channel 4 - Microphone			*/
	#define P_M_MV508_PCM		0x05		/*   W	Mixer		MVD508	Channel 5 - PCM				*/
	#define P_M_MV508_SPEAKER	0x06		/*   W	Mixer		MVD508	Channel 6 - PC Speaker			*/
	#define P_M_MV508_SB		0x07		/*   W	Mixer		MVD508	Channel 7 - SB DSP			*/

#define SERIAL_MIXER			0xB88		/* R W	Control		Serial mixer control (used other ways)		*/
	#define S_M_PCM_RESET		0x01		/* R W	PCM		Codec/DSP reset					*/
	#define S_M_FM_RESET		0x02		/* R W	FM		FM chip reset					*/
	#define S_M_SB_RESET		0x04		/* R W	PCM		SB emulation chip reset				*/
	#define S_M_MIXER_RESET		0x10		/* R W	Mixer		Mixer chip reset				*/
	#define S_M_INTEGRATOR_ENABLE	0x40		/* R W	Speaker		Enable PC speaker integrator (FORCE RealSound)	*/

#define PCM_CONTROL			0xF8A		/* R W	PCM		PCM Control Register				*/
        #define P_C_MIXER_CROSS_FIELD	0x0f
	#define P_C_MIXER_CROSS_R_TO_R	0x01		/* R W	Mixer		Connect Right to Right				*/
	#define P_C_MIXER_CROSS_L_TO_R	0x02		/* R W	Mixer		Connect Left  to Right				*/
	#define P_C_MIXER_CROSS_R_TO_L	0x04		/* R W	Mixer		Connect Right to Left				*/
	#define P_C_MIXER_CROSS_L_TO_L	0x08		/* R W	Mixer		Connect Left  to Left				*/
	#define P_C_PCM_DAC_MODE	0x10		/* R W	PCM		Playback (DAC) mode				*/
	#define P_C_PCM_ADC_MODE	0x00		/* R W	PCM		Record (ADC) mode				*/
	#define P_C_PCM_MONO		0x20		/* R W	PCM		Mono mode					*/
	#define P_C_PCM_STEREO		0x00		/* R W	PCM		Stereo mode					*/
	#define P_C_PCM_ENABLE		0x40		/* R W	PCM		Enable PCM engine				*/
	#define P_C_PCM_DMA_ENABLE	0x80		/* R W	PCM		Enable DRQ					*/

#define SAMPLE_COUNTER_CONTROL		0x138B		/* R W	PCM		Sample counter control register			*/
	#define S_C_C_SQUARE_WAVE	0x04		/* R W	PCM		Square wave generator (use for sample rate)	*/
	#define S_C_C_RATE		0x06		/* R W	PCM		Rate generator (use for sample buffer count)	*/
	#define S_C_C_LSB_THEN_MSB	0x30		/* R W	PCM		Change all 16 bits, LSB first, then MSB		*/

	/* MVD101 and SDK documentations have S_C_C_SAMPLE_RATE and S_C_C_SAMPLE_BUFFER transposed. Only one works :-) */
	#define S_C_C_SAMPLE_RATE	0x00		/* R W	PCM		Select sample rate timer			*/
	#define S_C_C_SAMPLE_BUFFER	0x40		/* R W	PCM		Select sample buffer counter			*/

	#define S_C_C_PC_SPEAKER	0x80		/* R W	PCM		Select PC speaker counter			*/

#define SAMPLE_RATE_TIMER		0x1388		/*   W	PCM		Sample rate timer register (PCM wait interval)	*/
#define SAMPLE_BUFFER_COUNTER		0x1389		/* R W	PCM		Sample buffer counter (DMA buffer size)		*/

#define MIDI_CONTROL			0x178b		/* R W  MIDI		Midi control register				*/
	#define M_C_ENA_TSTAMP_IRQ	0x01		/* R W	MIDI		Enable Time Stamp Interrupts			*/
	#define M_C_ENA_TME_COMP_IRQ	0x02		/* R W  MIDI		Enable time compare interrupts			*/
	#define M_C_ENA_INPUT_IRQ	0x04		/* R W  MIDI		Enable input FIFO interrupts			*/
	#define M_C_ENA_OUTPUT_IRQ	0x08		/* R W  MIDI		Enable output FIFO interrupts			*/
	#define M_C_ENA_OUTPUT_HALF_IRQ	0x10		/* R W  MIDI		Enable output FIFO half full interrupts		*/
	#define M_C_RESET_INPUT_FIFO	0x20		/* R W  MIDI		Reset input FIFO pointer			*/
	#define M_C_RESET_OUTPUT_FIFO	0x40		/* R W  MIDI		Reset output FIFO pointer			*/
	#define M_C_ENA_THRU_MODE	0x80		/* R W  MIDI		Echo input to output (THRU)			*/

#define MIDI_STATUS			0x1B88		/* R W  MIDI		Midi (interrupt) status register		*/
	#define M_S_TIMESTAMP		0x01		/* R W  MIDI		Midi time stamp interrupt occurred		*/
	#define M_S_COMPARE		0x02		/* R W  MIDI		Midi compare time interrupt occurred		*/
	#define M_S_INPUT_AVAIL		0x04		/* R W  MIDI		Midi input data available interrupt occurred	*/
	#define M_S_OUTPUT_EMPTY	0x08		/* R W  MIDI		Midi output FIFO empty interrupt occurred	*/
	#define M_S_OUTPUT_HALF_EMPTY	0x10		/* R W  MIDI		Midi output FIFO half empty interrupt occurred	*/
	#define M_S_INPUT_OVERRUN	0x20		/* R W  MIDI		Midi input overrun error occurred		*/
	#define M_S_OUTPUT_OVERRUN	0x40		/* R W  MIDI 		Midi output overrun error occurred		*/
	#define M_S_FRAMING_ERROR	0x80		/* R W  MIDI		Midi input framing error occurred		*/

#define MIDI_FIFO_STATUS		0x1B89		/* R W  MIDI		Midi fifo status				*/
#define MIDI_DATA			0x178A		/* R W  MIDI		Midi data register				*/

