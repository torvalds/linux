/*
 * $FreeBSD$
 */

/*
 * This file defines compatibility versions of several video structures
 * defined in the Linux videodev.h header (linux_videodev.h).  The
 * structures defined in this file are the ones that have been determined
 * to have 32- to 64-bit size dependencies.
 */

#ifndef _LINUX_VIDEODEV_COMPAT_H_
#define	_LINUX_VIDEODEV_COMPAT_H_

struct l_video_tuner
{
	l_int		tuner;
#define LINUX_VIDEO_TUNER_NAME_SIZE	32
	char		name[LINUX_VIDEO_TUNER_NAME_SIZE];
	l_ulong		rangelow, rangehigh;
	uint32_t	flags;
	uint16_t	mode;
	uint16_t	signal;
};

struct l_video_clip
{
	int32_t		x, y;
	int32_t		width, height;
	l_uintptr_t	next;
};

struct l_video_window
{
	uint32_t	x, y;
	uint32_t	width, height;
	uint32_t	chromakey;
	uint32_t	flags;
	l_uintptr_t	clips;
	l_int		clipcount;
};

struct l_video_buffer
{
	l_uintptr_t	base;
	l_int		height, width;
	l_int		depth;
	l_int		bytesperline;
};

struct l_video_code
{
#define LINUX_VIDEO_CODE_LOADWHAT_SIZE	16
	char		loadwhat[LINUX_VIDEO_CODE_LOADWHAT_SIZE];
	l_int		datasize;
	l_uintptr_t	data;
};

#endif /* !_LINUX_VIDEODEV_COMPAT_H_ */
