/*	$OpenBSD: vscsivar.h,v 1.5 2011/04/05 15:28:49 dlg Exp $ */

/*
 * Copyright (c) 2008 David Gwynne <dlg@openbsd.org>
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

#ifndef _SYS_DEV_VSCSIVAR_H
#define _SYS_DEV_VSCSIVAR_H

struct vscsi_ioc_i2t {
	int			tag;

	u_int			target;
	u_int			lun;

	struct scsi_generic	cmd;
	size_t			cmdlen;

	size_t			datalen;
	int			direction;
#define VSCSI_DIR_NONE		0
#define VSCSI_DIR_READ		1
#define VSCSI_DIR_WRITE		2
};

#define VSCSI_I2T _IOR('I', 0, struct vscsi_ioc_i2t)

struct vscsi_ioc_data {
	int			tag;

	void *			data;
	size_t			datalen;
};

#define VSCSI_DATA_READ _IOW('I', 1, struct vscsi_ioc_data)
#define VSCSI_DATA_WRITE _IOW('I', 2, struct vscsi_ioc_data)

struct vscsi_ioc_t2i {
	int			tag;

	int			status;
#define VSCSI_STAT_DONE		0
#define VSCSI_STAT_SENSE	1
#define VSCSI_STAT_RESET	2
#define VSCSI_STAT_ERR		3
	struct scsi_sense_data	sense;
};

#define VSCSI_T2I _IOW('I', 3, struct vscsi_ioc_t2i)

struct vscsi_ioc_devevent {
	int			target;
	int			lun;
};

#define VSCSI_REQPROBE _IOW('I', 4, struct vscsi_ioc_devevent)
#define VSCSI_REQDETACH _IOW('I', 5, struct vscsi_ioc_devevent)

#endif /* _SYS_DEV_VSCSIVAR_H */
