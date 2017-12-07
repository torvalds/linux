/*
 * Copyright (C) 2006-2017 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ___VBox_Graphics_VBoxVideo_h
#define ___VBox_Graphics_VBoxVideo_h

#include "vbox_err.h"

/* this should be in sync with monitorCount <xsd:maxInclusive value="64"/> in src/VBox/Main/xml/VirtualBox-settings-common.xsd */
#define VBOX_VIDEO_MAX_SCREENS 64

/*
 * The last 4096 bytes of the guest VRAM contains the generic info for all
 * DualView chunks: sizes and offsets of chunks. This is filled by miniport.
 *
 * Last 4096 bytes of each chunk contain chunk specific data: framebuffer info,
 * etc. This is used exclusively by the corresponding instance of a display driver.
 *
 * The VRAM layout:
 *     Last 4096 bytes - Adapter information area.
 *     4096 bytes aligned miniport heap (value specified in the config rouded up).
 *     Slack - what left after dividing the VRAM.
 *     4096 bytes aligned framebuffers:
 *       last 4096 bytes of each framebuffer is the display information area.
 *
 * The Virtual Graphics Adapter information in the guest VRAM is stored by the
 * guest video driver using structures prepended by VBOXVIDEOINFOHDR.
 *
 * When the guest driver writes dword 0 to the VBE_DISPI_INDEX_VBOX_VIDEO
 * the host starts to process the info. The first element at the start of
 * the 4096 bytes region should be normally be a LINK that points to
 * actual information chain. That way the guest driver can have some
 * fixed layout of the information memory block and just rewrite
 * the link to point to relevant memory chain.
 *
 * The processing stops at the END element.
 *
 * The host can access the memory only when the port IO is processed.
 * All data that will be needed later must be copied from these 4096 bytes.
 * But other VRAM can be used by host until the mode is disabled.
 *
 * The guest driver writes dword 0xffffffff to the VBE_DISPI_INDEX_VBOX_VIDEO
 * to disable the mode.
 *
 * VBE_DISPI_INDEX_VBOX_VIDEO is used to read the configuration information
 * from the host and issue commands to the host.
 *
 * The guest writes the VBE_DISPI_INDEX_VBOX_VIDEO index register, the the
 * following operations with the VBE data register can be performed:
 *
 * Operation            Result
 * write 16 bit value   NOP
 * read 16 bit value    count of monitors
 * write 32 bit value   sets the vbox command value and the command processed by the host
 * read 32 bit value    result of the last vbox command is returned
 */

#define VBOX_VIDEO_PRIMARY_SCREEN 0
#define VBOX_VIDEO_NO_SCREEN ~0

/**
 * VBVA command header.
 *
 * @todo Where does this fit in?
 */
typedef struct VBVACMDHDR {
   /** Coordinates of affected rectangle. */
   int16_t x;
   int16_t y;
   u16 w;
   u16 h;
} VBVACMDHDR;
assert_compile_size(VBVACMDHDR, 8);

/** @name VBVA ring defines.
 *
 * The VBVA ring buffer is suitable for transferring large (< 2GB) amount of
 * data. For example big bitmaps which do not fit to the buffer.
 *
 * Guest starts writing to the buffer by initializing a record entry in the
 * records queue. VBVA_F_RECORD_PARTIAL indicates that the record is being
 * written. As data is written to the ring buffer, the guest increases off32End
 * for the record.
 *
 * The host reads the records on flushes and processes all completed records.
 * When host encounters situation when only a partial record presents and
 * len_and_flags & ~VBVA_F_RECORD_PARTIAL >= VBVA_RING_BUFFER_SIZE -
 * VBVA_RING_BUFFER_THRESHOLD, the host fetched all record data and updates
 * off32Head. After that on each flush the host continues fetching the data
 * until the record is completed.
 *
 */
#define VBVA_RING_BUFFER_SIZE        (4*1024*1024 - 1024)
#define VBVA_RING_BUFFER_THRESHOLD   (4 * 1024)

#define VBVA_MAX_RECORDS (64)

#define VBVA_F_MODE_ENABLED         0x00000001u
#define VBVA_F_MODE_VRDP            0x00000002u
#define VBVA_F_MODE_VRDP_RESET      0x00000004u
#define VBVA_F_MODE_VRDP_ORDER_MASK 0x00000008u

#define VBVA_F_STATE_PROCESSING     0x00010000u

#define VBVA_F_RECORD_PARTIAL       0x80000000u

/**
 * VBVA record.
 */
typedef struct VBVARECORD {
	/** The length of the record. Changed by guest. */
	u32 len_and_flags;
} VBVARECORD;
assert_compile_size(VBVARECORD, 4);

/* The size of the information. */
/*
 * The minimum HGSMI heap size is PAGE_SIZE (4096 bytes) and is a restriction of the
 * runtime heapsimple API. Use minimum 2 pages here, because the info area also may
 * contain other data (for example struct hgsmi_host_flags structure).
 */
#ifndef VBOX_XPDM_MINIPORT
# define VBVA_ADAPTER_INFORMATION_SIZE (64*1024)
#else
#define VBVA_ADAPTER_INFORMATION_SIZE  (16*1024)
#define VBVA_DISPLAY_INFORMATION_SIZE  (64*1024)
#endif
#define VBVA_MIN_BUFFER_SIZE           (64*1024)


/* The value for port IO to let the adapter to interpret the adapter memory. */
#define VBOX_VIDEO_DISABLE_ADAPTER_MEMORY        0xFFFFFFFF

/* The value for port IO to let the adapter to interpret the adapter memory. */
#define VBOX_VIDEO_INTERPRET_ADAPTER_MEMORY      0x00000000

/* The value for port IO to let the adapter to interpret the display memory.
 * The display number is encoded in low 16 bits.
 */
#define VBOX_VIDEO_INTERPRET_DISPLAY_MEMORY_BASE 0x00010000


/* The end of the information. */
#define VBOX_VIDEO_INFO_TYPE_END          0
/* Instructs the host to fetch the next VBOXVIDEOINFOHDR at the given offset of VRAM. */
#define VBOX_VIDEO_INFO_TYPE_LINK         1
/* Information about a display memory position. */
#define VBOX_VIDEO_INFO_TYPE_DISPLAY      2
/* Information about a screen. */
#define VBOX_VIDEO_INFO_TYPE_SCREEN       3
/* Information about host notifications for the driver. */
#define VBOX_VIDEO_INFO_TYPE_HOST_EVENTS  4
/* Information about non-volatile guest VRAM heap. */
#define VBOX_VIDEO_INFO_TYPE_NV_HEAP      5
/* VBVA enable/disable. */
#define VBOX_VIDEO_INFO_TYPE_VBVA_STATUS  6
/* VBVA flush. */
#define VBOX_VIDEO_INFO_TYPE_VBVA_FLUSH   7
/* Query configuration value. */
#define VBOX_VIDEO_INFO_TYPE_QUERY_CONF32 8


#pragma pack(1)
typedef struct VBOXVIDEOINFOHDR {
	u8 u8Type;
	u8 u8Reserved;
	u16 u16Length;
} VBOXVIDEOINFOHDR;


typedef struct VBOXVIDEOINFOLINK {
	/* Relative offset in VRAM */
	s32 i32Offset;
} VBOXVIDEOINFOLINK;


/* Resides in adapter info memory. Describes a display VRAM chunk. */
typedef struct VBOXVIDEOINFODISPLAY {
	/* Index of the framebuffer assigned by guest. */
	u32 index;

	/* Absolute offset in VRAM of the framebuffer to be displayed on the monitor. */
	u32 offset;

	/* The size of the memory that can be used for the screen. */
	u32 u32FramebufferSize;

	/* The size of the memory that is used for the Display information.
	 * The information is at offset + u32FramebufferSize
	 */
	u32 u32InformationSize;

} VBOXVIDEOINFODISPLAY;


/* Resides in display info area, describes the current video mode. */
#define VBOX_VIDEO_INFO_SCREEN_F_NONE   0x00
#define VBOX_VIDEO_INFO_SCREEN_F_ACTIVE 0x01

typedef struct VBOXVIDEOINFOSCREEN {
	/* Physical X origin relative to the primary screen. */
	s32 xOrigin;

	/* Physical Y origin relative to the primary screen. */
	s32 yOrigin;

	/* The scan line size in bytes. */
	u32 line_size;

	/* Width of the screen. */
	u16 u16Width;

	/* Height of the screen. */
	u16 u16Height;

	/* Color depth. */
	u8 bitsPerPixel;

	/* VBOX_VIDEO_INFO_SCREEN_F_* */
	u8 u8Flags;
} VBOXVIDEOINFOSCREEN;

/* The guest initializes the structure to 0. The positions of the structure in the
 * display info area must not be changed, host will update the structure. Guest checks
 * the events and modifies the structure as a response to host.
 */
#define VBOX_VIDEO_INFO_HOST_EVENTS_F_NONE        0x00000000
#define VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET  0x00000080

typedef struct VBOXVIDEOINFOHOSTEVENTS {
	/* Host events. */
	u32 fu32Events;
} VBOXVIDEOINFOHOSTEVENTS;

/* Resides in adapter info memory. Describes the non-volatile VRAM heap. */
typedef struct VBOXVIDEOINFONVHEAP {
	/* Absolute offset in VRAM of the start of the heap. */
	u32 u32HeapOffset;

	/* The size of the heap. */
	u32 u32HeapSize;

} VBOXVIDEOINFONVHEAP;

/* Display information area. */
typedef struct VBOXVIDEOINFOVBVASTATUS {
	/* Absolute offset in VRAM of the start of the VBVA QUEUE. 0 to disable VBVA. */
	u32 u32QueueOffset;

	/* The size of the VBVA QUEUE. 0 to disable VBVA. */
	u32 u32QueueSize;

} VBOXVIDEOINFOVBVASTATUS;

typedef struct VBOXVIDEOINFOVBVAFLUSH {
	u32 u32DataStart;

	u32 u32DataEnd;

} VBOXVIDEOINFOVBVAFLUSH;

#define VBOX_VIDEO_QCI32_MONITOR_COUNT       0
#define VBOX_VIDEO_QCI32_OFFSCREEN_HEAP_SIZE 1

typedef struct VBOXVIDEOINFOQUERYCONF32 {
	u32 index;

	u32 value;

} VBOXVIDEOINFOQUERYCONF32;
#pragma pack()

#ifdef VBOX_WITH_VIDEOHWACCEL
#pragma pack(1)

#define VBOXVHWA_VERSION_MAJ 0
#define VBOXVHWA_VERSION_MIN 0
#define VBOXVHWA_VERSION_BLD 6
#define VBOXVHWA_VERSION_RSV 0

