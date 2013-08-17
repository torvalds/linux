#ifndef __SOUND_WAVEFRONT_H__
#define __SOUND_WAVEFRONT_H__

/*
 *  Driver for Turtle Beach Wavefront cards (Maui,Tropez,Tropez+)
 *
 *  Copyright (c) by Paul Barton-Davis <pbd@op.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#if (!defined(__GNUC__) && !defined(__GNUG__))

     You will not be able to compile this file correctly without gcc, because
     it is necessary to pack the "wavefront_alias" structure to a size
     of 22 bytes, corresponding to 16-bit alignment (as would have been
     the case on the original platform, MS-DOS). If this is not done,
     then WavePatch-format files cannot be read/written correctly.
     The method used to do this here ("__attribute__((packed)") is
     completely compiler dependent.
     
     All other wavefront_* types end up aligned to 32 bit values and
     still have the same (correct) size.

#else

     /* However, note that as of G++ 2.7.3.2, g++ was unable to
	correctly parse *type* __attribute__ tags. It will do the
	right thing if we use the "packed" attribute on each struct
	member, which has the same semantics anyway. 
     */

#endif /* __GNUC__ */

/***************************** WARNING ********************************
  PLEASE DO NOT MODIFY THIS FILE IN ANY WAY THAT AFFECTS ITS ABILITY TO 
  BE USED WITH EITHER C *OR* C++.
 **********************************************************************/

#ifndef NUM_MIDIKEYS 
#define NUM_MIDIKEYS 128
#endif  /* NUM_MIDIKEYS */

#ifndef NUM_MIDICHANNELS
#define NUM_MIDICHANNELS 16
#endif  /* NUM_MIDICHANNELS */

/* These are very useful/important. the original wavefront interface
   was developed on a 16 bit system, where sizeof(int) = 2
   bytes. Defining things like this makes the code much more portable, and
   easier to understand without having to toggle back and forth
   between a 16-bit view of the world and a 32-bit one. 
 */   

#ifndef __KERNEL__
/* keep them for compatibility */
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef char s8;
typedef unsigned char u8;
typedef s16 INT16;
typedef u16 UINT16;
typedef s32 INT32;
typedef u32 UINT32;
typedef s8 CHAR8;
typedef u8 UCHAR8;
#endif

/* Pseudo-commands not part of the WaveFront command set.
   These are used for various driver controls and direct
   hardware control.
 */

#define WFC_DEBUG_DRIVER                0
#define WFC_FX_IOCTL                    1
#define WFC_PATCH_STATUS                2
#define WFC_PROGRAM_STATUS              3
#define WFC_SAMPLE_STATUS               4
#define WFC_DISABLE_INTERRUPTS          5
#define WFC_ENABLE_INTERRUPTS           6
#define WFC_INTERRUPT_STATUS            7
#define WFC_ROMSAMPLES_RDONLY           8
#define WFC_IDENTIFY_SLOT_TYPE          9

/* Wavefront synth commands
 */

