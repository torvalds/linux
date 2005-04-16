/* SunOS Virtual User Input Device (VUID) compatibility */

typedef struct firm_event {
	unsigned short id;	  /* tag for this event */
	unsigned char  pair_type; /* unused by X11 */
        unsigned char  pair;	  /* unused by X11 */
        int            value;	  /* VKEY_UP, VKEY_DOWN or delta */
        struct timeval time;
} Firm_event;

enum {
    FE_PAIR_NONE,
    FE_PAIR_SET,
    FE_PAIR_DELTA,
    FE_PAIR_ABSOLUTE
};

/* VUID stream formats */
#define VUID_NATIVE     0	/* Native byte stream format */
#define VUID_FIRM_EVENT 1	/* send firm_event structures */

/* ioctls */
    /* Set input device byte stream format (any of VUID_{NATIVE,FIRM_EVENT}) */
#define VUIDSFORMAT   _IOW('v', 1, int)
    /* Retrieve input device byte stream format */
#define VUIDGFORMAT   _IOR('v', 2, int)

/* Possible tag values */
/*    mouse buttons: */
#define MS_LEFT         0x7f20
#define MS_MIDDLE       0x7f21
#define MS_RIGHT        0x7f22
/*    motion: */
#define LOC_X_DELTA     0x7f80
#define LOC_Y_DELTA     0x7f81
#define LOC_X_ABSOLUTE  0x7f82  /* X compat, unsupported */
#define LOC_Y_ABSOLUTE  0x7f83  /* X compat, unsupported */

#define VKEY_UP   0
#define VKEY_DOWN 1