typedef enum {
	VBOXVHWACMD_TYPE_SURF_CANCREATE = 1,
	VBOXVHWACMD_TYPE_SURF_CREATE,
	VBOXVHWACMD_TYPE_SURF_DESTROY,
	VBOXVHWACMD_TYPE_SURF_LOCK,
	VBOXVHWACMD_TYPE_SURF_UNLOCK,
	VBOXVHWACMD_TYPE_SURF_BLT,
	VBOXVHWACMD_TYPE_SURF_FLIP,
	VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE,
	VBOXVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION,
	VBOXVHWACMD_TYPE_SURF_COLORKEY_SET,
	VBOXVHWACMD_TYPE_QUERY_INFO1,
	VBOXVHWACMD_TYPE_QUERY_INFO2,
	VBOXVHWACMD_TYPE_ENABLE,
	VBOXVHWACMD_TYPE_DISABLE,
	VBOXVHWACMD_TYPE_HH_CONSTRUCT,
	VBOXVHWACMD_TYPE_HH_RESET
#ifdef VBOX_WITH_WDDM
	, VBOXVHWACMD_TYPE_SURF_GETINFO
	, VBOXVHWACMD_TYPE_SURF_COLORFILL
#endif
	, VBOXVHWACMD_TYPE_HH_DISABLE
	, VBOXVHWACMD_TYPE_HH_ENABLE
	, VBOXVHWACMD_TYPE_HH_SAVESTATE_SAVEBEGIN
	, VBOXVHWACMD_TYPE_HH_SAVESTATE_SAVEEND
	, VBOXVHWACMD_TYPE_HH_SAVESTATE_SAVEPERFORM
	, VBOXVHWACMD_TYPE_HH_SAVESTATE_LOADPERFORM
} VBOXVHWACMD_TYPE;

/* the command processing was asynch, set by the host to indicate asynch command completion
 * must not be cleared once set, the command completion is performed by issuing a host->guest completion command
 * while keeping this flag unchanged */
#define VBOXVHWACMD_FLAG_HG_ASYNCH               0x00010000
/* asynch completion is performed by issuing the event */
#define VBOXVHWACMD_FLAG_GH_ASYNCH_EVENT         0x00000001
/* issue interrupt on asynch completion */
#define VBOXVHWACMD_FLAG_GH_ASYNCH_IRQ           0x00000002
/* guest does not do any op on completion of this command, the host may copy the command and indicate that it does not need the command anymore
 * by setting the VBOXVHWACMD_FLAG_HG_ASYNCH_RETURNED flag */
#define VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION  0x00000004
/* the host has copied the VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION command and returned it to the guest */
#define VBOXVHWACMD_FLAG_HG_ASYNCH_RETURNED      0x00020000
/* this is the host->host cmd, i.e. a configuration command posted by the host to the framebuffer */
#define VBOXVHWACMD_FLAG_HH_CMD                  0x10000000

typedef struct VBOXVHWACMD {
	VBOXVHWACMD_TYPE enmCmd; /* command type */
	volatile s32 rc; /* command result */
	s32 iDisplay; /* display index */
	volatile s32 Flags; /* ored VBOXVHWACMD_FLAG_xxx values */
	uint64_t GuestVBVAReserved1; /* field internally used by the guest VBVA cmd handling, must NOT be modified by clients */
	uint64_t GuestVBVAReserved2; /* field internally used by the guest VBVA cmd handling, must NOT be modified by clients */
	volatile u32 cRefs;
	s32 Reserved;
	union {
		struct VBOXVHWACMD *pNext;
		u32             offNext;
		uint64_t Data; /* the body is 64-bit aligned */
	} u;
	char body[1];
} VBOXVHWACMD;

#define VBOXVHWACMD_HEADSIZE() (RT_OFFSETOF(VBOXVHWACMD, body))
#define VBOXVHWACMD_SIZE_FROMBODYSIZE(_s) (VBOXVHWACMD_HEADSIZE() + (_s))
#define VBOXVHWACMD_SIZE(_tCmd) (VBOXVHWACMD_SIZE_FROMBODYSIZE(sizeof(_tCmd)))
typedef unsigned int VBOXVHWACMD_LENGTH;
typedef uint64_t VBOXVHWA_SURFHANDLE;
#define VBOXVHWA_SURFHANDLE_INVALID 0ULL
#define VBOXVHWACMD_BODY(_p, _t) ((_t*)(_p)->body)
#define VBOXVHWACMD_HEAD(_pb) ((VBOXVHWACMD*)((u8 *)(_pb) - RT_OFFSETOF(VBOXVHWACMD, body)))

typedef struct VBOXVHWA_RECTL {
	s32 left;
	s32 top;
	s32 right;
	s32 bottom;
} VBOXVHWA_RECTL;

typedef struct VBOXVHWA_COLORKEY {
	u32 low;
	u32 high;
} VBOXVHWA_COLORKEY;

typedef struct VBOXVHWA_PIXELFORMAT {
	u32 flags;
	u32 fourCC;
	union {
		u32 rgbBitCount;
		u32 yuvBitCount;
	} c;

	union {
		u32 rgbRBitMask;
		u32 yuvYBitMask;
	} m1;

	union {
		u32 rgbGBitMask;
		u32 yuvUBitMask;
	} m2;

	union {
		u32 rgbBBitMask;
		u32 yuvVBitMask;
	} m3;

	union {
		u32 rgbABitMask;
	} m4;

	u32 Reserved;
} VBOXVHWA_PIXELFORMAT;

typedef struct VBOXVHWA_SURFACEDESC {
	u32 flags;
	u32 height;
	u32 width;
	u32 pitch;
	u32 sizeX;
	u32 sizeY;
	u32 cBackBuffers;
	u32 Reserved;
	VBOXVHWA_COLORKEY DstOverlayCK;
	VBOXVHWA_COLORKEY DstBltCK;
	VBOXVHWA_COLORKEY SrcOverlayCK;
	VBOXVHWA_COLORKEY SrcBltCK;
	VBOXVHWA_PIXELFORMAT PixelFormat;
	u32 surfCaps;
	u32 Reserved2;
	VBOXVHWA_SURFHANDLE hSurf;
	uint64_t offSurface;
} VBOXVHWA_SURFACEDESC;

typedef struct VBOXVHWA_BLTFX {
	u32 flags;
	u32 rop;
	u32 rotationOp;
	u32 rotation;
	u32 fillColor;
	u32 Reserved;
	VBOXVHWA_COLORKEY DstCK;
	VBOXVHWA_COLORKEY SrcCK;
} VBOXVHWA_BLTFX;

typedef struct VBOXVHWA_OVERLAYFX {
	u32 flags;
	u32 Reserved1;
	u32 fxFlags;
	u32 Reserved2;
	VBOXVHWA_COLORKEY DstCK;
	VBOXVHWA_COLORKEY SrcCK;
} VBOXVHWA_OVERLAYFX;

#define VBOXVHWA_CAPS_BLT               0x00000040
#define VBOXVHWA_CAPS_BLTCOLORFILL      0x04000000
#define VBOXVHWA_CAPS_BLTFOURCC         0x00000100
#define VBOXVHWA_CAPS_BLTSTRETCH        0x00000200
#define VBOXVHWA_CAPS_BLTQUEUE          0x00000080

#define VBOXVHWA_CAPS_OVERLAY           0x00000800
#define VBOXVHWA_CAPS_OVERLAYFOURCC     0x00002000
#define VBOXVHWA_CAPS_OVERLAYSTRETCH    0x00004000
#define VBOXVHWA_CAPS_OVERLAYCANTCLIP   0x00001000

#define VBOXVHWA_CAPS_COLORKEY          0x00400000
#define VBOXVHWA_CAPS_COLORKEYHWASSIST  0x01000000

#define VBOXVHWA_SCAPS_BACKBUFFER       0x00000004
#define VBOXVHWA_SCAPS_COMPLEX          0x00000008
#define VBOXVHWA_SCAPS_FLIP             0x00000010
#define VBOXVHWA_SCAPS_FRONTBUFFER      0x00000020
#define VBOXVHWA_SCAPS_OFFSCREENPLAIN   0x00000040
#define VBOXVHWA_SCAPS_OVERLAY          0x00000080
#define VBOXVHWA_SCAPS_PRIMARYSURFACE   0x00000200
#define VBOXVHWA_SCAPS_SYSTEMMEMORY     0x00000800
#define VBOXVHWA_SCAPS_VIDEOMEMORY      0x00004000
#define VBOXVHWA_SCAPS_VISIBLE          0x00008000
#define VBOXVHWA_SCAPS_LOCALVIDMEM      0x10000000

#define VBOXVHWA_PF_PALETTEINDEXED8     0x00000020
#define VBOXVHWA_PF_RGB                 0x00000040
#define VBOXVHWA_PF_RGBTOYUV            0x00000100
#define VBOXVHWA_PF_YUV                 0x00000200
#define VBOXVHWA_PF_FOURCC              0x00000004

#define VBOXVHWA_LOCK_DISCARDCONTENTS   0x00002000

#define VBOXVHWA_CFG_ENABLED            0x00000001

#define VBOXVHWA_SD_BACKBUFFERCOUNT     0x00000020
#define VBOXVHWA_SD_CAPS                0x00000001
#define VBOXVHWA_SD_CKDESTBLT           0x00004000
#define VBOXVHWA_SD_CKDESTOVERLAY       0x00002000
#define VBOXVHWA_SD_CKSRCBLT            0x00010000
#define VBOXVHWA_SD_CKSRCOVERLAY        0x00008000
#define VBOXVHWA_SD_HEIGHT              0x00000002
#define VBOXVHWA_SD_PITCH               0x00000008
#define VBOXVHWA_SD_PIXELFORMAT         0x00001000
/*#define VBOXVHWA_SD_REFRESHRATE       0x00040000*/
#define VBOXVHWA_SD_WIDTH               0x00000004

#define VBOXVHWA_CKEYCAPS_DESTBLT                  0x00000001
#define VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACE          0x00000002
#define VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACEYUV       0x00000004
#define VBOXVHWA_CKEYCAPS_DESTBLTYUV               0x00000008
#define VBOXVHWA_CKEYCAPS_DESTOVERLAY              0x00000010
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYCLRSPACE      0x00000020
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYCLRSPACEYUV   0x00000040
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE     0x00000080
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYYUV           0x00000100
#define VBOXVHWA_CKEYCAPS_SRCBLT                   0x00000200
#define VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACE           0x00000400
#define VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACEYUV        0x00000800
#define VBOXVHWA_CKEYCAPS_SRCBLTYUV                0x00001000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAY               0x00002000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYCLRSPACE       0x00004000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYCLRSPACEYUV    0x00008000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE      0x00010000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYYUV            0x00020000
#define VBOXVHWA_CKEYCAPS_NOCOSTOVERLAY            0x00040000

#define VBOXVHWA_BLT_COLORFILL                      0x00000400
#define VBOXVHWA_BLT_DDFX                           0x00000800
#define VBOXVHWA_BLT_EXTENDED_FLAGS                 0x40000000
#define VBOXVHWA_BLT_EXTENDED_LINEAR_CONTENT        0x00000004
#define VBOXVHWA_BLT_EXTENDED_PRESENTATION_STRETCHFACTOR 0x00000010
#define VBOXVHWA_BLT_KEYDESTOVERRIDE                0x00004000
#define VBOXVHWA_BLT_KEYSRCOVERRIDE                 0x00010000
#define VBOXVHWA_BLT_LAST_PRESENTATION              0x20000000
#define VBOXVHWA_BLT_PRESENTATION                   0x10000000
#define VBOXVHWA_BLT_ROP                            0x00020000


