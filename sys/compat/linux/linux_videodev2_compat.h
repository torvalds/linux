/*
 * $FreeBSD$
 */

/*
 * This file defines compatibility versions of several video structures
 * defined in the Linux videodev2.h header (linux_videodev2.h).  The
 * structures defined in this file are the ones that have been determined
 * to have 32- to 64-bit size dependencies.
 */

#ifndef _LINUX_VIDEODEV2_COMPAT_H_
#define	_LINUX_VIDEODEV2_COMPAT_H_

struct l_v4l2_buffer {
	uint32_t		index;
	enum v4l2_buf_type	type;
	uint32_t		bytesused;
	uint32_t		flags;
	enum v4l2_field		field;
	l_timeval		timestamp;
	struct v4l2_timecode	timecode;
	uint32_t		sequence;

	/* memory location */
	enum v4l2_memory	memory;
	union {
		uint32_t	offset;
		l_ulong		userptr;
	} m;
	uint32_t		length;
	uint32_t		input;
	uint32_t		reserved;
};

struct l_v4l2_framebuffer {
	uint32_t		capability;
	uint32_t		flags;
/* FIXME: in theory we should pass something like PCI device + memory
 * region + offset instead of some physical address */
	l_uintptr_t		base;
	struct v4l2_pix_format	fmt;
};

struct l_v4l2_clip {
	struct v4l2_rect	c;
	l_uintptr_t		next;
};

struct l_v4l2_window {
	struct v4l2_rect	w;
	enum v4l2_field		field;
	uint32_t		chromakey;
	l_uintptr_t		clips;
	uint32_t		clipcount;
	l_uintptr_t		bitmap;
	uint8_t			global_alpha;
};

struct l_v4l2_standard {
	uint32_t		index;
	v4l2_std_id		id;
	uint8_t			name[24];
	struct v4l2_fract	frameperiod; /* Frames, not fields */
	uint32_t		framelines;
	uint32_t		reserved[4];
}
#ifdef COMPAT_LINUX32 /* 32bit linuxolator */
__attribute__ ((packed))
#endif
;

struct l_v4l2_ext_control {
	uint32_t id;
	uint32_t size;
	uint32_t reserved2[1];
	union {
		int32_t value;
		int64_t value64;
		l_uintptr_t string;
	} u;
} __attribute__ ((packed));

struct l_v4l2_ext_controls {
	uint32_t ctrl_class;
	uint32_t count;
	uint32_t error_idx;
	uint32_t reserved[2];
	l_uintptr_t controls;
};

struct l_v4l2_format {
	enum v4l2_buf_type type;
	union {
		struct v4l2_pix_format		pix;     /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
		struct l_v4l2_window		win;     /* V4L2_BUF_TYPE_VIDEO_OVERLAY */
		struct v4l2_vbi_format		vbi;     /* V4L2_BUF_TYPE_VBI_CAPTURE */
		struct v4l2_sliced_vbi_format	sliced;  /* V4L2_BUF_TYPE_SLICED_VBI_CAPTURE */
		uint8_t	raw_data[200];                   /* user-defined */
	} fmt;
}
#ifdef COMPAT_LINUX32 /* 32bit linuxolator */
__attribute__ ((packed))
#endif
;

#ifdef VIDIOC_DQEVENT
struct l_v4l2_event {
	uint32_t				type;
	union {
		struct v4l2_event_vsync vsync;
		uint8_t			data[64];
	} u;
	uint32_t				pending;
	uint32_t				sequence;
	struct l_timespec			timestamp;
	uint32_t				reserved[9];
};
#endif

struct l_v4l2_input {
	uint32_t	     index;		/*  Which input */
	uint8_t		     name[32];		/*  Label */
	uint32_t	     type;		/*  Type of input */
	uint32_t	     audioset;		/*  Associated audios (bitfield) */
	uint32_t	     tuner;             /*  Associated tuner */
	v4l2_std_id  std;
	uint32_t	     status;
	uint32_t	     capabilities;
	uint32_t	     reserved[3];
}
#ifdef COMPAT_LINUX32 /* 32bit linuxolator */
__attribute__ ((packed))
#endif
;

#endif /* _LINUX_VIDEODEV2_COMPAT_H_ */
