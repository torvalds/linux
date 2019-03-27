/*
 * extensions to ioctl_meteor.h for the bt848 cards
 *
 * $FreeBSD$
 */

#ifndef  _DEV_BKTR_IOCTL_BT848_H_
#define  _DEV_BKTR_IOCTL_BT848_H_

/*
 * frequency sets
 */
#define CHNLSET_NABCST		1
#define CHNLSET_CABLEIRC	2
#define CHNLSET_CABLEHRC	3
#define CHNLSET_WEUROPE		4
#define CHNLSET_JPNBCST         5
#define CHNLSET_JPNCABLE        6
#define CHNLSET_XUSSR           7
#define CHNLSET_AUSTRALIA       8
#define CHNLSET_FRANCE          9
#define CHNLSET_MIN	        CHNLSET_NABCST
#define CHNLSET_MAX	        CHNLSET_FRANCE


/*
 * constants for various tuner registers
 */
#define BT848_HUEMIN		(-90)
#define BT848_HUEMAX		90
#define BT848_HUECENTER		0
#define BT848_HUERANGE		179.3
#define BT848_HUEREGMIN		(-128)
#define BT848_HUEREGMAX		127
#define BT848_HUESTEPS		256

#define BT848_BRIGHTMIN		(-50)
#define BT848_BRIGHTMAX		50
#define BT848_BRIGHTCENTER	0
#define BT848_BRIGHTRANGE	99.6
#define BT848_BRIGHTREGMIN	(-128)
#define BT848_BRIGHTREGMAX	127
#define BT848_BRIGHTSTEPS	256

#define BT848_CONTRASTMIN	0
#define BT848_CONTRASTMAX	237
#define BT848_CONTRASTCENTER	100
#define BT848_CONTRASTRANGE	236.57
#define BT848_CONTRASTREGMIN	0
#define BT848_CONTRASTREGMAX	511
#define BT848_CONTRASTSTEPS	512

#define BT848_CHROMAMIN		0
#define BT848_CHROMAMAX		284
#define BT848_CHROMACENTER	100
#define BT848_CHROMARANGE	283.89
#define BT848_CHROMAREGMIN	0
#define BT848_CHROMAREGMAX	511
#define BT848_CHROMASTEPS	512

#define BT848_SATUMIN		0
#define BT848_SATUMAX		202
#define BT848_SATUCENTER	100
#define BT848_SATURANGE		201.18
#define BT848_SATUREGMIN	0
#define BT848_SATUREGMAX	511
#define BT848_SATUSTEPS		512

#define BT848_SATVMIN		0
#define BT848_SATVMAX		284
#define BT848_SATVCENTER	100
#define BT848_SATVRANGE		283.89
#define BT848_SATVREGMIN	0
#define BT848_SATVREGMAX	511
#define BT848_SATVSTEPS		512


/*
 * audio stuff
 */
#define AUDIO_TUNER		0x00	/* command for the audio routine */
#define AUDIO_EXTERN		0x01	/* don't confuse them with bit */
#define AUDIO_INTERN		0x02	/* settings */
#define AUDIO_MUTE		0x80
#define AUDIO_UNMUTE		0x81


/*
 * EEProm stuff
 */
struct eeProm {
	short		offset;
	short		count;
	unsigned char	bytes[ 256 ];
};


/*
 * XXX: this is a hack, should be in ioctl_meteor.h
 * here to avoid touching that file for now...
 */
#define	TVTUNER_SETCHNL    _IOW('x', 32, unsigned int)	/* set channel */
#define	TVTUNER_GETCHNL    _IOR('x', 32, unsigned int)	/* get channel */
#define	TVTUNER_SETTYPE    _IOW('x', 33, unsigned int)	/* set tuner type */
#define	TVTUNER_GETTYPE    _IOR('x', 33, unsigned int)	/* get tuner type */
#define	TVTUNER_GETSTATUS  _IOR('x', 34, unsigned int)	/* get tuner status */
#define	TVTUNER_SETFREQ    _IOW('x', 35, unsigned int)	/* set frequency */
#define	TVTUNER_GETFREQ    _IOR('x', 36, unsigned int)	/* get frequency */
 