#define VBOXVHWA_OVER_DDFX                          0x00080000
#define VBOXVHWA_OVER_HIDE                          0x00000200
#define VBOXVHWA_OVER_KEYDEST                       0x00000400
#define VBOXVHWA_OVER_KEYDESTOVERRIDE               0x00000800
#define VBOXVHWA_OVER_KEYSRC                        0x00001000
#define VBOXVHWA_OVER_KEYSRCOVERRIDE                0x00002000
#define VBOXVHWA_OVER_SHOW                          0x00004000

#define VBOXVHWA_CKEY_COLORSPACE                    0x00000001
#define VBOXVHWA_CKEY_DESTBLT                       0x00000002
#define VBOXVHWA_CKEY_DESTOVERLAY                   0x00000004
#define VBOXVHWA_CKEY_SRCBLT                        0x00000008
#define VBOXVHWA_CKEY_SRCOVERLAY                    0x00000010

#define VBOXVHWA_BLT_ARITHSTRETCHY                  0x00000001
#define VBOXVHWA_BLT_MIRRORLEFTRIGHT                0x00000002
#define VBOXVHWA_BLT_MIRRORUPDOWN                   0x00000004

#define VBOXVHWA_OVERFX_ARITHSTRETCHY               0x00000001
#define VBOXVHWA_OVERFX_MIRRORLEFTRIGHT             0x00000002
#define VBOXVHWA_OVERFX_MIRRORUPDOWN                0x00000004

#define VBOXVHWA_CAPS2_CANRENDERWINDOWED            0x00080000
#define VBOXVHWA_CAPS2_WIDESURFACES                 0x00001000
#define VBOXVHWA_CAPS2_COPYFOURCC                   0x00008000
/*#define VBOXVHWA_CAPS2_FLIPINTERVAL                 0x00200000*/
/*#define VBOXVHWA_CAPS2_FLIPNOVSYNC                  0x00400000*/


#define VBOXVHWA_OFFSET64_VOID        (UINT64_MAX)

typedef struct VBOXVHWA_VERSION {
	u32 maj;
	u32 min;
	u32 bld;
	u32 reserved;
} VBOXVHWA_VERSION;

#define VBOXVHWA_VERSION_INIT(_pv) do { \
		(_pv)->maj = VBOXVHWA_VERSION_MAJ; \
		(_pv)->min = VBOXVHWA_VERSION_MIN; \
		(_pv)->bld = VBOXVHWA_VERSION_BLD; \
		(_pv)->reserved = VBOXVHWA_VERSION_RSV; \
		} while(0)

typedef struct VBOXVHWACMD_QUERYINFO1 {
	union {
		struct {
			VBOXVHWA_VERSION guestVersion;
		} in;

		struct {
			u32 cfgFlags;
			u32 caps;

			u32 caps2;
			u32 colorKeyCaps;

			u32 stretchCaps;
			u32 surfaceCaps;

			u32 numOverlays;
			u32 curOverlays;

			u32 numFourCC;
			u32 reserved;
		} out;
	} u;
} VBOXVHWACMD_QUERYINFO1;

typedef struct VBOXVHWACMD_QUERYINFO2 {
	u32 numFourCC;
	u32 FourCC[1];
} VBOXVHWACMD_QUERYINFO2;

#define VBOXVHWAINFO2_SIZE(_cFourCC) RT_OFFSETOF(VBOXVHWACMD_QUERYINFO2, FourCC[_cFourCC])

typedef struct VBOXVHWACMD_SURF_CANCREATE {
	VBOXVHWA_SURFACEDESC SurfInfo;
	union {
		struct {
			u32 bIsDifferentPixelFormat;
			u32 Reserved;
		} in;

		struct {
			s32 ErrInfo;
		} out;
	} u;
} VBOXVHWACMD_SURF_CANCREATE;

typedef struct VBOXVHWACMD_SURF_CREATE {
	VBOXVHWA_SURFACEDESC SurfInfo;
} VBOXVHWACMD_SURF_CREATE;

#ifdef VBOX_WITH_WDDM
typedef struct VBOXVHWACMD_SURF_GETINFO {
	VBOXVHWA_SURFACEDESC SurfInfo;
} VBOXVHWACMD_SURF_GETINFO;
#endif

typedef struct VBOXVHWACMD_SURF_DESTROY {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hSurf;
		} in;
	} u;
} VBOXVHWACMD_SURF_DESTROY;

typedef struct VBOXVHWACMD_SURF_LOCK {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hSurf;
			uint64_t offSurface;
			u32 flags;
			u32 rectValid;
			VBOXVHWA_RECTL rect;
		} in;
	} u;
} VBOXVHWACMD_SURF_LOCK;

typedef struct VBOXVHWACMD_SURF_UNLOCK {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hSurf;
			u32 xUpdatedMemValid;
			u32 reserved;
			VBOXVHWA_RECTL xUpdatedMemRect;
		} in;
	} u;
} VBOXVHWACMD_SURF_UNLOCK;

typedef struct VBOXVHWACMD_SURF_BLT {
	uint64_t DstGuestSurfInfo;
	uint64_t SrcGuestSurfInfo;
	union {
		struct {
			VBOXVHWA_SURFHANDLE hDstSurf;
			uint64_t offDstSurface;
			VBOXVHWA_RECTL dstRect;
			VBOXVHWA_SURFHANDLE hSrcSurf;
			uint64_t offSrcSurface;
			VBOXVHWA_RECTL srcRect;
			u32 flags;
			u32 xUpdatedSrcMemValid;
			VBOXVHWA_BLTFX desc;
			VBOXVHWA_RECTL xUpdatedSrcMemRect;
		} in;
	} u;
} VBOXVHWACMD_SURF_BLT;

#ifdef VBOX_WITH_WDDM
typedef struct VBOXVHWACMD_SURF_COLORFILL {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hSurf;
			uint64_t offSurface;
			u32 reserved;
			u32 cRects;
			VBOXVHWA_RECTL aRects[1];
		} in;
	} u;
} VBOXVHWACMD_SURF_COLORFILL;
#endif

typedef struct VBOXVHWACMD_SURF_FLIP {
	uint64_t TargGuestSurfInfo;
	uint64_t CurrGuestSurfInfo;
	union {
		struct {
			VBOXVHWA_SURFHANDLE hTargSurf;
			uint64_t offTargSurface;
			VBOXVHWA_SURFHANDLE hCurrSurf;
			uint64_t offCurrSurface;
			u32 flags;
			u32 xUpdatedTargMemValid;
			VBOXVHWA_RECTL xUpdatedTargMemRect;
		} in;
	} u;
} VBOXVHWACMD_SURF_FLIP;

typedef struct VBOXVHWACMD_SURF_COLORKEY_SET {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hSurf;
			uint64_t offSurface;
			VBOXVHWA_COLORKEY CKey;
			u32 flags;
			u32 reserved;
		} in;
	} u;
} VBOXVHWACMD_SURF_COLORKEY_SET;

#define VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT 0x00000001
#define VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_DSTMEMRECT 0x00000002

typedef struct VBOXVHWACMD_SURF_OVERLAY_UPDATE {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hDstSurf;
			uint64_t offDstSurface;
			VBOXVHWA_RECTL dstRect;
			VBOXVHWA_SURFHANDLE hSrcSurf;
			uint64_t offSrcSurface;
			VBOXVHWA_RECTL srcRect;
			u32 flags;
			u32 xFlags;
			VBOXVHWA_OVERLAYFX desc;
			VBOXVHWA_RECTL xUpdatedSrcMemRect;
			VBOXVHWA_RECTL xUpdatedDstMemRect;
		} in;
	} u;
}VBOXVHWACMD_SURF_OVERLAY_UPDATE;

typedef struct VBOXVHWACMD_SURF_OVERLAY_SETPOSITION {
	union {
		struct {
			VBOXVHWA_SURFHANDLE hDstSurf;
			uint64_t offDstSurface;
			VBOXVHWA_SURFHANDLE hSrcSurf;
			uint64_t offSrcSurface;
			u32 xPos;
			u32 yPos;
			u32 flags;
			u32 reserved;
		} in;
	} u;
} VBOXVHWACMD_SURF_OVERLAY_SETPOSITION;

typedef struct VBOXVHWACMD_HH_CONSTRUCT {
	void    *pVM;
	/* VRAM info for the backend to be able to properly translate VRAM offsets */
	void    *pvVRAM;
	u32 cbVRAM;
} VBOXVHWACMD_HH_CONSTRUCT;

typedef struct VBOXVHWACMD_HH_SAVESTATE_SAVEPERFORM {
	struct SSMHANDLE * pSSM;
} VBOXVHWACMD_HH_SAVESTATE_SAVEPERFORM;

typedef struct VBOXVHWACMD_HH_SAVESTATE_LOADPERFORM {
	struct SSMHANDLE * pSSM;
} VBOXVHWACMD_HH_SAVESTATE_LOADPERFORM;

typedef void FNVBOXVHWA_HH_CALLBACK(void*);
typedef FNVBOXVHWA_HH_CALLBACK *PFNVBOXVHWA_HH_CALLBACK;

#define VBOXVHWA_HH_CALLBACK_SET(_pCmd, _pfn, _parg) \
	do { \
		(_pCmd)->GuestVBVAReserved1 = (uint64_t)(uintptr_t)(_pfn); \
		(_pCmd)->GuestVBVAReserved2 = (uint64_t)(uintptr_t)(_parg); \
	}while(0)

#define VBOXVHWA_HH_CALLBACK_GET(_pCmd) ((PFNVBOXVHWA_HH_CALLBACK)(_pCmd)->GuestVBVAReserved1)
#define VBOXVHWA_HH_CALLBACK_GET_ARG(_pCmd) ((void*)(_pCmd)->GuestVBVAReserved2)

#pragma pack()
#endif /* #ifdef VBOX_WITH_VIDEOHWACCEL */

/* All structures are without alignment. */
#pragma pack(1)

typedef struct VBVAHOSTFLAGS {
	u32 host_events;
	u32 supported_orders;
} VBVAHOSTFLAGS;

typedef struct VBVABUFFER {
	VBVAHOSTFLAGS host_flags;

	/* The offset where the data start in the buffer. */
	u32 data_offset;
	/* The offset where next data must be placed in the buffer. */
	u32 free_offset;

	/* The queue of record descriptions. */
	VBVARECORD records[VBVA_MAX_RECORDS];
	u32 first_record_index;
	u32 free_record_index;

	/* Space to leave free in the buffer when large partial records are transferred. */
	u32 partial_write_tresh;

	u32 data_len;
	u8  data[1]; /* variable size for the rest of the VBVABUFFER area in VRAM. */
} VBVABUFFER;

#define VBVA_MAX_RECORD_SIZE (128*_1M)

