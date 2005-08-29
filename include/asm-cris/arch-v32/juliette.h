#ifndef _ASM_JULIETTE_H
#define _ASM_JULIETTE_H

/* juliette _IOC_TYPE, bits 8 to 15 in ioctl cmd */

#define JULIOCTYPE 42

/* supported ioctl _IOC_NR's */

#define JULSTARTDMA      0x1        /* start a picture asynchronously */

/* set parameters */

#define SETDEFAULT        0x2 /* CCD/VIDEO/SS1M */
#define SETPARAMETERS     0x3 /* CCD/VIDEO      */
#define SETSIZE           0x4 /* CCD/VIDEO/SS1M */
#define SETCOMPRESSION    0x5 /* CCD/VIDEO/SS1M */
#define SETCOLORLEVEL     0x6 /* CCD/VIDEO      */
#define SETBRIGHTNESS     0x7 /* CCD            */
#define SETROTATION       0x8 /* CCD            */
#define SETTEXT           0x9 /* CCD/VIDEO/SS1M */
#define SETCLOCK          0xa /* CCD/VIDEO/SS1M */
#define SETDATE           0xb /* CCD/VIDEO/SS1M */
#define SETTIMEFORMAT     0xc /* CCD/VIDEO/SS1M */
#define SETDATEFORMAT     0xd /*     VIDEO      */
#define SETTEXTALIGNMENT  0xe /*     VIDEO      */
#define SETFPS            0xf /* CCD/VIDEO/SS1M */
#define SETVGA           0xff /*     VIDEO      */
#define SETCOMMENT       0xfe /* CCD/VIDEO      */

/* get parameters */

#define GETDRIVERTYPE    0x10 /* CCD/VIDEO/SS1M */
#define GETNBROFCAMERAS  0x11 /* CCD/VIDEO/SS1M */
#define GETPARAMETERS    0x12 /* CCD/VIDEO/SS1M */
#define GETBUFFERSIZE    0x13 /* CCD/VIDEO/SS1M */
#define GETVIDEOTYPE     0x14 /*     VIDEO/SS1M */
#define GETVIDEOSIGNAL   0x15 /*     VIDEO      */
#define GETMODULATION    0x16 /*     VIDEO      */
#define GETDCYVALUES     0xa0 /* CCD      /SS1M */
#define GETDCYWIDTH      0xa1 /* CCD      /SS1M */
#define GETDCYHEIGHT     0xa2 /* CCD      /SS1M */
#define GETSIZE          0xa3 /* CCD/VIDEO      */
#define GETCOMPRESSION   0xa4 /* CCD/VIDEO      */

/* detect and get parameters */

#define DETECTMODULATION  0x17 /*     VIDEO      */
#define DETECTVIDEOTYPE   0x18 /*     VIDEO      */
#define DETECTVIDEOSIGNAL 0x19 /*     VIDEO      */

/* configure default parameters */

#define CONFIGUREDEFAULT  0x20 /* CCD/VIDEO/SS1M */
#define DEFSIZE           0x21 /* CCD/VIDEO/SS1M */
#define DEFCOMPRESSION    0x22 /* CCD/VIDEO/SS1M */
#define DEFCOLORLEVEL     0x23 /* CCD/VIDEO      */
#define DEFBRIGHTNESS     0x24 /* CCD            */
#define DEFROTATION       0x25 /* CCD            */
#define DEFWHITEBALANCE   0x26 /* CCD            */
#define DEFEXPOSURE       0x27 /* CCD            */
#define DEFAUTOEXPWINDOW  0x28 /* CCD            */
#define DEFTEXT           0x29 /* CCD/VIDEO/SS1M */
#define DEFCLOCK          0x2a /* CCD/VIDEO/SS1M */
#define DEFDATE           0x2b /* CCD/VIDEO/SS1M */
#define DEFTIMEFORMAT     0x2c /* CCD/VIDEO/SS1M */
#define DEFDATEFORMAT     0x2d /*     VIDEO      */
#define DEFTEXTALIGNMENT  0x2e /*     VIDEO      */
#define DEFFPS            0x2f /* CCD/VIDEO/SS1M */
#define DEFTEXTSTRING     0x30 /* CCD/VIDEO/SS1M */
#define DEFHEADERINFO     0x31 /* CCD/VIDEO/SS1M */
#define DEFWEXAR          0x32 /* CCD            */
#define DEFLINEDELAY      0x33 /* CCD            */
#define DEFDISABLEDVIDEO  0x34 /*     VIDEO      */
#define DEFVIDEOTYPE      0x35 /*     VIDEO      */
#define DEFMODULATION     0x36 /*     VIDEO      */
#define DEFXOFFSET        0x37 /*     VIDEO      */
#define DEFYOFFSET        0x38 /*     VIDEO      */
#define DEFYCMODE         0x39 /*     VIDEO      */
#define DEFVCRMODE        0x3a /*     VIDEO      */
#define DEFSTOREDCYVALUES 0x3b /* CCD/VIDEO/SS1M */
#define DEFWCDS           0x3c /* CCD            */
#define DEFVGA            0x3d /*     VIDEO      */
#define DEFCOMMENT        0x3e /* CCD/VIDEO      */
#define DEFCOMMENTSIZE    0x3f /* CCD/VIDEO      */
#define DEFCOMMENTTEXT    0x50 /* CCD/VIDEO      */
#define DEFSTOREDCYTEXT   0x51 /*     VIDEO      */