#define BT848_SHUE	_IOW('x', 37, int)		/* set hue */
#define BT848_GHUE	_IOR('x', 37, int)		/* get hue */
#define	BT848_SBRIG	_IOW('x', 38, int)		/* set brightness */
#define BT848_GBRIG	_IOR('x', 38, int)		/* get brightness */
#define	BT848_SCSAT	_IOW('x', 39, int)		/* set chroma sat */
#define BT848_GCSAT	_IOR('x', 39, int)		/* get UV saturation */
#define	BT848_SCONT	_IOW('x', 40, int)		/* set contrast */
#define	BT848_GCONT	_IOR('x', 40, int)		/* get contrast */
#define	BT848_SVSAT	_IOW('x', 41, int)		/* set chroma V sat */
#define BT848_GVSAT	_IOR('x', 41, int)		/* get V saturation */
#define	BT848_SUSAT	_IOW('x', 42, int)		/* set chroma U sat */
#define BT848_GUSAT	_IOR('x', 42, int)		/* get U saturation */

#define	BT848_SCBARS	_IOR('x', 43, int)		/* set colorbar */
#define	BT848_CCBARS	_IOR('x', 44, int)		/* clear colorbar */


#define	BT848_SAUDIO	_IOW('x', 46, int)		/* set audio channel */
#define BT848_GAUDIO	_IOR('x', 47, int)		/* get audio channel */
#define	BT848_SBTSC	_IOW('x', 48, int)		/* set audio channel */

#define	BT848_GSTATUS	_IOR('x', 49, unsigned int)	/* reap status */

#define	BT848_WEEPROM	_IOWR('x', 50, struct eeProm)	/* write to EEProm */
#define	BT848_REEPROM	_IOWR('x', 51, struct eeProm)	/* read from EEProm */

#define	BT848_SIGNATURE	_IOWR('x', 52, struct eeProm)	/* read card sig */

#define	TVTUNER_SETAFC	_IOW('x', 53, int)		/* turn AFC on/off */
#define TVTUNER_GETAFC	_IOR('x', 54, int)		/* query AFC on/off */
#define BT848_SLNOTCH	_IOW('x', 55, int)		/* set luma notch */
#define BT848_GLNOTCH	_IOR('x', 56, int)		/* get luma notch */

/* Read/Write the BT848's I2C bus directly
 * b7-b0:    data (read/write)
 * b15-b8:   internal peripheral register (write)   
 * b23-b16:  i2c addr (write)
 * b31-b24:  1 = write, 0 = read 
 */
#define BT848_I2CWR     _IOWR('x', 57, unsigned long)    /* i2c read-write */

struct bktr_msp_control {
	unsigned char function;
	unsigned int  address;
	unsigned int  data;
};

#define BT848_MSP_RESET _IO('x', 76)				/* MSP chip reset */
#define BT848_MSP_READ  _IOWR('x', 77, struct bktr_msp_control)	/* MSP chip read */
#define BT848_MSP_WRITE _IOWR('x', 78, struct bktr_msp_control)	/* MSP chip write */

/* Support for radio tuner */
#define RADIO_SETMODE	 _IOW('x', 58, unsigned int)  /* set radio modes */
#define RADIO_GETMODE	 _IOR('x', 58, unsigned char)  /* get radio modes */
#define   RADIO_AFC	 0x01		/* These modes will probably not */
#define   RADIO_MONO	 0x02		/*  work on the FRxxxx. It does	 */
#define   RADIO_MUTE	 0x08		/*  work on the FMxxxx.	*/
#define RADIO_SETFREQ    _IOW('x', 59, unsigned int)  /* set frequency   */
#define RADIO_GETFREQ    _IOR('x', 59, unsigned int)  /* set frequency   */
 /*        Argument is frequency*100MHz  */

/*
 * XXX: more bad magic,
 *      we need to fix the METEORGINPUT to return something public
 *      duplicate them here for now...
 */
#define	METEOR_DEV0		0x00001000
#define	METEOR_DEV1		0x00002000
#define	METEOR_DEV2		0x00004000
#define	METEOR_DEV3		0x00008000
#define	METEOR_DEV_SVIDEO	0x00006000
/*
 * right now I don't know were to put these, but as they are suppose to be
 * a part of a common video capture interface, these should be relocated to
 * another place.  Probably most of the METEOR_xxx defines need to be
 * renamed and moved to a common header
 */

