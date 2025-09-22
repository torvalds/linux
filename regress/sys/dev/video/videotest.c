/*	$OpenBSD: videotest.c,v 1.6 2021/10/24 21:24:20 deraadt Exp $ */

/*
 * Copyright (c) 2010 Marcus Glocker <mglocker@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Regression test program for the video(4) interface.
 *
 * TODO:
 * - Add test for VIDIOC_ENUM_FRAMEINTERVALS ioctl.
 * - Add test for VIDIOC_ENUMINPUT ioctl.
 * - Add test for VIDIOC_S_INPUT ioctl.
 * - Add test for VIDIOC_TRY_FMT ioctl.
 * - Add test for VIDIOC_QUERYCTRL ioctl.
 * - Add test for VIDIOC_G_CTRL ioctl.
 * - Add test for VIDIOC_S_CTRL ioctl.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/videoio.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Defines.
 */
#define DEV_CHECK_NR		128
#define	DEV_PATH		"/dev/"

/*
 * Some devices need a hell of time to initialize and stop (e.g. the
 * Logitech Pro 9000).  If we don't give them that time, they will stall
 * at some point in the open / close cycle and will require a cold reset
 * (detach / attach) to operate again.
 */
#define WAIT_INIT		5		/* seconds */
#define WAIT_STOP		15		/* seconds */

#define POLL_NO			0
#define	POLL_YES		1
#define POLL_TIMEOUT		2000		/* milliseconds */

#define	ACCESS_READ		0
#define ACCESS_MMAP		1

#define MMAP_QUEUE_NR		4

/*
 * Prototypes.
 */
int	 test_ioctl_querycap(int);
int	 test_ioctl_g_fmt(int);
int	 test_ioctl_enum_fmt(int);
int	 test_ioctl_enum_fsizes(int, uint32_t, int);
int	 test_capture(char *, char *, int, int);
int	 test_capture_read(int, char *, int, int);
int	 test_capture_mmap(int, char *, int, int);
void	 jpeg_insert_dht(uint8_t *, int, uint8_t *, int *);
char	*print_pixelformat(uint32_t, int);

/*
 * Structures.
 */
struct frame_buffer {
	uint8_t		*buf;
	int		 len;
};

struct sizes {
	uint32_t	width;
	uint32_t	height;
};

/*
 * Global variables.
 */
struct fmt_sizes {
	uint32_t	pixelformat;
	struct sizes	s[32];
} dev_fmts[8];

/*
 * Main program.
 */
int
main(void)
{
	int	i, fd, r;
	char	dev_name[32], dev_full[32];

	for (i = 0; i < DEV_CHECK_NR; i++) {
		/* assemble device name and path */
		snprintf(dev_name, sizeof(dev_name), "video%d", i);
		snprintf(dev_full, sizeof(dev_full), "%s%s",
		    DEV_PATH, dev_name);

		/* open video device */
		fd = open(dev_full, O_RDWR);
		if (fd == -1) {
			warn("%s", dev_full);
			break;
		}

		/* run some ioctl tests */
		r = test_ioctl_querycap(fd);
		if (r == -1)
			err(1, "ioctl_querycap");

		r = test_ioctl_g_fmt(fd);
		if (r == -1)
			err(1, "ioctl_g_fmt");

		r = test_ioctl_enum_fmt(fd);
		if (r == -1)
			err(1, "ioctl_enum_fmt");

		/* close video device */
		close(fd);

		/* run frame capture tests */
		r = test_capture(dev_name, dev_full, ACCESS_READ, POLL_NO);
		if (r == -1)
			err(1, "test_capture");

		r = test_capture(dev_name, dev_full, ACCESS_READ, POLL_YES);
		if (r == -1)
			err(1, "test_capture");

		r = test_capture(dev_name, dev_full, ACCESS_MMAP, POLL_NO);
		if (r == -1)
			err(1, "test_capture");

		r = test_capture(dev_name, dev_full, ACCESS_MMAP, POLL_YES);
		if (r == -1)
			err(1, "test_capture");
	}

	return (0);
}