/* guest->host commands */
#define VBVA_QUERY_CONF32 1
#define VBVA_SET_CONF32   2
#define VBVA_INFO_VIEW    3
#define VBVA_INFO_HEAP    4
#define VBVA_FLUSH        5
#define VBVA_INFO_SCREEN  6
#define VBVA_ENABLE       7
#define VBVA_MOUSE_POINTER_SHAPE 8
#ifdef VBOX_WITH_VIDEOHWACCEL
# define VBVA_VHWA_CMD    9
#endif /* # ifdef VBOX_WITH_VIDEOHWACCEL */
#ifdef VBOX_WITH_VDMA
# define VBVA_VDMA_CTL   10 /* setup G<->H DMA channel info */
# define VBVA_VDMA_CMD    11 /* G->H DMA command             */
#endif
#define VBVA_INFO_CAPS   12 /* informs host about HGSMI caps. see struct vbva_caps below */
#define VBVA_SCANLINE_CFG    13 /* configures scanline, see VBVASCANLINECFG below */
#define VBVA_SCANLINE_INFO   14 /* requests scanline info, see VBVASCANLINEINFO below */
#define VBVA_CMDVBVA_SUBMIT  16 /* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_FLUSH   17 /* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_CTL     18 /* G->H DMA command             */
#define VBVA_QUERY_MODE_HINTS 19 /* Query most recent mode hints sent. */
/** Report the guest virtual desktop position and size for mapping host and
 * guest pointer positions. */
#define VBVA_REPORT_INPUT_MAPPING 20
/** Report the guest cursor position and query the host position. */
#define VBVA_CURSOR_POSITION 21

/* host->guest commands */
#define VBVAHG_EVENT              1
#define VBVAHG_DISPLAY_CUSTOM     2
#ifdef VBOX_WITH_VDMA
#define VBVAHG_SHGSMI_COMPLETION  3
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
#define VBVAHG_DCUSTOM_VHWA_CMDCOMPLETE 1
#pragma pack(1)
typedef struct VBVAHOSTCMDVHWACMDCOMPLETE {
	u32 offCmd;
}VBVAHOSTCMDVHWACMDCOMPLETE;
#pragma pack()
#endif /* # ifdef VBOX_WITH_VIDEOHWACCEL */

#pragma pack(1)
typedef enum {
	VBVAHOSTCMD_OP_EVENT = 1,
	VBVAHOSTCMD_OP_CUSTOM
}VBVAHOSTCMD_OP_TYPE;

typedef struct VBVAHOSTCMDEVENT {
	uint64_t pEvent;
}VBVAHOSTCMDEVENT;


typedef struct VBVAHOSTCMD {
	/* destination ID if >=0 specifies display index, otherwize the command is directed to the miniport */
	s32 iDstID;
	s32 customOpCode;
	union {
		struct VBVAHOSTCMD *pNext;
		u32             offNext;
		uint64_t Data; /* the body is 64-bit aligned */
	} u;
	char body[1];
}VBVAHOSTCMD;

#define VBVAHOSTCMD_SIZE(_size) (sizeof(VBVAHOSTCMD) + (_size))
#define VBVAHOSTCMD_BODY(_pCmd, _tBody) ((_tBody*)(_pCmd)->body)
#define VBVAHOSTCMD_HDR(_pBody) ((VBVAHOSTCMD*)(((u8*)_pBody) - RT_OFFSETOF(VBVAHOSTCMD, body)))
#define VBVAHOSTCMD_HDRSIZE (RT_OFFSETOF(VBVAHOSTCMD, body))

#pragma pack()

/* struct vbva_conf32::index */
#define VBOX_VBVA_CONF32_MONITOR_COUNT  0
#define VBOX_VBVA_CONF32_HOST_HEAP_SIZE 1
/** Returns VINF_SUCCESS if the host can report mode hints via VBVA.
 * Set value to VERR_NOT_SUPPORTED before calling. */
#define VBOX_VBVA_CONF32_MODE_HINT_REPORTING  2
/** Returns VINF_SUCCESS if the host can report guest cursor enabled status via
 * VBVA.  Set value to VERR_NOT_SUPPORTED before calling. */
#define VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING  3
/** Returns the currently available host cursor capabilities.  Available if
 * struct vbva_conf32::VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING returns success.
 * @see VMMDevReqMouseStatus::mouseFeatures. */
#define VBOX_VBVA_CONF32_CURSOR_CAPABILITIES  4
/** Returns the supported flags in VBVAINFOSCREEN::u8Flags. */
#define VBOX_VBVA_CONF32_SCREEN_FLAGS 5
/** Returns the max size of VBVA record. */
#define VBOX_VBVA_CONF32_MAX_RECORD_SIZE 6

typedef struct vbva_conf32 {
	u32 index;
	u32 value;
} vbva_conf32;

/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED0  BIT(0)
/** Guest cursor capability: can the host show a hardware cursor at the host
 * pointer location? */
#define VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE   BIT(1)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED2  BIT(2)
/** Reserved for historical reasons.  Must always be unset. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED3  BIT(3)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED4  BIT(4)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED5  BIT(5)

typedef struct VBVAINFOVIEW {
	/* Index of the screen, assigned by the guest. */
	u32 view_index;

	/* The screen offset in VRAM, the framebuffer starts here. */
	u32 u32ViewOffset;

	/* The size of the VRAM memory that can be used for the view. */
	u32 u32ViewSize;

	/* The recommended maximum size of the VRAM memory for the screen. */
	u32 u32MaxScreenSize;
} VBVAINFOVIEW;

typedef struct VBVAINFOHEAP {
	/* Absolute offset in VRAM of the start of the heap. */
	u32 u32HeapOffset;

	/* The size of the heap. */
	u32 u32HeapSize;

} VBVAINFOHEAP;

typedef struct VBVAFLUSH {
	u32 reserved;

} VBVAFLUSH;

typedef struct VBVACMDVBVASUBMIT {
	u32 reserved;
} VBVACMDVBVASUBMIT;

/* flush is requested because due to guest command buffer overflow */
#define VBVACMDVBVAFLUSH_F_GUEST_BUFFER_OVERFLOW 1

typedef struct VBVACMDVBVAFLUSH {
	u32 flags;
} VBVACMDVBVAFLUSH;


/* VBVAINFOSCREEN::u8Flags */
#define VBVA_SCREEN_F_NONE     0x0000
#define VBVA_SCREEN_F_ACTIVE   0x0001
/** The virtual monitor has been disabled by the guest and should be removed
 * by the host and ignored for purposes of pointer position calculation. */
#define VBVA_SCREEN_F_DISABLED 0x0002
/** The virtual monitor has been blanked by the guest and should be blacked
 * out by the host using width, height, etc values from the VBVAINFOSCREEN request. */
#define VBVA_SCREEN_F_BLANK    0x0004
/** The virtual monitor has been blanked by the guest and should be blacked
 * out by the host using the previous mode values for width. height, etc. */
#define VBVA_SCREEN_F_BLANK2   0x0008

typedef struct VBVAINFOSCREEN {
	/* Which view contains the screen. */
	u32 view_index;

	/* Physical X origin relative to the primary screen. */
	s32 origin_x;

	/* Physical Y origin relative to the primary screen. */
	s32 origin_y;

	/* Offset of visible framebuffer relative to the framebuffer start. */
	u32 start_offset;

	/* The scan line size in bytes. */
	u32 line_size;

	/* Width of the screen. */
	u32 width;

	/* Height of the screen. */
	u32 height;

	/* Color depth. */
	u16 bits_per_pixel;

	/* VBVA_SCREEN_F_* */
	u16 flags;
} VBVAINFOSCREEN;


/* VBVAENABLE::flags */
#define VBVA_F_NONE    0x00000000
#define VBVA_F_ENABLE  0x00000001
#define VBVA_F_DISABLE 0x00000002
/* extended VBVA to be used with WDDM */
#define VBVA_F_EXTENDED 0x00000004
/* vbva offset is absolute VRAM offset */
#define VBVA_F_ABSOFFSET 0x00000008

typedef struct VBVAENABLE {
	u32 flags;
	u32 offset;
	s32  result;
} VBVAENABLE;

typedef struct vbva_enable_ex {
	VBVAENABLE base;
	u32 screen_id;
} vbva_enable_ex;


typedef struct vbva_mouse_pointer_shape {
	/* The host result. */
	s32 result;

	/* VBOX_MOUSE_POINTER_* bit flags. */
	u32 flags;

	/* X coordinate of the hot spot. */
	u32 hot_x;

	/* Y coordinate of the hot spot. */
	u32 hot_y;

	/* Width of the pointer in pixels. */
	u32 width;

	/* Height of the pointer in scanlines. */
	u32 height;

	/* Pointer data.
	 *
	 ****
	 * The data consists of 1 bpp AND mask followed by 32 bpp XOR (color) mask.
	 *
	 * For pointers without alpha channel the XOR mask pixels are 32 bit values: (lsb)BGR0(msb).
	 * For pointers with alpha channel the XOR mask consists of (lsb)BGRA(msb) 32 bit values.
	 *
	 * Guest driver must create the AND mask for pointers with alpha channel, so if host does not
	 * support alpha, the pointer could be displayed as a normal color pointer. The AND mask can
	 * be constructed from alpha values. For example alpha value >= 0xf0 means bit 0 in the AND mask.
	 *
	 * The AND mask is 1 bpp bitmap with byte aligned scanlines. Size of AND mask,
	 * therefore, is cbAnd = (width + 7) / 8 * height. The padding bits at the
	 * end of any scanline are undefined.
	 *
	 * The XOR mask follows the AND mask on the next 4 bytes aligned offset:
	 * u8 *pXor = pAnd + (cbAnd + 3) & ~3
	 * Bytes in the gap between the AND and the XOR mask are undefined.
	 * XOR mask scanlines have no gap between them and size of XOR mask is:
	 * cXor = width * 4 * height.
	 ****
	 *
	 * Preallocate 4 bytes for accessing actual data as p->data.
	 */
	u8 data[4];

} vbva_mouse_pointer_shape;

/** @name struct vbva_mouse_pointer_shape::flags
 * @note The VBOX_MOUSE_POINTER_* flags are used in the guest video driver,
 *       values must be <= 0x8000 and must not be changed. (try make more sense
 *       of this, please).
 * @{
 */
/** pointer is visible */
#define VBOX_MOUSE_POINTER_VISIBLE (0x0001)
/** pointer has alpha channel */
#define VBOX_MOUSE_POINTER_ALPHA   (0x0002)
/** pointerData contains new pointer shape */
#define VBOX_MOUSE_POINTER_SHAPE   (0x0004)
/** @} */

/* the guest driver can handle asynch guest cmd completion by reading the command offset from io port */
#define VBVACAPS_COMPLETEGCMD_BY_IOREAD 0x00000001
/* the guest driver can handle video adapter IRQs */
#define VBVACAPS_IRQ                    0x00000002
/** The guest can read video mode hints sent via VBVA. */
#define VBVACAPS_VIDEO_MODE_HINTS       0x00000004
/** The guest can switch to a software cursor on demand. */
#define VBVACAPS_DISABLE_CURSOR_INTEGRATION 0x00000008
/** The guest does not depend on host handling the VBE registers. */
#define VBVACAPS_USE_VBVA_ONLY 0x00000010
typedef struct vbva_caps {
	s32 rc;
	u32 caps;
} vbva_caps;