#define WFC_DOWNLOAD_SAMPLE		0x80
#define WFC_DOWNLOAD_BLOCK		0x81
#define WFC_DOWNLOAD_MULTISAMPLE	0x82
#define WFC_DOWNLOAD_SAMPLE_ALIAS	0x83
#define WFC_DELETE_SAMPLE		0x84
#define WFC_REPORT_FREE_MEMORY		0x85
#define WFC_DOWNLOAD_PATCH		0x86
#define WFC_DOWNLOAD_PROGRAM		0x87
#define WFC_SET_SYNTHVOL		0x89
#define WFC_SET_NVOICES			0x8B
#define WFC_DOWNLOAD_DRUM		0x90
#define WFC_GET_SYNTHVOL		0x92
#define WFC_GET_NVOICES			0x94
#define WFC_DISABLE_CHANNEL		0x9A
#define WFC_ENABLE_CHANNEL		0x9B
#define WFC_MISYNTH_OFF			0x9D
#define WFC_MISYNTH_ON			0x9E
#define WFC_FIRMWARE_VERSION		0x9F
#define WFC_GET_NSAMPLES		0xA0
#define WFC_DISABLE_DRUM_PROGRAM	0xA2
#define WFC_UPLOAD_PATCH		0xA3
#define WFC_UPLOAD_PROGRAM		0xA4
#define WFC_SET_TUNING			0xA6
#define WFC_GET_TUNING			0xA7
#define WFC_VMIDI_ON			0xA8
#define WFC_VMIDI_OFF			0xA9
#define WFC_MIDI_STATUS			0xAA
#define WFC_GET_CHANNEL_STATUS		0xAB
#define WFC_DOWNLOAD_SAMPLE_HEADER	0xAC
#define WFC_UPLOAD_SAMPLE_HEADER	0xAD
#define WFC_UPLOAD_MULTISAMPLE		0xAE
#define WFC_UPLOAD_SAMPLE_ALIAS		0xAF
#define WFC_IDENTIFY_SAMPLE_TYPE	0xB0
#define WFC_DOWNLOAD_EDRUM_PROGRAM	0xB1
#define WFC_UPLOAD_EDRUM_PROGRAM	0xB2
#define WFC_SET_EDRUM_CHANNEL		0xB3
#define WFC_INSTOUT_LEVELS		0xB4
#define WFC_PEAKOUT_LEVELS		0xB5
#define WFC_REPORT_CHANNEL_PROGRAMS	0xB6
#define WFC_HARDWARE_VERSION		0xCF
#define WFC_UPLOAD_SAMPLE_PARAMS	0xD7
#define WFC_DOWNLOAD_OS			0xF1
#define WFC_NOOP                        0xFF

#define WF_MAX_SAMPLE   512
#define WF_MAX_PATCH    256
#define WF_MAX_PROGRAM  128

#define WF_SECTION_MAX  44   /* longest OS section length */

/* # of bytes we send to the board when sending it various kinds of
   substantive data, such as samples, patches and programs.
*/

#define WF_PROGRAM_BYTES 32
#define WF_PATCH_BYTES 132
#define WF_SAMPLE_BYTES 27
#define WF_SAMPLE_HDR_BYTES 25
#define WF_ALIAS_BYTES 25
#define WF_DRUM_BYTES 9
#define WF_MSAMPLE_BYTES 259 /* (MIDI_KEYS * 2) + 3 */

#define WF_ACK     0x80
#define WF_DMA_ACK 0x81

/* OR-values for MIDI status bits */

#define WF_MIDI_VIRTUAL_ENABLED 0x1
#define WF_MIDI_VIRTUAL_IS_EXTERNAL 0x2
#define WF_MIDI_IN_TO_SYNTH_DISABLED 0x4

/* slot indexes for struct address_info: makes code a little more mnemonic */

#define WF_SYNTH_SLOT         0
#define WF_INTERNAL_MIDI_SLOT 1
#define WF_EXTERNAL_MIDI_SLOT 2

/* Magic MIDI bytes used to switch I/O streams on the ICS2115 MPU401
   emulation. Note these NEVER show up in output from the device and
   should NEVER be used in input unless Virtual MIDI mode has been 
   disabled. If they do show up as input, the results are unpredictable.
*/

#define WF_EXTERNAL_SWITCH  0xFD
#define WF_INTERNAL_SWITCH  0xF9

/* Debugging flags */

#define WF_DEBUG_CMD 0x1
#define WF_DEBUG_DATA 0x2
#define WF_DEBUG_LOAD_PATCH 0x4
#define WF_DEBUG_IO 0x8

/* WavePatch file format stuff */

#define WF_WAVEPATCH_VERSION     120;  /*  Current version number (1.2)  */
#define WF_MAX_COMMENT           64    /*  Comment length */
#define WF_NUM_LAYERS            4
#define WF_NAME_LENGTH           32
#define WF_SOURCE_LENGTH         260