int
test_ioctl_querycap(int fd)
{
	int			r;
	struct v4l2_capability	caps;

	printf("[ Calling VIDIOC_QUERYCAP ioctl ]\n\n");

	memset(&caps, 0, sizeof(struct v4l2_capability));
	r = ioctl(fd, VIDIOC_QUERYCAP, &caps);
	if (r == -1)
		return (-1);

	printf("Driver       : %s\n", caps.driver);
	printf("Card         : %s\n", caps.card);
	printf("Bus Info     : %s\n", caps.bus_info);
	printf("Version      : %d\n", caps.version);
	printf("Capabilities : ");
	if (caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)
		printf("CAPTURE ");
	if (caps.capabilities & V4L2_CAP_STREAMING)
		printf("STREAMING ");
	if (caps.capabilities & V4L2_CAP_READWRITE)
		printf("READWRITE ");
	printf("\n");

	printf("\n");

	return (0);
}

int
test_ioctl_g_fmt(int fd)
{
	int			r;
	struct v4l2_format	fmt;

	printf("[ Calling VIDIOC_G_FMT ioctl ]\n\n");

	memset(&fmt, 0, sizeof(struct v4l2_format));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	r = ioctl(fd, VIDIOC_G_FMT, &fmt);
	if (r == -1)
		return (r);

	printf("Current format         : %s\n",
	    print_pixelformat(fmt.fmt.pix.pixelformat, 0));
	printf("Current width          : %u pixels\n", fmt.fmt.pix.width);
	printf("Current height         : %u pixels\n", fmt.fmt.pix.height);
	printf("Current max. framesize : %u bytes\n", fmt.fmt.pix.sizeimage);

	printf("\n");

	return (0);
}

int
test_ioctl_enum_fmt(int fd)
{
	int			r;
	struct v4l2_fmtdesc	fmtdesc;

	printf("[ Calling VIDIOC_ENUM_FMT|VIDIOC_ENUM_FRAMESIZES ioctl ]\n\n");

	memset(&fmtdesc, 0, sizeof(struct v4l2_fmtdesc));
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while ((r = ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) == 0) {
		printf("Pixelformat '%s' ", fmtdesc.description);

		(void)test_ioctl_enum_fsizes(fd, fmtdesc.pixelformat,
		    fmtdesc.index);

		fmtdesc.index++;
	}
	if (errno != EINVAL)
		return (-1);

	return (0);
}

int
test_ioctl_enum_fsizes(int fd, uint32_t pixelformat, int index)
{
	int			r;
	struct v4l2_frmsizeenum	fsizes;

	printf("supports following sizes:\n");

	memset(&fsizes, 0, sizeof(struct v4l2_frmsizeenum));
	fsizes.index = 0;
	fsizes.pixel_format = pixelformat;
	while ((r = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsizes)) == 0) {
		if (fsizes.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
			printf("discrete width = %u, height = %u\n",
			    fsizes.discrete.width,
			    fsizes.discrete.height);

			/* save format and sizes for later use */
			dev_fmts[index].pixelformat = pixelformat;
			dev_fmts[index].s[fsizes.index].width =
			    fsizes.discrete.width;
			dev_fmts[index].s[fsizes.index].height =
			    fsizes.discrete.height;
		}

		fsizes.index++;
	}
	if (errno != EINVAL)
		return (-1);

	printf("\n");

	return (0);
}