/* makes graphics device generate IRQ on VSYNC */
#define VBVASCANLINECFG_ENABLE_VSYNC_IRQ        0x00000001
/* guest driver may request the current scanline */
#define VBVASCANLINECFG_ENABLE_SCANLINE_INFO    0x00000002
/* request the current refresh period, returned in u32RefreshPeriodMs */
#define VBVASCANLINECFG_QUERY_REFRESH_PERIOD    0x00000004
/* set new refresh period specified in u32RefreshPeriodMs.
 * if used with VBVASCANLINECFG_QUERY_REFRESH_PERIOD,
 * u32RefreshPeriodMs is set to the previous refresh period on return */
#define VBVASCANLINECFG_SET_REFRESH_PERIOD      0x00000008

typedef struct VBVASCANLINECFG {
	s32 rc;
	u32 flags;
	u32 u32RefreshPeriodMs;
	u32 reserved;
} VBVASCANLINECFG;

typedef struct VBVASCANLINEINFO {
	s32 rc;
	u32 screen_id;
	u32 u32InVBlank;
	u32 u32ScanLine;
} VBVASCANLINEINFO;

/** Query the most recent mode hints received from the host. */
typedef struct vbva_query_mode_hints {
	/** The maximum number of screens to return hints for. */
	u16 hints_queried_count;
	/** The size of the mode hint structures directly following this one. */
	u16 cbHintStructureGuest;
	/** The return code for the operation.  Initialise to VERR_NOT_SUPPORTED. */
	s32  rc;
} vbva_query_mode_hints;

/** Structure in which a mode hint is returned.  The guest allocates an array
 *  of these immediately after the struct vbva_query_mode_hints structure.  To accomodate
 *  future extensions, the struct vbva_query_mode_hints structure specifies the size of
 *  the struct vbva_modehint structures allocated by the guest, and the host only fills
 *  out structure elements which fit into that size.  The host should fill any
 *  unused members (e.g. dx, dy) or structure space on the end with ~0.  The
 *  whole structure can legally be set to ~0 to skip a screen. */
typedef struct vbva_modehint {
	u32 magic;
	u32 cx;
	u32 cy;
	u32 bpp;  /* Which has never been used... */
	u32 display;
	u32 dx;  /**< X offset into the virtual frame-buffer. */
	u32 dy;  /**< Y offset into the virtual frame-buffer. */
	u32 fEnabled;  /* Not flags.  Add new members for new flags. */
} vbva_modehint;

#define VBVAMODEHINT_MAGIC 0x0801add9u

/** Report the rectangle relative to which absolute pointer events should be
 *  expressed.  This information remains valid until the next VBVA resize event
 *  for any screen, at which time it is reset to the bounding rectangle of all
 *  virtual screens and must be re-set.
 *  @see VBVA_REPORT_INPUT_MAPPING. */
typedef struct vbva_report_input_mapping {
	s32 x;    /**< Upper left X co-ordinate relative to the first screen. */
	s32 y;    /**< Upper left Y co-ordinate relative to the first screen. */
	u32 cx;  /**< Rectangle width. */
	u32 cy;  /**< Rectangle height. */
} vbva_report_input_mapping;

/** Report the guest cursor position and query the host one.  The host may wish
 *  to use the guest information to re-position its own cursor (though this is
 *  currently unlikely).
 *  @see VBVA_CURSOR_POSITION */
typedef struct vbva_cursor_position {
	u32 report_position;  /**< Are we reporting a position? */
	u32 x;                /**< Guest cursor X position */
	u32 y;                /**< Guest cursor Y position */
} vbva_cursor_position;

#pragma pack()

typedef uint64_t VBOXVIDEOOFFSET;

#define VBOXVIDEOOFFSET_VOID ((VBOXVIDEOOFFSET)~0)

#pragma pack(1)

/*
 * VBOXSHGSMI made on top HGSMI and allows receiving notifications
 * about G->H command completion
 */
/* SHGSMI command header */
typedef struct VBOXSHGSMIHEADER {
	uint64_t pvNext;    /*<- completion processing queue */
	u32 flags;    /*<- see VBOXSHGSMI_FLAG_XXX Flags */
	u32 cRefs;     /*<- command referece count */
	uint64_t u64Info1;  /*<- contents depends on the flags value */
	uint64_t u64Info2;  /*<- contents depends on the flags value */
} VBOXSHGSMIHEADER, *PVBOXSHGSMIHEADER;

typedef enum {
	VBOXVDMACMD_TYPE_UNDEFINED         = 0,
	VBOXVDMACMD_TYPE_DMA_PRESENT_BLT   = 1,
	VBOXVDMACMD_TYPE_DMA_BPB_TRANSFER,
	VBOXVDMACMD_TYPE_DMA_BPB_FILL,
	VBOXVDMACMD_TYPE_DMA_PRESENT_SHADOW2PRIMARY,
	VBOXVDMACMD_TYPE_DMA_PRESENT_CLRFILL,
	VBOXVDMACMD_TYPE_DMA_PRESENT_FLIP,
	VBOXVDMACMD_TYPE_DMA_NOP,
	VBOXVDMACMD_TYPE_CHROMIUM_CMD, /* chromium cmd */
	VBOXVDMACMD_TYPE_DMA_BPB_TRANSFER_VRAMSYS,
	VBOXVDMACMD_TYPE_CHILD_STATUS_IRQ /* make the device notify child (monitor) state change IRQ */
} VBOXVDMACMD_TYPE;

#pragma pack()

/* the command processing was asynch, set by the host to indicate asynch command completion
 * must not be cleared once set, the command completion is performed by issuing a host->guest completion command
 * while keeping this flag unchanged */
#define VBOXSHGSMI_FLAG_HG_ASYNCH               0x00010000
#if 0
/* if set     - asynch completion is performed by issuing the event,
 * if cleared - asynch completion is performed by calling a callback */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_EVENT         0x00000001
#endif
/* issue interrupt on asynch completion, used for critical G->H commands,
 * i.e. for completion of which guest is waiting. */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ           0x00000002
/* guest does not do any op on completion of this command,
 * the host may copy the command and indicate that it does not need the command anymore
 * by not setting VBOXSHGSMI_FLAG_HG_ASYNCH */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_NOCOMPLETION  0x00000004
/* guest requires the command to be processed asynchronously,
 * not setting VBOXSHGSMI_FLAG_HG_ASYNCH by the host in this case is treated as command failure */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_FORCE         0x00000008
/* force IRQ on cmd completion */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ_FORCE     0x00000010
/* an IRQ-level callback is associated with the command */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ  0x00000020
/* guest expects this command to be completed synchronously */
#define VBOXSHGSMI_FLAG_GH_SYNCH                0x00000040


static inline u8 * VBoxSHGSMIBufferData (const VBOXSHGSMIHEADER* pHeader)
{
	return (u8 *)pHeader + sizeof (VBOXSHGSMIHEADER);
}

#define VBoxSHGSMIBufferHeaderSize() (sizeof (VBOXSHGSMIHEADER))

static inline PVBOXSHGSMIHEADER VBoxSHGSMIBufferHeader (const void *pvData)
{
	return (PVBOXSHGSMIHEADER)((u8 *)pvData - sizeof (VBOXSHGSMIHEADER));
}

#ifdef VBOX_WITH_VDMA
# pragma pack(1)

/* VDMA - Video DMA */

/* VDMA Control API */
/* VBOXVDMA_CTL::flags */
typedef enum {
	VBOXVDMA_CTL_TYPE_NONE = 0,
	VBOXVDMA_CTL_TYPE_ENABLE,
	VBOXVDMA_CTL_TYPE_DISABLE,
	VBOXVDMA_CTL_TYPE_FLUSH,
	VBOXVDMA_CTL_TYPE_WATCHDOG
} VBOXVDMA_CTL_TYPE;

typedef struct VBOXVDMA_CTL {
	VBOXVDMA_CTL_TYPE enmCtl;
	u32 offset;
	s32  result;
} VBOXVDMA_CTL, *PVBOXVDMA_CTL;

typedef struct VBOXVDMA_RECTL {
	int16_t left;
	int16_t top;
	u16 width;
	u16 height;
} VBOXVDMA_RECTL, *PVBOXVDMA_RECTL;

typedef enum {
	VBOXVDMA_PIXEL_FORMAT_UNKNOWN      =  0,
	VBOXVDMA_PIXEL_FORMAT_R8G8B8       = 20,
	VBOXVDMA_PIXEL_FORMAT_A8R8G8B8     = 21,
	VBOXVDMA_PIXEL_FORMAT_X8R8G8B8     = 22,
	VBOXVDMA_PIXEL_FORMAT_R5G6B5       = 23,
	VBOXVDMA_PIXEL_FORMAT_X1R5G5B5     = 24,
	VBOXVDMA_PIXEL_FORMAT_A1R5G5B5     = 25,
	VBOXVDMA_PIXEL_FORMAT_A4R4G4B4     = 26,
	VBOXVDMA_PIXEL_FORMAT_R3G3B2       = 27,
	VBOXVDMA_PIXEL_FORMAT_A8           = 28,
	VBOXVDMA_PIXEL_FORMAT_A8R3G3B2     = 29,
	VBOXVDMA_PIXEL_FORMAT_X4R4G4B4     = 30,
	VBOXVDMA_PIXEL_FORMAT_A2B10G10R10  = 31,
	VBOXVDMA_PIXEL_FORMAT_A8B8G8R8     = 32,
	VBOXVDMA_PIXEL_FORMAT_X8B8G8R8     = 33,
	VBOXVDMA_PIXEL_FORMAT_G16R16       = 34,
	VBOXVDMA_PIXEL_FORMAT_A2R10G10B10  = 35,
	VBOXVDMA_PIXEL_FORMAT_A16B16G16R16 = 36,
	VBOXVDMA_PIXEL_FORMAT_A8P8         = 40,
	VBOXVDMA_PIXEL_FORMAT_P8           = 41,
	VBOXVDMA_PIXEL_FORMAT_L8           = 50,
	VBOXVDMA_PIXEL_FORMAT_A8L8         = 51,
	VBOXVDMA_PIXEL_FORMAT_A4L4         = 52,
	VBOXVDMA_PIXEL_FORMAT_V8U8         = 60,
	VBOXVDMA_PIXEL_FORMAT_L6V5U5       = 61,
	VBOXVDMA_PIXEL_FORMAT_X8L8V8U8     = 62,
	VBOXVDMA_PIXEL_FORMAT_Q8W8V8U8     = 63,
	VBOXVDMA_PIXEL_FORMAT_V16U16       = 64,
	VBOXVDMA_PIXEL_FORMAT_W11V11U10    = 65,
	VBOXVDMA_PIXEL_FORMAT_A2W10V10U10  = 67
} VBOXVDMA_PIXEL_FORMAT;

typedef struct VBOXVDMA_SURF_DESC {
	u32 width;
	u32 height;
	VBOXVDMA_PIXEL_FORMAT format;
	u32 bpp;
	u32 pitch;
	u32 flags;
} VBOXVDMA_SURF_DESC, *PVBOXVDMA_SURF_DESC;