#define JULABORTDMA       0x70 /* Abort current DMA transfer */

/* juliette general i/o port */

#define JIO_READBITS      0x40 /* read and return current port bits */
#define JIO_SETBITS       0x41 /* set bits marked by 1 in the argument */
#define JIO_CLRBITS       0x42 /* clr bits marked by 1 in the argument */
#define JIO_READDIR       0x43 /* read direction, 0=input 1=output */
#define JIO_SETINPUT      0x44 /* set direction, 0=unchanged 1=input
                                  returns current dir */
#define JIO_SETOUTPUT     0x45 /* set direction, 0=unchanged 1=output
                                  returns current dir */

/**** YumYum internal adresses ****/

/* Juliette buffer addresses */

#define BUFFER1_VIDEO   0x1100
#define BUFFER2_VIDEO   0x2800
#define ACDC_BUFF_VIDEO 0x0aaa
#define BUFFER1         0x1700
#define BUFFER2         0x2b01
#define ACDC_BUFFER     0x1200
#define BUFFER1_SS1M    0x1100
#define BUFFER2_SS1M    0x2800
#define ACDC_BUFF_SS1M  0x0900

/* Juliette parameter memory addresses */

#define PA_BUFFER_CNT     0x3f09 /* CCD/VIDEO */
#define PA_CCD_BUFFER     0x3f10 /* CCD       */
#define PA_VIDEO_BUFFER   0x3f10 /*     VIDEO */
#define PA_DCT_BUFFER     0x3f11 /* CCD/VIDEO */
#define PA_TEMP           0x3f12 /* CCD/VIDEO */
#define PA_VIDEOLINE_RD   0x3f13 /*     VIDEO */
#define PA_VIDEOLINE_WR   0x3f14 /*     VIDEO */
#define PA_VI_HDELAY0     0x3f15 /*     VIDEO */
#define PA_VI_VDELAY0     0x3f16 /*     VIDEO */
#define PA_VI_HDELAY1     0x3f17 /*     VIDEO */
#define PA_VI_VDELAY1     0x3f18 /*     VIDEO */
#define PA_VI_HDELAY2     0x3f19 /*     VIDEO */
#define PA_VI_VDELAY2     0x3f1a /*     VIDEO */
#define PA_VI_HDELAY3     0x3f1b /*     VIDEO */
#define PA_VI_VDELAY3     0x3f1c /*     VIDEO */
#define PA_VI_CTRL        0x3f20 /*     VIDEO */
#define PA_JPEG_CTRL      0x3f22 /* CCD/VIDEO */
#define PA_BUFFER_SIZE    0x3f24 /* CCD/VIDEO */
#define PA_PAL_NTSC       0x3f25 /*     VIDEO */
#define PA_MACROBLOCKS    0x3f26 /* CCD/VIDEO */
#define PA_COLOR          0x3f27 /*     VIDEO */
#define PA_MEMCH1CNT2     0x3f28 /* CCD/VIDEO */
#define PA_MEMCH1CNT3     0x3f29 /*     VIDEO */
#define PA_MEMCH1STR2     0x3f2a /* CCD/VIDEO */
#define PA_MEMCH1STR3     0x3f2b /*     VIDEO */
#define PA_BUFFERS        0x3f2c /* CCD/VIDEO */
#define PA_PROGRAM        0x3f2d /* CCD/VIDEO */
#define PA_ROTATION       0x3f2e /* CCD       */
#define PA_PC             0x3f30 /* CCD/VIDEO */
#define PA_PC2            0x3f31 /*     VIDEO */
#define PA_ODD_LINE       0x3f32 /*     VIDEO */
#define PA_EXP_DELAY      0x3f34 /* CCD       */
#define PA_MACROBLOCK_CNT 0x3f35 /* CCD/VIDEO */
#define PA_DRAM_PTR1_L    0x3f36 /* CCD/VIDEO */
#define PA_CLPOB_CNT      0x3f37 /* CCD       */
#define PA_DRAM_PTR1_H    0x3f38 /* CCD/VIDEO */
#define PA_DRAM_PTR2_L    0x3f3a /*     VIDEO */
#define PA_DRAM_PTR2_H    0x3f3c /*     VIDEO */
#define PA_CCD_LINE_CNT   0x3f3f /* CCD       */
#define PA_VIDEO_LINE_CNT 0x3f3f /*     VIDEO */
#define PA_TEXT           0x3f41 /* CCD/VIDEO */
#define PA_CAMERA_CHANGED 0x3f42 /*     VIDEO */
#define PA_TEXTALIGNMENT  0x3f43 /*     VIDEO */
#define PA_DISABLED       0x3f44 /*     VIDEO */
#define PA_MACROBLOCKTEXT 0x3f45 /*     VIDEO */
#define PA_VGA            0x3f46 /*     VIDEO */
#define PA_ZERO           0x3ffe /*     VIDEO */
#define PA_NULL           0x3fff /* CCD/VIDEO */