#define BankFileID     "Bank"
#define DrumkitFileID  "DrumKit"
#define ProgramFileID  "Program"

struct wf_envelope
{
    u8 attack_time:7;
    u8 Unused1:1;

    u8 decay1_time:7;
    u8 Unused2:1;

    u8 decay2_time:7;
    u8 Unused3:1;

    u8 sustain_time:7;
    u8 Unused4:1;

    u8 release_time:7;
    u8 Unused5:1;

    u8 release2_time:7;
    u8 Unused6:1;

    s8 attack_level;
    s8 decay1_level;
    s8 decay2_level;
    s8 sustain_level;
    s8 release_level;

    u8 attack_velocity:7;
    u8 Unused7:1;

    u8 volume_velocity:7;
    u8 Unused8:1;

    u8 keyboard_scaling:7;
    u8 Unused9:1;
};
typedef struct wf_envelope wavefront_envelope;

struct wf_lfo
{
    u8 sample_number;

    u8 frequency:7;
    u8 Unused1:1;

    u8 am_src:4;
    u8 fm_src:4;

    s8 fm_amount;
    s8 am_amount;
    s8 start_level;
    s8 end_level;

    u8 ramp_delay:7;
    u8 wave_restart:1; /* for LFO2 only */

    u8 ramp_time:7;
    u8 Unused2:1;
};
typedef struct wf_lfo wavefront_lfo;

struct wf_patch
{
    s16  frequency_bias;         /*  ** THIS IS IN MOTOROLA FORMAT!! ** */

    u8 amplitude_bias:7;
    u8 Unused1:1;

    u8 portamento:7;
    u8 Unused2:1;

    u8 sample_number;

    u8 pitch_bend:4;
    u8 sample_msb:1;
    u8 Unused3:3;

    u8 mono:1;
    u8 retrigger:1;
    u8 nohold:1;
    u8 restart:1;
    u8 filterconfig:2; /* SDK says "not used" */
    u8 reuse:1;
    u8 reset_lfo:1;    

    u8 fm_src2:4;
    u8 fm_src1:4;   

    s8 fm_amount1;
    s8 fm_amount2;

    u8 am_src:4;
    u8 Unused4:4;

    s8 am_amount;

    u8 fc1_mode:4;
    u8 fc2_mode:4;

    s8 fc1_mod_amount;
    s8 fc1_keyboard_scaling;
    s8 fc1_bias;
    s8 fc2_mod_amount;
    s8 fc2_keyboard_scaling;
    s8 fc2_bias;

    u8 randomizer:7;
    u8 Unused5:1;

    struct wf_envelope envelope1;
    struct wf_envelope envelope2;
    struct wf_lfo lfo1;
    struct wf_lfo lfo2;
};
typedef struct wf_patch wavefront_patch;

struct wf_layer
{
    u8 patch_number;

    u8 mix_level:7;
    u8 mute:1;

    u8 split_point:7;
    u8 play_below:1;

    u8 pan_mod_src:2;
    u8 pan_or_mod:1;
    u8 pan:4;
    u8 split_type:1;
};
typedef struct wf_layer wavefront_layer;

struct wf_program
{
    struct wf_layer layer[WF_NUM_LAYERS];
};
typedef struct wf_program wavefront_program;

struct wf_sample_offset
{
    s32 Fraction:4;
    s32 Integer:20;
    s32 Unused:8;
};
typedef struct wf_sample_offset wavefront_sample_offset;          
     
/* Sample slot types */

#define WF_ST_SAMPLE      0
#define WF_ST_MULTISAMPLE 1
#define WF_ST_ALIAS       2
#define WF_ST_EMPTY       3

/* pseudo's */

#define WF_ST_DRUM        4
#define WF_ST_PROGRAM     5
#define WF_ST_PATCH       6
#define WF_ST_SAMPLEHDR   7

#define WF_ST_MASK        0xf

/* Flags for slot status. These occupy the upper bits of the same byte
   as a sample type.
*/