/*typedef uint64_t VBOXVDMAPHADDRESS;*/
typedef uint64_t VBOXVDMASURFHANDLE;

/* region specified as a rectangle, otherwize it is a size of memory pointed to by phys address */
#define VBOXVDMAOPERAND_FLAGS_RECTL       0x1
/* Surface handle is valid */
#define VBOXVDMAOPERAND_FLAGS_PRIMARY        0x2
/* address is offset in VRAM */
#define VBOXVDMAOPERAND_FLAGS_VRAMOFFSET  0x4


/* VBOXVDMACBUF_DR::phBuf specifies offset in VRAM */
#define VBOXVDMACBUF_FLAG_BUF_VRAM_OFFSET 0x00000001
/* command buffer follows the VBOXVDMACBUF_DR in VRAM, VBOXVDMACBUF_DR::phBuf is ignored */
#define VBOXVDMACBUF_FLAG_BUF_FOLLOWS_DR  0x00000002

/*
 * We can not submit the DMA command via VRAM since we do not have control over
 * DMA command buffer [de]allocation, i.e. we only control the buffer contents.
 * In other words the system may call one of our callbacks to fill a command buffer
 * with the necessary commands and then discard the buffer w/o any notification.
 *
 * We have only DMA command buffer physical address at submission time.
 *
 * so the only way is to */
typedef struct VBOXVDMACBUF_DR {
	u16 flags;
	u16 cbBuf;
	/* RT_SUCCESS()     - on success
	 * VERR_INTERRUPTED - on preemption
	 * VERR_xxx         - on error */
	s32  rc;
	union {
		uint64_t phBuf;
		VBOXVIDEOOFFSET offVramBuf;
	} Location;
	uint64_t aGuestData[7];
} VBOXVDMACBUF_DR, *PVBOXVDMACBUF_DR;

#define VBOXVDMACBUF_DR_TAIL(_pCmd, _t) ( (_t*)(((u8*)(_pCmd)) + sizeof (VBOXVDMACBUF_DR)) )
#define VBOXVDMACBUF_DR_FROM_TAIL(_pCmd) ( (VBOXVDMACBUF_DR*)(((u8*)(_pCmd)) - sizeof (VBOXVDMACBUF_DR)) )

typedef struct VBOXVDMACMD {
	VBOXVDMACMD_TYPE enmType;
	u32 u32CmdSpecific;
} VBOXVDMACMD, *PVBOXVDMACMD;

#define VBOXVDMACMD_HEADER_SIZE() sizeof (VBOXVDMACMD)
#define VBOXVDMACMD_SIZE_FROMBODYSIZE(_s) (VBOXVDMACMD_HEADER_SIZE() + (_s))
#define VBOXVDMACMD_SIZE(_t) (VBOXVDMACMD_SIZE_FROMBODYSIZE(sizeof (_t)))
#define VBOXVDMACMD_BODY(_pCmd, _t) ( (_t*)(((u8*)(_pCmd)) + VBOXVDMACMD_HEADER_SIZE()) )
#define VBOXVDMACMD_BODY_SIZE(_s) ( (_s) - VBOXVDMACMD_HEADER_SIZE() )
#define VBOXVDMACMD_FROM_BODY(_pCmd) ( (VBOXVDMACMD*)(((u8*)(_pCmd)) - VBOXVDMACMD_HEADER_SIZE()) )
#define VBOXVDMACMD_BODY_FIELD_OFFSET(_ot, _t, _f) ( (_ot)(uintptr_t)( VBOXVDMACMD_BODY(0, u8) + RT_OFFSETOF(_t, _f) ) )

typedef struct VBOXVDMACMD_DMA_PRESENT_BLT {
	VBOXVIDEOOFFSET offSrc;
	VBOXVIDEOOFFSET offDst;
	VBOXVDMA_SURF_DESC srcDesc;
	VBOXVDMA_SURF_DESC dstDesc;
	VBOXVDMA_RECTL srcRectl;
	VBOXVDMA_RECTL dstRectl;
	u32 reserved;
	u32 cDstSubRects;
	VBOXVDMA_RECTL aDstSubRects[1];
} VBOXVDMACMD_DMA_PRESENT_BLT, *PVBOXVDMACMD_DMA_PRESENT_BLT;

typedef struct VBOXVDMACMD_DMA_PRESENT_SHADOW2PRIMARY {
	VBOXVDMA_RECTL Rect;
} VBOXVDMACMD_DMA_PRESENT_SHADOW2PRIMARY, *PVBOXVDMACMD_DMA_PRESENT_SHADOW2PRIMARY;


#define VBOXVDMACMD_DMA_BPB_TRANSFER_F_SRC_VRAMOFFSET 0x00000001
#define VBOXVDMACMD_DMA_BPB_TRANSFER_F_DST_VRAMOFFSET 0x00000002

typedef struct VBOXVDMACMD_DMA_BPB_TRANSFER {
	u32 cbTransferSize;
	u32 flags;
	union {
		uint64_t phBuf;
		VBOXVIDEOOFFSET offVramBuf;
	} Src;
	union {
		uint64_t phBuf;
		VBOXVIDEOOFFSET offVramBuf;
	} Dst;
} VBOXVDMACMD_DMA_BPB_TRANSFER, *PVBOXVDMACMD_DMA_BPB_TRANSFER;

#define VBOXVDMACMD_SYSMEMEL_F_PAGELIST 0x00000001

typedef struct VBOXVDMACMD_SYSMEMEL {
	u32 cPages;
	u32 flags;
	uint64_t phBuf[1];
} VBOXVDMACMD_SYSMEMEL, *PVBOXVDMACMD_SYSMEMEL;

#define VBOXVDMACMD_SYSMEMEL_NEXT(_pEl) (((_pEl)->flags & VBOXVDMACMD_SYSMEMEL_F_PAGELIST) ? \
		((PVBOXVDMACMD_SYSMEMEL)(((u8*)(_pEl))+RT_OFFSETOF(VBOXVDMACMD_SYSMEMEL, phBuf[(_pEl)->cPages]))) \
		: \
		((_pEl)+1)

#define VBOXVDMACMD_DMA_BPB_TRANSFER_VRAMSYS_SYS2VRAM 0x00000001

typedef struct VBOXVDMACMD_DMA_BPB_TRANSFER_VRAMSYS {
	u32 cTransferPages;
	u32 flags;
	VBOXVIDEOOFFSET offVramBuf;
	VBOXVDMACMD_SYSMEMEL FirstEl;
} VBOXVDMACMD_DMA_BPB_TRANSFER_VRAMSYS, *PVBOXVDMACMD_DMA_BPB_TRANSFER_VRAMSYS;

typedef struct VBOXVDMACMD_DMA_BPB_FILL {
	VBOXVIDEOOFFSET offSurf;
	u32 cbFillSize;
	u32 u32FillPattern;
} VBOXVDMACMD_DMA_BPB_FILL, *PVBOXVDMACMD_DMA_BPB_FILL;

#define VBOXVDMA_CHILD_STATUS_F_CONNECTED    0x01
#define VBOXVDMA_CHILD_STATUS_F_DISCONNECTED 0x02
#define VBOXVDMA_CHILD_STATUS_F_ROTATED      0x04

typedef struct VBOXVDMA_CHILD_STATUS {
	u32 iChild;
	u8  flags;
	u8  u8RotationAngle;
	u16 u16Reserved;
} VBOXVDMA_CHILD_STATUS, *PVBOXVDMA_CHILD_STATUS;

/* apply the aInfos are applied to all targets, the iTarget is ignored */
#define VBOXVDMACMD_CHILD_STATUS_IRQ_F_APPLY_TO_ALL 0x00000001

typedef struct VBOXVDMACMD_CHILD_STATUS_IRQ {
	u32 cInfos;
	u32 flags;
	VBOXVDMA_CHILD_STATUS aInfos[1];
} VBOXVDMACMD_CHILD_STATUS_IRQ, *PVBOXVDMACMD_CHILD_STATUS_IRQ;

# pragma pack()
#endif /* #ifdef VBOX_WITH_VDMA */

#pragma pack(1)
typedef struct VBOXVDMACMD_CHROMIUM_BUFFER {
	VBOXVIDEOOFFSET offBuffer;
	u32 buffer_length;
	u32 u32GuestData;
	uint64_t u64GuestData;
} VBOXVDMACMD_CHROMIUM_BUFFER, *PVBOXVDMACMD_CHROMIUM_BUFFER;

typedef struct VBOXVDMACMD_CHROMIUM_CMD {
	u32 cBuffers;
	u32 reserved;
	VBOXVDMACMD_CHROMIUM_BUFFER aBuffers[1];
} VBOXVDMACMD_CHROMIUM_CMD, *PVBOXVDMACMD_CHROMIUM_CMD;

typedef enum {
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_UNKNOWN = 0,
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_CRHGSMI_SETUP,
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_SAVESTATE_BEGIN,
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_SAVESTATE_END,
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_CRHGSMI_SETUP_MAINCB,
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_CRCONNECT,
	VBOXVDMACMD_CHROMIUM_CTL_TYPE_SIZEHACK = 0x7fffffff
} VBOXVDMACMD_CHROMIUM_CTL_TYPE;

typedef struct VBOXVDMACMD_CHROMIUM_CTL {
	VBOXVDMACMD_CHROMIUM_CTL_TYPE enmType;
	u32 cbCmd;
} VBOXVDMACMD_CHROMIUM_CTL, *PVBOXVDMACMD_CHROMIUM_CTL;


typedef struct PDMIDISPLAYVBVACALLBACKS *HCRHGSMICMDCOMPLETION;
typedef int FNCRHGSMICMDCOMPLETION(HCRHGSMICMDCOMPLETION hCompletion, PVBOXVDMACMD_CHROMIUM_CMD pCmd, int rc);
typedef FNCRHGSMICMDCOMPLETION *PFNCRHGSMICMDCOMPLETION;

/* tells whether 3D backend has some 3D overlay data displayed */
typedef bool FNCROGLHASDATA(void);
typedef FNCROGLHASDATA *PFNCROGLHASDATA;

/* same as PFNCROGLHASDATA, but for specific screen */
typedef bool FNCROGLHASDATAFORSCREEN(u32 i32ScreenID);
typedef FNCROGLHASDATAFORSCREEN *PFNCROGLHASDATAFORSCREEN;

/* callbacks chrogl gives to main */
typedef struct CR_MAIN_INTERFACE {
	PFNCROGLHASDATA pfnHasData;
	PFNCROGLHASDATAFORSCREEN pfnHasDataForScreen;
} CR_MAIN_INTERFACE;

typedef struct VBOXVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP_MAINCB {
	VBOXVDMACMD_CHROMIUM_CTL Hdr;
	/*in*/
	HCRHGSMICMDCOMPLETION hCompletion;
	PFNCRHGSMICMDCOMPLETION pfnCompletion;
	/*out*/
	CR_MAIN_INTERFACE MainInterface;
} VBOXVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP_MAINCB, *PVBOXVDMACMD_CHROMIUM_CTL_CRHGSMI_SETUP_MAINCB;

typedef struct VBOXCRCON_SERVER *HVBOXCRCON_SERVER;
typedef struct PDMIDISPLAYVBVACALLBACKS* HVBOXCRCON_CLIENT;

