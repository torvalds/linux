/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#define _BSD_SOURCE /* for endian.h */

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/eventfd.h>

#include "libaio.h"
#define IOCB_FLAG_RESFD         (1 << 0)

#include <linux/usb/functionfs.h>

#define BUF_LEN		8192
#define BUFS_MAX	128
#define AIO_MAX		(BUFS_MAX*2)

/******************** Descriptors and Strings *******************************/

static const struct {
	struct usb_functionfs_descs_head_v2 header;
	__le32 fs_count;
	__le32 hs_count;
	__le32 ss_count;
	__le32 os_count;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio bulk_sink;
		struct usb_endpoint_descriptor_no_audio bulk_source;
	} __attribute__ ((__packed__)) fs_descs, hs_descs;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio sink;
		struct usb_ss_ep_comp_descriptor sink_comp;
		struct usb_endpoint_descriptor_no_audio source;
		struct usb_ss_ep_comp_descriptor source_comp;
	} __attribute__ ((__packed__)) ss_descs;
	struct usb_os_desc_header os_header;
	struct usb_ext_compat_desc os_desc;

} __attribute__ ((__packed__)) descriptors = {
	.header = {
		.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.flags = htole32(FUNCTIONFS_HAS_FS_DESC |
				 FUNCTIONFS_HAS_HS_DESC |
				 FUNCTIONFS_HAS_SS_DESC |
				 FUNCTIONFS_HAS_MS_OS_DESC),
		.length = htole32(sizeof(descriptors)),
	},
	.fs_count = htole32(3),
	.fs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.fs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.fs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.fs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
		},
	},
	.hs_count = htole32(3),
	.hs_descs = {
		.intf = {
			.bLength = sizeof(descriptors.hs_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.bulk_sink = {
			.bLength = sizeof(descriptors.hs_descs.bulk_sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = htole16(512),
		},
		.bulk_source = {
			.bLength = sizeof(descriptors.hs_descs.bulk_source),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = htole16(512),
		},
	},
	.ss_count = htole32(5),
	.ss_descs = {
		.intf = {
			.bLength = sizeof(descriptors.ss_descs.intf),
			.bDescriptorType = USB_DT_INTERFACE,
			.bInterfaceNumber = 0,
			.bNumEndpoints = 2,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.sink = {
			.bLength = sizeof(descriptors.ss_descs.sink),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = htole16(1024),
		},
		.sink_comp = {
			.bLength = sizeof(descriptors.ss_descs.sink_comp),
			.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
			.bMaxBurst = 4,
		},
		.source = {
			.bLength = sizeof(descriptors.ss_descs.source),
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = htole16(1024),
		},
		.source_comp = {
			.bLength = sizeof(descriptors.ss_descs.source_comp),
			.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
			.bMaxBurst = 4,
		},
	},
	.os_count = htole32(1),
	.os_header = {
		.interface = htole32(1),
		.dwLength = htole32(sizeof(descriptors.os_header) +
			    sizeof(descriptors.os_desc)),
		.bcdVersion = htole32(1),
		.wIndex = htole32(4),
		.bCount = htole32(1),
		.Reserved = htole32(0),
	},
	.os_desc = {
		.bFirstInterfaceNumber = 0,
		.Reserved1 = htole32(1),
		.CompatibleID = {0},
		.SubCompatibleID = {0},
		.Reserved2 = {0},
	},
};

#define STR_INTERFACE "AIO Test"

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof(STR_INTERFACE)];
	} __attribute__ ((__packed__)) lang0;
} __attribute__ ((__packed__)) strings = {
	.header = {
		.magic = htole32(FUNCTIONFS_STRINGS_MAGIC),
		.length = htole32(sizeof(strings)),
		.str_count = htole32(1),
		.lang_count = htole32(1),
	},
	.lang0 = {
		htole16(0x0409), /* en-us */
		STR_INTERFACE,
	},
};

/********************** Buffer structure *******************************/

struct io_buffer {
	struct iocb **iocb;
	unsigned char **buf;
	unsigned cnt;
	unsigned len;
	unsigned requested;
};

/******************** Endpoints handling *******************************/

static void display_event(struct usb_functionfs_event *event)
{
	static const char *const names[] = {
		[FUNCTIONFS_BIND] = "BIND",
		[FUNCTIONFS_UNBIND] = "UNBIND",
		[FUNCTIONFS_ENABLE] = "ENABLE",
		[FUNCTIONFS_DISABLE] = "DISABLE",
		[FUNCTIONFS_SETUP] = "SETUP",
		[FUNCTIONFS_SUSPEND] = "SUSPEND",
		[FUNCTIONFS_RESUME] = "RESUME",
	};
	switch (event->type) {
	case FUNCTIONFS_BIND:
	case FUNCTIONFS_UNBIND:
	case FUNCTIONFS_ENABLE:
	case FUNCTIONFS_DISABLE:
	case FUNCTIONFS_SETUP:
	case FUNCTIONFS_SUSPEND:
	case FUNCTIONFS_RESUME:
		printf("Event %s\n", names[event->type]);
	}
}

static void handle_ep0(int ep0, bool *ready)
{
	int ret;
	struct usb_functionfs_event event;

	ret = read(ep0, &event, sizeof(event));
	if (!ret) {
		perror("unable to read event from ep0");
		return;
	}
	display_event(&event);
	switch (event.type) {
	case FUNCTIONFS_SETUP:
		if (event.u.setup.bRequestType & USB_DIR_IN)
			write(ep0, NULL, 0);
		else
			read(ep0, NULL, 0);
		break;

	case FUNCTIONFS_ENABLE:
		*ready = true;
		break;

	case FUNCTIONFS_DISABLE:
		*ready = false;
		break;

	default:
		break;
	}
}