#define WF_SLOT_USED      0x80   /* XXX don't rely on this being accurate */
#define WF_SLOT_FILLED    0x40
#define WF_SLOT_ROM       0x20

#define WF_SLOT_MASK      0xf0

/* channel constants */

#define WF_CH_MONO  0
#define WF_CH_LEFT  1
#define WF_CH_RIGHT 2

/* Sample formats */

#define LINEAR_16BIT 0
#define WHITE_NOISE  1
#define LINEAR_8BIT  2
#define MULAW_8BIT   3

#define WF_SAMPLE_IS_8BIT(smpl) ((smpl)->SampleResolution&2)


/* 

  Because most/all of the sample data we pass in via pointers has
  never been copied (just mmap-ed into user space straight from the
  disk), it would be nice to allow handling of multi-channel sample
  data without forcing user-level extraction of the relevant bytes.
  
  So, we need a way of specifying which channel to use (the WaveFront
  only handles mono samples in a given slot), and the only way to do
  this without using some struct other than wavefront_sample as the
  interface is the awful hack of using the unused bits in a
  wavefront_sample:
  
  Val      Meaning
  ---      -------
  0        no channel selection (use channel 1, sample is MONO)
  1        use first channel, and skip one
  2        use second channel, and skip one
  3        use third channel, and skip two
  4        use fourth channel, skip three
  5        use fifth channel, skip four
  6        use six channel, skip five


  This can handle up to 4 channels, and anyone downloading >4 channels
  of sample data just to select one of them needs to find some tools
  like sox ...

  NOTE: values 0, 1 and 2 correspond to WF_CH_* above. This is 
  important.

*/

#define WF_SET_CHANNEL(samp,chn) \
 (samp)->Unused1 = chn & 0x1; \
 (samp)->Unused2 = chn & 0x2; \
 (samp)->Unused3 = chn & 0x4 
  
#define WF_GET_CHANNEL(samp) \
  (((samp)->Unused3 << 2)|((samp)->Unused2<<1)|(samp)->Unused1)
  
typedef struct wf_sample {
    struct wf_sample_offset sampleStartOffset;
    struct wf_sample_offset loopStartOffset;
    struct wf_sample_offset loopEndOffset;
    struct wf_sample_offset sampleEndOffset;
    s16 FrequencyBias;
    u8 SampleResolution:2;  /* sample_format */
    u8 Unused1:1;
    u8 Loop:1;
    u8 Bidirectional:1;
    u8 Unused2:1;
    u8 Reverse:1;
    u8 Unused3:1;
} wavefront_sample;

typedef struct wf_multisample {
    s16 NumberOfSamples;   /* log2 of the number of samples */
    s16 SampleNumber[NUM_MIDIKEYS];
} wavefront_multisample;

typedef struct wf_alias {
    s16 OriginalSample;

    struct wf_sample_offset sampleStartOffset;
    struct wf_sample_offset loopStartOffset;
    struct wf_sample_offset sampleEndOffset;
    struct wf_sample_offset loopEndOffset;

    s16  FrequencyBias;

    u8 SampleResolution:2;
    u8 Unused1:1;
    u8 Loop:1;
    u8 Bidirectional:1;
    u8 Unused2:1;
    u8 Reverse:1;
    u8 Unused3:1;
    
    /* This structure is meant to be padded only to 16 bits on their
       original. Of course, whoever wrote their documentation didn't
       realize that sizeof(struct) can be >=
       sum(sizeof(struct-fields)) and so thought that giving a C level
       description of the structs used in WavePatch files was
       sufficient. I suppose it was, as long as you remember the 
       standard 16->32 bit issues.
    */

    u8 sixteen_bit_padding;
} __attribute__((packed)) wavefront_alias;

typedef struct wf_drum {
    u8 PatchNumber;
    u8 MixLevel:7;
    u8 Unmute:1;
    u8 Group:4;
    u8 Unused1:4;
    u8 PanModSource:2;
    u8 PanModulated:1;
    u8 PanAmount:4;
    u8 Unused2:1;
} wavefront_drum;