int
test_capture(char *dev_name, char *dev_full, int access, int use_poll)
{
	ssize_t			 n1, n2;
	int			 fd1, fd2;
	int			 i, j, r;
	int			 buf_size, img_size, img_len;
	char			 filename[64];
	uint8_t			*buf, *img;
	uint32_t		 last_pixelformat;
	struct v4l2_format	 fmt;

	fd1 = last_pixelformat = n1 = 0;
	img = buf = NULL;

	printf("[ Testing %s access type %s]\n\n",
	    access == ACCESS_READ ? "READ" : "MMAP",
	    use_poll ? "with poll " : "");

	for (i = 0; i < 8; i++) {
		/* did we reach end of formats? */
		if (dev_fmts[i].pixelformat == 0)
			return (0);

		/* some devices have duplicate format descriptors */
		if (last_pixelformat == dev_fmts[i].pixelformat)
			continue;
		else
			last_pixelformat = dev_fmts[i].pixelformat;

		for (j = 0; j < 32; j++) {
			/* did we reach end of sizes? */
			if (dev_fmts[i].s[j].width == 0) {
				free(buf);
				buf = NULL;
				free(img);
				img = NULL;
				break;
			}

			/* open device */
			fd1 = open(dev_full, O_RDWR);
			if (fd1 == -1)
				err(1, "open");
			sleep(WAIT_INIT);	/* let device initialize */

			/* set format */
			printf("Set format to %s-%ux%u ... ",
			    print_pixelformat(dev_fmts[i].pixelformat, 0),
			    dev_fmts[i].s[j].width,
			    dev_fmts[i].s[j].height);

			memset(&fmt, 0, sizeof(struct v4l2_format));
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fmt.fmt.pix.width = dev_fmts[i].s[j].width;
			fmt.fmt.pix.height = dev_fmts[i].s[j].height;
			fmt.fmt.pix.pixelformat = dev_fmts[i].pixelformat;
			fmt.fmt.pix.field = V4L2_FIELD_ANY;
			r = ioctl(fd1, VIDIOC_S_FMT, &fmt);
			if (r == -1)
				goto error;

			printf("results in %u bytes max. framesize.\n",
			    fmt.fmt.pix.sizeimage);

			/* allocate frame and image buffer */
			free(buf);
			buf = NULL;
			free(img);
			img = NULL;

			buf_size = fmt.fmt.pix.sizeimage;
			buf = calloc(1, buf_size);
			if (buf == NULL)
				goto error;

			img_size = fmt.fmt.pix.sizeimage + 1024;
			img = calloc(1, img_size);
			if (img == NULL)
				goto error;

			/* get frame */
			if (access == ACCESS_READ)
				n1 = test_capture_read(fd1, buf, buf_size,
				    use_poll);
			else
				n1 = test_capture_mmap(fd1, buf, buf_size,
				    use_poll);
			if (n1 == -1)
				goto error;

			/*
			 * Convert frame to JPEG image.
			 *
			 * TODO:
			 * For now just MJPEG convertion is supported.
			 */
			snprintf(filename, sizeof(filename),
			    "%s_img_%s_%ux%u%s%s",
			    dev_name,
			    print_pixelformat(dev_fmts[i].pixelformat, 1),
			    fmt.fmt.pix.width,
			    fmt.fmt.pix.height,
			    access == ACCESS_READ ? "_read" : "_mmap",
			    use_poll ? "_poll" : "");

			switch (dev_fmts[i].pixelformat) {
			case V4L2_PIX_FMT_MJPEG:
				printf("Converting MJPEG to JPEG.\n");

				/* insert dynamic huffmann table to mjpeg */
				jpeg_insert_dht(buf, n1, img, &img_len);

				strlcat(filename, ".jpg", sizeof(filename));
				break;
			case V4L2_PIX_FMT_YUYV:
				printf("Convertion for YUYV not supported!\n");

				img_len = n1;
				memcpy(img, buf, img_len);

				strlcat(filename, ".raw", sizeof(filename));
				break;
			default:
				break;
			}

			/* write image file */
			fd2 = open(filename, O_RDWR|O_CREAT|O_TRUNC, 0644);
			if (fd2 == -1)
				goto error;
			n2 = write(fd2, img, img_len);
			if (n2 == -1)
				goto error;
			printf("Saving image to '%s'.\n\n", filename);
			close(fd2);

			/* shutdown device */
			close(fd1);
			sleep(WAIT_STOP);	/* let device stop */
		}
	}
error:
	free(buf);
	buf = NULL;
	free(img);
	img = NULL;
	close(fd1);

	printf("\n");

	return (-1);
}

int
test_capture_read(int fd, char *buf, int buf_size, int use_poll)
{
	ssize_t		n1;
	int		i, r;
	struct pollfd	pfds[1];

	n1 = 0;

	/*
	 * Read frame data.
	 *
	 * Some devices need a while until they start
	 * sending a sane image.  Therefore skip the
	 * first few frames.
	 */
	printf("Reading frame data ... ");

	for (i = 0; i < 3; i++) {
		if (use_poll) {
			pfds[0].fd = fd;
			pfds[0].events = POLLIN;

			r = poll(pfds, 1, POLL_TIMEOUT);
			if (r == -1)
				return (-1);
			if (r == 0) {
				printf("poll timeout (%d seconds)!\n",
				    POLL_TIMEOUT / 1000);
				continue;
			}
		}

		n1 = read(fd, buf, buf_size);
		if (n1 == -1)
			return (-1);
	}
	printf("%ld bytes read.\n", n1);

	return (n1);
}