typedef enum {
	jpeg  = 0,
	dummy = 1
} request_type;

typedef enum {
	hugesize  = 0,
	fullsize  = 1,
	halfsize  = 2,
	fieldsize = 3
} size_type;

typedef enum {
	min       = 0,
	low       = 1,
	medium    = 2,
	high      = 3,
	very_high = 4,
	very_low  = 5,
	q1        = 6,
	q2        = 7,
	q3        = 8,
	q4        = 9,
	q5        = 10,
	q6        = 11
} compr_type;

typedef enum {
	deg_0   = 0,
	deg_180 = 1,
	deg_90  = 2,
	deg_270 = 3
} rotation_type;

typedef enum {
	auto_white    = 0,
	hold          = 1,
	fixed_outdoor = 2,
	fixed_indoor  = 3,
	fixed_fluor   = 4
} white_balance_type;

typedef enum {
	auto_exp  = 0,
	fixed_exp = 1
} exposure_type;

typedef enum {
	no_window = 0,
	center    = 1,
	top       = 2,
	lower     = 3,
	left      = 4,
	right     = 5,
	spot      = 6,
	cw        = 7
} exp_window_type;

typedef enum {
	h_24 = 0,
	h_12 = 1,
	h_24P = 2
} hour_type;

typedef enum {
	standard = 0,
	YYYY_MM_DD = 1,
	Www_Mmm_DD_YYYY = 2,
	Www_DD_MM_YYYY = 3
} date_type;

typedef enum {
	left_align = 0,
	center_align = 1,
	right_align = 2
} alignment_type;

typedef enum {
	off = 0,
	on  = 1,
	no  = 0,
	yes = 1
} enable_type;

typedef enum {
         disabled = 0,
         enabled  = 1,
         extended = 2
} comment_type;

typedef enum {
	pal  = 0,
	ntsc = 1
} video_type;

typedef enum {
	pal_bghi_ntsc_m              = 0,
	ntsc_4_43_50hz_pal_4_43_60hz = 1,
	pal_n_ntsc_4_43_60hz         = 2,
	ntsc_n_pal_m                 = 3,
	secam_pal_4_43_60hz          = 4
} modulation_type;

typedef enum {
	cam0 = 0,
	cam1 = 1,
	cam2 = 2,
	cam3 = 3,
	quad = 32
} camera_type;

typedef enum {
	video_driver = 0,
	ccd_driver   = 1
} driver_type;

struct jul_param {
	request_type req_type;
	size_type size;
	compr_type compression;
	rotation_type rotation;
	int color_level;
	int brightness;
	white_balance_type white_balance;
	exposure_type exposure;
        exp_window_type auto_exp_window;
	hour_type time_format;
	date_type date_format;
	alignment_type text_alignment;
	enable_type text;
	enable_type clock;
	enable_type date;
	enable_type fps;
        enable_type vga;
        enable_type comment;
};

struct video_param {
	enable_type disabled;
	modulation_type modulation;
	video_type video;
	enable_type signal;
	enable_type vcr;
	int xoffset;
	int yoffset;
};

/* The juliette_request structure is used during the JULSTARTDMA asynchronous
 * picture-taking ioctl call as an argument to specify a buffer which will get
 * the final picture.
 */

struct juliette_request {
	char *buf;              /* Pointer to the buffer to hold picture data */
	unsigned int buflen;    /* Length of the above buffer */
	unsigned int size;      /* Resulting length, 0 if the picture is not ready */
};

#endif