typedef struct wf_drumkit {
    struct wf_drum drum[NUM_MIDIKEYS];
} wavefront_drumkit;

typedef struct wf_channel_programs {
    u8 Program[NUM_MIDICHANNELS];
} wavefront_channel_programs;

/* How to get MIDI channel status from the data returned by
   a WFC_GET_CHANNEL_STATUS command (a struct wf_channel_programs)
*/

#define WF_CHANNEL_STATUS(ch,wcp) (wcp)[(ch/7)] & (1<<((ch)%7))

typedef union wf_any {
    wavefront_sample s;
    wavefront_multisample ms;
    wavefront_alias a;
    wavefront_program pr;
    wavefront_patch p;
    wavefront_drum d;
} wavefront_any;

/* Hannu Solvainen hoped that his "patch_info" struct in soundcard.h
   might work for other wave-table based patch loading situations.
   Alas, his fears were correct. The WaveFront doesn't even come with
   just "patches", but several different kind of structures that
   control the sound generation process.
 */

typedef struct wf_patch_info {
    
    /* the first two fields are used by the OSS "patch loading" interface
       only, and are unused by the current user-level library.
    */

    s16   key;               /* Use WAVEFRONT_PATCH here */
    u16  devno;             /* fill in when sending */
    u8  subkey;            /* WF_ST_{SAMPLE,ALIAS,etc.} */

#define WAVEFRONT_FIND_FREE_SAMPLE_SLOT 999

    u16  number;            /* patch/sample/prog number */

    u32  size;              /* size of any data included in 
				  one of the fields in `hdrptr', or
				  as `dataptr'.

				  NOTE: for actual samples, this is
				  the size of the *SELECTED CHANNEL*
				  even if more data is actually available.
				  
				  So, a stereo sample (2 channels) of
				  6000 bytes total has `size' = 3000.

				  See the macros and comments for
				  WF_{GET,SET}_CHANNEL above.

			       */
    wavefront_any __user *hdrptr;      /* user-space ptr to hdr bytes */
    u16 __user *dataptr;            /* actual sample data */

    wavefront_any hdr;          /* kernel-space copy of hdr bytes */         
} wavefront_patch_info;

/* The maximum number of bytes we will ever move to or from user space
   in response to a WFC_* command.  This obviously doesn't cover
   actual sample data.
*/

#define WF_MAX_READ sizeof(wavefront_multisample)
#define WF_MAX_WRITE sizeof(wavefront_multisample)

/*
   This allows us to execute any WF command except the download/upload
   ones, which are handled differently due to copyin/copyout issues as
   well as data-nybbling to/from the card.
 */

typedef struct wavefront_control {
    int cmd;                           /* WFC_* */
    char status;                       /* return status to user-space */
    unsigned char rbuf[WF_MAX_READ];   /* bytes read from card */
    unsigned char wbuf[WF_MAX_WRITE];  /* bytes written to card */
} wavefront_control;

#define WFCTL_WFCMD    0x1
#define WFCTL_LOAD_SPP 0x2

/* Modulator table */

#define WF_MOD_LFO1      0
#define WF_MOD_LFO2      1
#define WF_MOD_ENV1      2
#define WF_MOD_ENV2      3
#define WF_MOD_KEYBOARD  4
#define WF_MOD_LOGKEY    5
#define WF_MOD_VELOCITY  6
#define WF_MOD_LOGVEL    7
#define WF_MOD_RANDOM    8
#define WF_MOD_PRESSURE  9
#define WF_MOD_MOD_WHEEL 10
#define WF_MOD_1         WF_MOD_MOD_WHEEL 
#define WF_MOD_BREATH    11
#define WF_MOD_2         WF_MOD_BREATH
#define WF_MOD_FOOT      12
#define WF_MOD_4         WF_MOD_FOOT
#define WF_MOD_VOLUME    13
#define WF_MOD_7         WF_MOD_VOLUME
#define WF_MOD_PAN       14
#define WF_MOD_10        WF_MOD_PAN
#define WF_MOD_EXPR      15
#define WF_MOD_11        WF_MOD_EXPR