typedef enum { METEOR_PIXTYPE_RGB, METEOR_PIXTYPE_YUV,
	       METEOR_PIXTYPE_YUV_PACKED,
	       METEOR_PIXTYPE_YUV_12 } METEOR_PIXTYPE;


struct meteor_pixfmt {
	unsigned int   index;         /* Index in supported pixfmt list     */
	METEOR_PIXTYPE type;          /* What's the board gonna feed us     */
	unsigned int   Bpp;           /* Bytes per pixel                    */
	unsigned long  masks[3];      /* R,G,B or Y,U,V masks, respectively */
	unsigned       swap_bytes :1; /* Bytes  swapped within shorts       */
	unsigned       swap_shorts:1; /* Shorts swapped within longs        */
};


struct bktr_clip {
    int          x_min;
    int          x_max;
    int          y_min;
    int          y_max;
};

#define BT848_MAX_CLIP_NODE 100
struct _bktr_clip {
    struct bktr_clip x[BT848_MAX_CLIP_NODE];
};

/*
 * I'm using METEOR_xxx just because that will be common to other interface
 * and less of a surprise
 */
#define METEORSACTPIXFMT	_IOW('x', 64, int )
#define METEORGACTPIXFMT	_IOR('x', 64, int )
#define METEORGSUPPIXFMT	_IOWR('x', 65, struct meteor_pixfmt)

/* set clip list */
#define BT848SCLIP     _IOW('x', 66, struct _bktr_clip )
#define BT848GCLIP     _IOR('x', 66, struct _bktr_clip )


/* set input format */
#define BT848SFMT		_IOW('x', 67, unsigned long )
#define BT848GFMT		_IOR('x', 67, unsigned long )

/* set clear-buffer-on-start */
#define BT848SCBUF	_IOW('x', 68, int)
#define BT848GCBUF	_IOR('x', 68, int)

/* set capture area */
/* The capture area is the area of the video image which is grabbed */
/* Usually the capture area is 640x480 (768x576 PAL) pixels */
/* This area is then scaled to the dimensions the user requires */
/* using the METEORGEO ioctl */
/* However, the capture area could be 400x300 pixels from the top right */
/* corner of the video image */
struct bktr_capture_area {
   int      x_offset;
   int      y_offset;
   int      x_size;
   int      y_size;
};
#define BT848_SCAPAREA   _IOW('x', 69, struct bktr_capture_area)
#define BT848_GCAPAREA   _IOR('x', 69, struct bktr_capture_area)


/* Get channel Set */
#define BT848_MAX_CHNLSET_NAME_LEN 16
struct bktr_chnlset {
       short   index;
       short   max_channel;
       char    name[BT848_MAX_CHNLSET_NAME_LEN];
};
#define	TVTUNER_GETCHNLSET _IOWR('x', 70, struct bktr_chnlset)



/* Infra Red Remote Control */
struct bktr_remote {
       unsigned char data[3];
};
#define	REMOTE_GETKEY      _IOR('x', 71, struct bktr_remote)/*read the remote */
                                                            /*control receiver*/
                                                            /*returns raw data*/

 
/*
 * Direct access to GPIO pins. You must add BKTR_GPIO_ACCESS to your kernel
 * configuration file to use these 
 */
#define BT848_GPIO_SET_EN      _IOW('x', 72, int)      /* set gpio_out_en */
#define BT848_GPIO_GET_EN      _IOR('x', 73, int)      /* get gpio_out_en */
#define BT848_GPIO_SET_DATA    _IOW('x', 74, int)      /* set gpio_data */
#define BT848_GPIO_GET_DATA    _IOR('x', 75, int)      /* get gpio_data */



/*  XXX - Copied from /sys/pci/brktree_reg.h  */
#define BT848_IFORM_FORMAT              (0x7<<0)
# define BT848_IFORM_F_RSVD             (0x7)
# define BT848_IFORM_F_SECAM            (0x6)
# define BT848_IFORM_F_PALN             (0x5)
# define BT848_IFORM_F_PALM             (0x4)
# define BT848_IFORM_F_PALBDGHI         (0x3)
# define BT848_IFORM_F_NTSCJ            (0x2)
# define BT848_IFORM_F_NTSCM            (0x1)
# define BT848_IFORM_F_AUTO             (0x0)


#endif	/* _DEV_BKTR_IOCTL_BT848_H_ */