int
test_capture_mmap(int fd, char *buf, int buf_size, int use_poll)
{
	int				 i, r, type;
	struct v4l2_requestbuffers	 reqbufs;
	struct v4l2_buffer		 buffer;
	struct frame_buffer		 fbuffer[MMAP_QUEUE_NR];
	struct pollfd			 pfds[1];

	/* request buffers */
	memset(&reqbufs, 0, sizeof(struct v4l2_requestbuffers));
	reqbufs.count = MMAP_QUEUE_NR;
	reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbufs.memory = V4L2_MEMORY_MMAP;
	r = ioctl(fd, VIDIOC_REQBUFS, &reqbufs);
	if (r == -1)
		return (-1);

	/* map the buffers */
	for (i = 0; i < MMAP_QUEUE_NR; i++) {
		memset(&buffer, 0, sizeof(struct v4l2_buffer));
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		r = ioctl(fd, VIDIOC_QUERYBUF, &buffer);
		if (r == -1)
			return (-1);

		fbuffer[i].buf =
		    mmap(0, buffer.length, PROT_READ, MAP_SHARED, fd,
		        buffer.m.offset);
		if (fbuffer[i].buf == MAP_FAILED)
			return (-1);
		fbuffer[i].len = buffer.length;
	}

	/* queue the buffers */
	for (i = 0; i < MMAP_QUEUE_NR; i++) {
		memset(&buffer, 0, sizeof(struct v4l2_buffer));
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		r = ioctl(fd, VIDIOC_QBUF, &buffer);
		if (r == -1)
			return (-1);
	}

	/* turn on stream */
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	r = ioctl(fd, VIDIOC_STREAMON, &type);
	if (r == -1)
		return (-1);

	/* dequeue buffers */
	printf("Dequeue frame data ... ");

	for (i = 0; i < MMAP_QUEUE_NR; i++) {
		if (use_poll) {
			pfds[0].fd = fd;
			pfds[0].events = POLLIN;

			r = poll(pfds, 1, POLL_TIMEOUT);
			if (r == -1)
				return (-1);
			if (r == 0) {
				printf("poll timeout (%d seconds)!\n",
				    POLL_TIMEOUT / 1000);
				return (-1);
			}
		}

		memset(&buffer, 0, sizeof(struct v4l2_buffer));
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		r = ioctl(fd, VIDIOC_DQBUF, &buffer);
		if (r == -1)
			return (-1);
	}
	printf("%d bytes dequeued.\n", buffer.bytesused);
	memcpy(buf, fbuffer[buffer.index].buf, buffer.bytesused);

	/* turn off stream */
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	r = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (r == -1)
		return (-1);

	/* unmap buffers */
	for (i = 0; i < MMAP_QUEUE_NR; i++) {
		r = munmap(fbuffer[i].buf, fbuffer[i].len);
		if (r == -1)
			return (-1);
	}

	return (buffer.bytesused);
}

void
jpeg_insert_dht(uint8_t *src, int src_len, uint8_t *dst, int *dst_len)
{
	int	 i;
	uint8_t	*p;

	static unsigned char dht[] = {
	    0xff, 0xc4, 0x01, 0xa2, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
	    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	    0x09, 0x0a, 0x0b, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02,
	    0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d,
	    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31,
	    0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
	    0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
	    0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
	    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
	    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57,
	    0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
	    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
	    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8,
	    0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
	    0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	    0xf9, 0xfa, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
	    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
	    0x0b, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	    0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01,
	    0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
	    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14,
	    0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
	    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a,
	    0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46,
	    0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
	    0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83,
	    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
	    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8,
	    0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa
	};

	p = src;
	for (i = 0; i < src_len; i++, p++) {
		if (*p != 0xff)
			continue;

		p++;
		if (*p == 0xda)
			break;
		else
			i++;
	}

	memcpy(dst, src, i);
	dst += i;
	memcpy(dst, dht, sizeof(dht));
	dst += sizeof(dht);
	src += i;
	memcpy(dst, src, src_len - i);

	*dst_len = src_len + sizeof(dht);
}

char *
print_pixelformat(uint32_t pixelformat, int lowercase)
{
	static char	pformat[8];

	memset(pformat, 0, sizeof(pformat));

	switch (pixelformat) {
	case V4L2_PIX_FMT_MJPEG:
		if (lowercase)
			memcpy(pformat, "mjpeg", 5);
		else
			memcpy(pformat, "MJPEG", 5);
		break;
	case V4L2_PIX_FMT_YUYV:
		if (lowercase)
			memcpy(pformat, "yuyv", 4);
		else
			memcpy(pformat, "YUYV", 4);
		break;
	default:
		memcpy(pformat, "unknown", 7);
		break;
	}

	return (pformat);
}