typedef struct VBOXCRCON_3DRGN_CLIENT* HVBOXCRCON_3DRGN_CLIENT;
typedef struct VBOXCRCON_3DRGN_ASYNCCLIENT* HVBOXCRCON_3DRGN_ASYNCCLIENT;

/* server callbacks */
/* submit chromium cmd */
typedef int FNVBOXCRCON_SVR_CRCMD(HVBOXCRCON_SERVER hServer, PVBOXVDMACMD_CHROMIUM_CMD pCmd, u32 cbCmd);
typedef FNVBOXCRCON_SVR_CRCMD *PFNVBOXCRCON_SVR_CRCMD;

/* submit chromium control cmd */
typedef int FNVBOXCRCON_SVR_CRCTL(HVBOXCRCON_SERVER hServer, PVBOXVDMACMD_CHROMIUM_CTL pCtl, u32 cbCmd);
typedef FNVBOXCRCON_SVR_CRCTL *PFNVBOXCRCON_SVR_CRCTL;

/* request 3D data.
 * The protocol is the following:
 * 1. if there is no 3D data displayed on screen, returns VINF_EOF immediately w/o calling any PFNVBOXCRCON_3DRGN_XXX callbacks
 * 2. otherwise calls PFNVBOXCRCON_3DRGN_ONSUBMIT, submits the "regions get" request to the CrOpenGL server to process it asynchronously and returns VINF_SUCCESS
 * 2.a on "regions get" request processing calls PFNVBOXCRCON_3DRGN_BEGIN,
 * 2.b then PFNVBOXCRCON_3DRGN_REPORT zero or more times for each 3D region,
 * 2.c and then PFNVBOXCRCON_3DRGN_END
 * 3. returns VERR_XXX code on failure
 * */
typedef int FNVBOXCRCON_SVR_3DRGN_GET(HVBOXCRCON_SERVER hServer, HVBOXCRCON_3DRGN_CLIENT hRgnClient, u32 idScreen);
typedef FNVBOXCRCON_SVR_3DRGN_GET *PFNVBOXCRCON_SVR_3DRGN_GET;

/* 3D Regions Client callbacks */
/* called from the PFNVBOXCRCON_SVR_3DRGN_GET callback in case server has 3D data and is going to process the request asynchronously,
 * see comments for PFNVBOXCRCON_SVR_3DRGN_GET above */
typedef int FNVBOXCRCON_3DRGN_ONSUBMIT(HVBOXCRCON_3DRGN_CLIENT hRgnClient, u32 idScreen, HVBOXCRCON_3DRGN_ASYNCCLIENT *phRgnAsyncClient);
typedef FNVBOXCRCON_3DRGN_ONSUBMIT *PFNVBOXCRCON_3DRGN_ONSUBMIT;

/* called from the "regions get" command processing thread, to indicate that the "regions get" is started.
 * see comments for PFNVBOXCRCON_SVR_3DRGN_GET above */
typedef int FNVBOXCRCON_3DRGN_BEGIN(HVBOXCRCON_3DRGN_ASYNCCLIENT hRgnAsyncClient, u32 idScreen);
typedef FNVBOXCRCON_3DRGN_BEGIN *PFNVBOXCRCON_3DRGN_BEGIN;

/* called from the "regions get" command processing thread, to report a 3D region.
 * see comments for PFNVBOXCRCON_SVR_3DRGN_GET above */
typedef int FNVBOXCRCON_3DRGN_REPORT(HVBOXCRCON_3DRGN_ASYNCCLIENT hRgnAsyncClient, u32 idScreen, void *pvData, u32 cbStride, const void *pRect);
typedef FNVBOXCRCON_3DRGN_REPORT *PFNVBOXCRCON_3DRGN_REPORT;

/* called from the "regions get" command processing thread, to indicate that the "regions get" is completed.
 * see comments for PFNVBOXCRCON_SVR_3DRGN_GET above */
typedef int FNVBOXCRCON_3DRGN_END(HVBOXCRCON_3DRGN_ASYNCCLIENT hRgnAsyncClient, u32 idScreen);
typedef FNVBOXCRCON_3DRGN_END *PFNVBOXCRCON_3DRGN_END;


/* client callbacks */
/* complete chromium cmd */
typedef int FNVBOXCRCON_CLT_CRCTL_COMPLETE(HVBOXCRCON_CLIENT hClient, PVBOXVDMACMD_CHROMIUM_CTL pCtl, int rc);
typedef FNVBOXCRCON_CLT_CRCTL_COMPLETE *PFNVBOXCRCON_CLT_CRCTL_COMPLETE;

/* complete chromium control cmd */
typedef int FNVBOXCRCON_CLT_CRCMD_COMPLETE(HVBOXCRCON_CLIENT hClient, PVBOXVDMACMD_CHROMIUM_CMD pCmd, int rc);
typedef FNVBOXCRCON_CLT_CRCMD_COMPLETE *PFNVBOXCRCON_CLT_CRCMD_COMPLETE;

typedef struct VBOXCRCON_SERVER_CALLBACKS {
	HVBOXCRCON_SERVER hServer;
	PFNVBOXCRCON_SVR_CRCMD pfnCrCmd;
	PFNVBOXCRCON_SVR_CRCTL pfnCrCtl;
	PFNVBOXCRCON_SVR_3DRGN_GET pfn3DRgnGet;
} VBOXCRCON_SERVER_CALLBACKS, *PVBOXCRCON_SERVER_CALLBACKS;

typedef struct VBOXCRCON_CLIENT_CALLBACKS {
	HVBOXCRCON_CLIENT hClient;
	PFNVBOXCRCON_CLT_CRCMD_COMPLETE pfnCrCmdComplete;
	PFNVBOXCRCON_CLT_CRCTL_COMPLETE pfnCrCtlComplete;
	PFNVBOXCRCON_3DRGN_ONSUBMIT pfn3DRgnOnSubmit;
	PFNVBOXCRCON_3DRGN_BEGIN pfn3DRgnBegin;
	PFNVBOXCRCON_3DRGN_REPORT pfn3DRgnReport;
	PFNVBOXCRCON_3DRGN_END pfn3DRgnEnd;
} VBOXCRCON_CLIENT_CALLBACKS, *PVBOXCRCON_CLIENT_CALLBACKS;

/* issued by Main to establish connection between Main and CrOpenGL service */
typedef struct VBOXVDMACMD_CHROMIUM_CTL_CRCONNECT {
	VBOXVDMACMD_CHROMIUM_CTL Hdr;
	/*input (filled by Client) :*/
	/*class VMMDev*/void *pVMMDev;
	VBOXCRCON_CLIENT_CALLBACKS ClientCallbacks;
	/*output (filled by Server) :*/
	VBOXCRCON_SERVER_CALLBACKS ServerCallbacks;
} VBOXVDMACMD_CHROMIUM_CTL_CRCONNECT, *PVBOXVDMACMD_CHROMIUM_CTL_CRCONNECT;

/* ring command buffer dr */
#define VBOXCMDVBVA_STATE_SUBMITTED   1
#define VBOXCMDVBVA_STATE_CANCELLED   2
#define VBOXCMDVBVA_STATE_IN_PROGRESS 3
/* the "completed" state is signalled via the ring buffer values */

/* CrHgsmi command */
#define VBOXCMDVBVA_OPTYPE_CRCMD                        1
/* blit command that does blitting of allocations identified by VRAM offset or host id
 * for VRAM-offset ones the size and format are same as primary */
#define VBOXCMDVBVA_OPTYPE_BLT                          2
/* flip */
#define VBOXCMDVBVA_OPTYPE_FLIP                         3
/* ColorFill */
#define VBOXCMDVBVA_OPTYPE_CLRFILL                      4
/* allocation paging transfer request */
#define VBOXCMDVBVA_OPTYPE_PAGING_TRANSFER              5
/* allocation paging fill request */
#define VBOXCMDVBVA_OPTYPE_PAGING_FILL                  6
/* same as VBOXCMDVBVA_OPTYPE_NOP, but contains VBOXCMDVBVA_HDR data */
#define VBOXCMDVBVA_OPTYPE_NOPCMD                       7
/* actual command is stored in guest system memory */
#define VBOXCMDVBVA_OPTYPE_SYSMEMCMD                    8
/* complex command - i.e. can contain multiple commands
 * i.e. the VBOXCMDVBVA_OPTYPE_COMPLEXCMD VBOXCMDVBVA_HDR is followed
 * by one or more VBOXCMDVBVA_HDR commands.
 * Each command's size is specified in it's VBOXCMDVBVA_HDR's u32FenceID field */
#define VBOXCMDVBVA_OPTYPE_COMPLEXCMD                   9

/* nop - is a one-bit command. The buffer size to skip is determined by VBVA buffer size */
#define VBOXCMDVBVA_OPTYPE_NOP                          0x80

/* u8Flags flags */
/* transfer from RAM to Allocation */
#define VBOXCMDVBVA_OPF_PAGING_TRANSFER_IN                  0x80

#define VBOXCMDVBVA_OPF_BLT_TYPE_SAMEDIM_A8R8G8B8           0
#define VBOXCMDVBVA_OPF_BLT_TYPE_GENERIC_A8R8G8B8           1
#define VBOXCMDVBVA_OPF_BLT_TYPE_OFFPRIMSZFMT_OR_ID         2

#define VBOXCMDVBVA_OPF_BLT_TYPE_MASK                       3


#define VBOXCMDVBVA_OPF_CLRFILL_TYPE_GENERIC_A8R8G8B8       0

#define VBOXCMDVBVA_OPF_CLRFILL_TYPE_MASK                   1


/* blit direction is from first operand to second */
#define VBOXCMDVBVA_OPF_BLT_DIR_IN_2                        0x10
/* operand 1 contains host id */
#define VBOXCMDVBVA_OPF_OPERAND1_ISID                       0x20
/* operand 2 contains host id */
#define VBOXCMDVBVA_OPF_OPERAND2_ISID                       0x40
/* primary hint id is src */
#define VBOXCMDVBVA_OPF_PRIMARY_HINT_SRC                    0x80

/* trying to make the header as small as possible,
 * we'd have pretty few op codes actually, so 8bit is quite enough,
 * we will be able to extend it in any way. */
typedef struct VBOXCMDVBVA_HDR {
	/* one VBOXCMDVBVA_OPTYPE_XXX, except NOP, see comments above */
	u8 u8OpCode;
	/* command-specific
	 * VBOXCMDVBVA_OPTYPE_CRCMD                     - must be null
	 * VBOXCMDVBVA_OPTYPE_BLT                       - OR-ed VBOXCMDVBVA_OPF_ALLOC_XXX flags
	 * VBOXCMDVBVA_OPTYPE_PAGING_TRANSFER           - must be null
	 * VBOXCMDVBVA_OPTYPE_PAGING_FILL               - must be null
	 * VBOXCMDVBVA_OPTYPE_NOPCMD                    - must be null
	 * VBOXCMDVBVA_OPTYPE_NOP                       - not applicable (as the entire VBOXCMDVBVA_HDR is not valid) */
	u8 u8Flags;
	/* one of VBOXCMDVBVA_STATE_XXX*/
	volatile u8 u8State;
	union {
		/* result, 0 on success, otherwise contains the failure code TBD */
		int8_t i8Result;
		u8 u8PrimaryID;
	} u;
	union {
		/* complex command (VBOXCMDVBVA_OPTYPE_COMPLEXCMD) element data */
		struct {
			/* command length */
			u16 u16CbCmdHost;
			/* guest-specific data, host expects it to be NULL */
			u16 u16CbCmdGuest;
		} complexCmdEl;
		/* DXGK DDI fence ID */
		u32 u32FenceID;
	} u2;
} VBOXCMDVBVA_HDR;