/* FX-related material */

typedef struct wf_fx_info {
    int request;             /* see list below */
    long data[4];             /* we don't need much */
} wavefront_fx_info;

/* support for each of these will be forthcoming once I or someone 
   else has figured out which of the addresses on page 6 and page 7 of 
   the YSS225 control each parameter. Incidentally, these come from
   the Windows driver interface, but again, Turtle Beach didn't
   document the API to use them.
*/

#define WFFX_SETOUTGAIN		        0
#define WFFX_SETSTEREOOUTGAIN		1
#define WFFX_SETREVERBIN1GAIN		2
#define WFFX_SETREVERBIN2GAIN		3
#define WFFX_SETREVERBIN3GAIN		4
#define WFFX_SETCHORUSINPORT		5
#define WFFX_SETREVERBIN1PORT		6
#define WFFX_SETREVERBIN2PORT		7
#define WFFX_SETREVERBIN3PORT		8
#define WFFX_SETEFFECTPORT		9
#define WFFX_SETAUXPORT		        10
#define WFFX_SETREVERBTYPE		11
#define WFFX_SETREVERBDELAY		12
#define WFFX_SETCHORUSLFO		13
#define WFFX_SETCHORUSPMD		14
#define WFFX_SETCHORUSAMD		15
#define WFFX_SETEFFECT		        16
#define WFFX_SETBASEALL		        17
#define WFFX_SETREVERBALL		18
#define WFFX_SETCHORUSALL		20
#define WFFX_SETREVERBDEF		22
#define WFFX_SETCHORUSDEF		23
#define WFFX_DELAYSETINGAIN		24
#define WFFX_DELAYSETFBGAIN	        25
#define WFFX_DELAYSETFBLPF		26
#define WFFX_DELAYSETGAIN		27
#define WFFX_DELAYSETTIME		28
#define WFFX_DELAYSETFBTIME		29
#define WFFX_DELAYSETALL		30
#define WFFX_DELAYSETDEF		32
#define WFFX_SDELAYSETINGAIN		33
#define WFFX_SDELAYSETFBGAIN		34
#define WFFX_SDELAYSETFBLPF		35
#define WFFX_SDELAYSETGAIN		36
#define WFFX_SDELAYSETTIME		37
#define WFFX_SDELAYSETFBTIME		38
#define WFFX_SDELAYSETALL		39
#define WFFX_SDELAYSETDEF		41
#define WFFX_DEQSETINGAIN		42
#define WFFX_DEQSETFILTER		43
#define WFFX_DEQSETALL		        44
#define WFFX_DEQSETDEF		        46
#define WFFX_MUTE		        47
#define WFFX_FLANGESETBALANCE	        48	
#define WFFX_FLANGESETDELAY		49
#define WFFX_FLANGESETDWFFX_TH		50
#define WFFX_FLANGESETFBGAIN		51
#define WFFX_FLANGESETINGAIN		52
#define WFFX_FLANGESETLFO		53
#define WFFX_FLANGESETALL		54
#define WFFX_FLANGESETDEF		56
#define WFFX_PITCHSETSHIFT		57
#define WFFX_PITCHSETBALANCE		58
#define WFFX_PITCHSETALL		59
#define WFFX_PITCHSETDEF		61
#define WFFX_SRSSETINGAIN		62
#define WFFX_SRSSETSPACE		63
#define WFFX_SRSSETCENTER		64
#define WFFX_SRSSETGAIN		        65
#define WFFX_SRSSETMODE	        	66
#define WFFX_SRSSETDEF		        68

/* Allow direct user-space control over FX memory/coefficient data.
   In theory this could be used to download the FX microprogram,
   but it would be a little slower, and involve some weird code.
 */

#define WFFX_MEMSET              69

#endif /* __SOUND_WAVEFRONT_H__ */