void init_bufs(struct io_buffer *iobuf, unsigned n, unsigned len)
{
	unsigned i;
	iobuf->buf = malloc(n*sizeof(*iobuf->buf));
	iobuf->iocb = malloc(n*sizeof(*iobuf->iocb));
	iobuf->cnt = n;
	iobuf->len = len;
	iobuf->requested = 0;
	for (i = 0; i < n; ++i) {
		iobuf->buf[i] = malloc(len*sizeof(**iobuf->buf));
		iobuf->iocb[i] = malloc(sizeof(**iobuf->iocb));
	}
	iobuf->cnt = n;
}

void delete_bufs(struct io_buffer *iobuf)
{
	unsigned i;
	for (i = 0; i < iobuf->cnt; ++i) {
		free(iobuf->buf[i]);
		free(iobuf->iocb[i]);
	}
	free(iobuf->buf);
	free(iobuf->iocb);
}

int main(int argc, char *argv[])
{
	int ret;
	unsigned i, j;
	char *ep_path;

	int ep0, ep1;

	io_context_t ctx;

	int evfd;
	fd_set rfds;

	struct io_buffer iobuf[2];
	int actual = 0;
	bool ready;

	if (argc != 2) {
		printf("ffs directory not specified!\n");
		return 1;
	}

	ep_path = malloc(strlen(argv[1]) + 4 /* "/ep#" */ + 1 /* '\0' */);
	if (!ep_path) {
		perror("malloc");
		return 1;
	}

	/* open endpoint files */
	sprintf(ep_path, "%s/ep0", argv[1]);
	ep0 = open(ep_path, O_RDWR);
	if (ep0 < 0) {
		perror("unable to open ep0");
		return 1;
	}
	if (write(ep0, &descriptors, sizeof(descriptors)) < 0) {
		perror("unable do write descriptors");
		return 1;
	}
	if (write(ep0, &strings, sizeof(strings)) < 0) {
		perror("unable to write strings");
		return 1;
	}
	sprintf(ep_path, "%s/ep1", argv[1]);
	ep1 = open(ep_path, O_RDWR);
	if (ep1 < 0) {
		perror("unable to open ep1");
		return 1;
	}

	free(ep_path);

	memset(&ctx, 0, sizeof(ctx));
	/* setup aio context to handle up to AIO_MAX requests */
	if (io_setup(AIO_MAX, &ctx) < 0) {
		perror("unable to setup aio");
		return 1;
	}

	evfd = eventfd(0, 0);
	if (evfd < 0) {
		perror("unable to open eventfd");
		return 1;
	}

	for (i = 0; i < sizeof(iobuf)/sizeof(*iobuf); ++i)
		init_bufs(&iobuf[i], BUFS_MAX, BUF_LEN);

	while (1) {
		FD_ZERO(&rfds);
		FD_SET(ep0, &rfds);
		FD_SET(evfd, &rfds);

		ret = select(((ep0 > evfd) ? ep0 : evfd)+1,
			     &rfds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}

		if (FD_ISSET(ep0, &rfds))
			handle_ep0(ep0, &ready);

		/* we are waiting for function ENABLE */
		if (!ready)
			continue;

		/*
		 * when we're preparing new data to submit,
		 * second buffer being transmitted
		 */
		for (i = 0; i < sizeof(iobuf)/sizeof(*iobuf); ++i) {
			if (iobuf[i].requested)
				continue;
			/* prepare requests */
			for (j = 0; j < iobuf[i].cnt; ++j) {
				io_prep_pwrite(iobuf[i].iocb[j], ep1,
					       iobuf[i].buf[j],
					       iobuf[i].len, 0);
				/* enable eventfd notification */
				iobuf[i].iocb[j]->u.c.flags |= IOCB_FLAG_RESFD;
				iobuf[i].iocb[j]->u.c.resfd = evfd;
			}
			/* submit table of requests */
			ret = io_submit(ctx, iobuf[i].cnt, iobuf[i].iocb);
			if (ret >= 0) {
				iobuf[i].requested = ret;
				printf("submit: %d requests buf: %d\n", ret, i);
			} else
				perror("unable to submit requests");
		}

		/* if event is ready to read */
		if (!FD_ISSET(evfd, &rfds))
			continue;

		uint64_t ev_cnt;
		ret = read(evfd, &ev_cnt, sizeof(ev_cnt));
		if (ret < 0) {
			perror("unable to read eventfd");
			break;
		}

		struct io_event e[BUFS_MAX];
		/* we read aio events */
		ret = io_getevents(ctx, 1, BUFS_MAX, e, NULL);
		if (ret > 0) /* if we got events */
			iobuf[actual].requested -= ret;

		/* if all req's from iocb completed */
		if (!iobuf[actual].requested)
			actual = (actual + 1)%(sizeof(iobuf)/sizeof(*iobuf));
	}

	/* free resources */

	for (i = 0; i < sizeof(iobuf)/sizeof(*iobuf); ++i)
		delete_bufs(&iobuf[i]);
	io_destroy(ctx);

	close(ep1);
	close(ep0);

	return 0;
}