typedef u32 VBOXCMDVBVAOFFSET;
typedef uint64_t VBOXCMDVBVAPHADDR;
typedef u32 VBOXCMDVBVAPAGEIDX;

typedef struct VBOXCMDVBVA_CRCMD_BUFFER {
	u32 buffer_length;
	VBOXCMDVBVAOFFSET offBuffer;
} VBOXCMDVBVA_CRCMD_BUFFER;

typedef struct VBOXCMDVBVA_CRCMD_CMD {
	u32 cBuffers;
	VBOXCMDVBVA_CRCMD_BUFFER aBuffers[1];
} VBOXCMDVBVA_CRCMD_CMD;

typedef struct VBOXCMDVBVA_CRCMD {
	VBOXCMDVBVA_HDR Hdr;
	VBOXCMDVBVA_CRCMD_CMD Cmd;
} VBOXCMDVBVA_CRCMD;

typedef struct VBOXCMDVBVA_ALLOCINFO {
	union {
		VBOXCMDVBVAOFFSET offVRAM;
		u32 id;
	} u;
} VBOXCMDVBVA_ALLOCINFO;

typedef struct VBOXCMDVBVA_ALLOCDESC {
	VBOXCMDVBVA_ALLOCINFO Info;
	u16 u16Width;
	u16 u16Height;
} VBOXCMDVBVA_ALLOCDESC;

typedef struct VBOXCMDVBVA_RECT {
   /** Coordinates of affected rectangle. */
   int16_t xLeft;
   int16_t yTop;
   int16_t xRight;
   int16_t yBottom;
} VBOXCMDVBVA_RECT;

typedef struct VBOXCMDVBVA_POINT {
   int16_t x;
   int16_t y;
} VBOXCMDVBVA_POINT;

typedef struct VBOXCMDVBVA_BLT_HDR {
	VBOXCMDVBVA_HDR Hdr;
	VBOXCMDVBVA_POINT Pos;
} VBOXCMDVBVA_BLT_HDR;

typedef struct VBOXCMDVBVA_BLT_PRIMARY {
	VBOXCMDVBVA_BLT_HDR Hdr;
	VBOXCMDVBVA_ALLOCINFO alloc;
	/* the rects count is determined from the command size */
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_BLT_PRIMARY;

typedef struct VBOXCMDVBVA_BLT_PRIMARY_GENERIC_A8R8G8B8 {
	VBOXCMDVBVA_BLT_HDR Hdr;
	VBOXCMDVBVA_ALLOCDESC alloc;
	/* the rects count is determined from the command size */
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_BLT_PRIMARY_GENERIC_A8R8G8B8;

typedef struct VBOXCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID {
	VBOXCMDVBVA_BLT_HDR Hdr;
	VBOXCMDVBVA_ALLOCINFO alloc;
	u32 id;
	/* the rects count is determined from the command size */
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_BLT_OFFPRIMSZFMT_OR_ID;

typedef struct VBOXCMDVBVA_BLT_SAMEDIM_A8R8G8B8 {
	VBOXCMDVBVA_BLT_HDR Hdr;
	VBOXCMDVBVA_ALLOCDESC alloc1;
	VBOXCMDVBVA_ALLOCINFO info2;
	/* the rects count is determined from the command size */
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_BLT_SAMEDIM_A8R8G8B8;

typedef struct VBOXCMDVBVA_BLT_GENERIC_A8R8G8B8 {
	VBOXCMDVBVA_BLT_HDR Hdr;
	VBOXCMDVBVA_ALLOCDESC alloc1;
	VBOXCMDVBVA_ALLOCDESC alloc2;
	/* the rects count is determined from the command size */
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_BLT_GENERIC_A8R8G8B8;

#define VBOXCMDVBVA_SIZEOF_BLTSTRUCT_MAX (sizeof (VBOXCMDVBVA_BLT_GENERIC_A8R8G8B8))

typedef struct VBOXCMDVBVA_FLIP {
	VBOXCMDVBVA_HDR Hdr;
	VBOXCMDVBVA_ALLOCINFO src;
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_FLIP;

#define VBOXCMDVBVA_SIZEOF_FLIPSTRUCT_MIN (RT_OFFSETOF(VBOXCMDVBVA_FLIP, aRects))

typedef struct VBOXCMDVBVA_CLRFILL_HDR {
	VBOXCMDVBVA_HDR Hdr;
	u32 u32Color;
} VBOXCMDVBVA_CLRFILL_HDR;

typedef struct VBOXCMDVBVA_CLRFILL_PRIMARY {
	VBOXCMDVBVA_CLRFILL_HDR Hdr;
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_CLRFILL_PRIMARY;

typedef struct VBOXCMDVBVA_CLRFILL_GENERIC_A8R8G8B8 {
	VBOXCMDVBVA_CLRFILL_HDR Hdr;
	VBOXCMDVBVA_ALLOCDESC dst;
	VBOXCMDVBVA_RECT aRects[1];
} VBOXCMDVBVA_CLRFILL_GENERIC_A8R8G8B8;

#define VBOXCMDVBVA_SIZEOF_CLRFILLSTRUCT_MAX (sizeof (VBOXCMDVBVA_CLRFILL_GENERIC_A8R8G8B8))

#if 0
#define VBOXCMDVBVA_SYSMEMEL_CPAGES_MAX  0x1000

typedef struct VBOXCMDVBVA_SYSMEMEL {
	u32 cPagesAfterFirst  : 12;
	u32 iPage1            : 20;
	u32 iPage2;
} VBOXCMDVBVA_SYSMEMEL;
#endif

typedef struct VBOXCMDVBVA_PAGING_TRANSFER_DATA {
	/* for now can only contain offVRAM.
	 * paging transfer can NOT be initiated for allocations having host 3D object (hostID) associated */
	VBOXCMDVBVA_ALLOCINFO Alloc;
	VBOXCMDVBVAPAGEIDX aPageNumbers[1];
} VBOXCMDVBVA_PAGING_TRANSFER_DATA;

typedef struct VBOXCMDVBVA_PAGING_TRANSFER {
	VBOXCMDVBVA_HDR Hdr;
	VBOXCMDVBVA_PAGING_TRANSFER_DATA Data;
} VBOXCMDVBVA_PAGING_TRANSFER;

typedef struct VBOXCMDVBVA_PAGING_FILL {
	VBOXCMDVBVA_HDR Hdr;
	u32 u32CbFill;
	u32 u32Pattern;
	/* paging transfer can NOT be initiated for allocations having host 3D object (hostID) associated */
	VBOXCMDVBVAOFFSET offVRAM;
} VBOXCMDVBVA_PAGING_FILL;

typedef struct VBOXCMDVBVA_SYSMEMCMD {
	VBOXCMDVBVA_HDR Hdr;
	VBOXCMDVBVAPHADDR phCmd;
} VBOXCMDVBVA_SYSMEMCMD;

#define VBOXCMDVBVACTL_TYPE_ENABLE     1
#define VBOXCMDVBVACTL_TYPE_3DCTL      2
#define VBOXCMDVBVACTL_TYPE_RESIZE     3

typedef struct VBOXCMDVBVA_CTL {
	u32 u32Type;
	s32 result;
} VBOXCMDVBVA_CTL;

typedef struct VBOXCMDVBVA_CTL_ENABLE {
	VBOXCMDVBVA_CTL Hdr;
	VBVAENABLE Enable;
} VBOXCMDVBVA_CTL_ENABLE;

#define VBOXCMDVBVA_SCREENMAP_SIZE(_elType) ((VBOX_VIDEO_MAX_SCREENS + sizeof (_elType) - 1) / sizeof (_elType))
#define VBOXCMDVBVA_SCREENMAP_DECL(_elType, _name) _elType _name[VBOXCMDVBVA_SCREENMAP_SIZE(_elType)]

typedef struct VBOXCMDVBVA_RESIZE_ENTRY {
	VBVAINFOSCREEN Screen;
	VBOXCMDVBVA_SCREENMAP_DECL(u32, aTargetMap);
} VBOXCMDVBVA_RESIZE_ENTRY;

typedef struct VBOXCMDVBVA_RESIZE {
	VBOXCMDVBVA_RESIZE_ENTRY aEntries[1];
} VBOXCMDVBVA_RESIZE;

typedef struct VBOXCMDVBVA_CTL_RESIZE {
	VBOXCMDVBVA_CTL Hdr;
	VBOXCMDVBVA_RESIZE Resize;
} VBOXCMDVBVA_CTL_RESIZE;

#define VBOXCMDVBVA3DCTL_TYPE_CONNECT     1
#define VBOXCMDVBVA3DCTL_TYPE_DISCONNECT  2
#define VBOXCMDVBVA3DCTL_TYPE_CMD         3

typedef struct VBOXCMDVBVA_3DCTL {
	u32 u32Type;
	u32 u32CmdClientId;
} VBOXCMDVBVA_3DCTL;

typedef struct VBOXCMDVBVA_3DCTL_CONNECT {
	VBOXCMDVBVA_3DCTL Hdr;
	u32 u32MajorVersion;
	u32 u32MinorVersion;
	uint64_t u64Pid;
} VBOXCMDVBVA_3DCTL_CONNECT;

typedef struct VBOXCMDVBVA_3DCTL_CMD {
	VBOXCMDVBVA_3DCTL Hdr;
	VBOXCMDVBVA_HDR Cmd;
} VBOXCMDVBVA_3DCTL_CMD;

typedef struct VBOXCMDVBVA_CTL_3DCTL_CMD {
	VBOXCMDVBVA_CTL Hdr;
	VBOXCMDVBVA_3DCTL_CMD Cmd;
} VBOXCMDVBVA_CTL_3DCTL_CMD;

typedef struct VBOXCMDVBVA_CTL_3DCTL_CONNECT {
	VBOXCMDVBVA_CTL Hdr;
	VBOXCMDVBVA_3DCTL_CONNECT Connect;
} VBOXCMDVBVA_CTL_3DCTL_CONNECT;

typedef struct VBOXCMDVBVA_CTL_3DCTL {
	VBOXCMDVBVA_CTL Hdr;
	VBOXCMDVBVA_3DCTL Ctl;
} VBOXCMDVBVA_CTL_3DCTL;

#pragma pack()


#ifdef VBOXVDMA_WITH_VBVA
# pragma pack(1)

typedef struct VBOXVDMAVBVACMD {
	u32 offCmd;
} VBOXVDMAVBVACMD;

#pragma pack()
#endif

#endif

