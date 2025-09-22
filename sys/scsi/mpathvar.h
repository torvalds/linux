/*	$OpenBSD: mpathvar.h,v 1.10 2019/09/27 23:07:42 krw Exp $ */

/*
 * Copyright (c) 2010 David Gwynne <dlg@openbsd.org>
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

#ifndef _SCSI_MPATHVAR_H
#define _SCSI_MPATHVAR_H

struct mpath_group;

struct mpath_ops {
	char	op_name[16];
	int	(*op_checksense)(struct scsi_xfer *);
	void	(*op_status)(struct scsi_link *);
};

#define MPATH_SENSE_DECLINED	0 /* path driver declined to interpret sense */
#define MPATH_SENSE_FAILOVER	1 /* sense says controllers have failed over */

#define MPATH_S_UNKNOWN		-1
#define MPATH_S_ACTIVE		0
#define MPATH_S_PASSIVE		1

struct mpath_path {
	/* the path driver must set these */
	struct scsi_xshandler	 p_xsh;
	struct scsi_link	*p_link;
	int			 p_gid;

	/* the following are private to mpath.c */
	TAILQ_ENTRY(mpath_path)	 p_entry;
	struct mpath_group	*p_group;
	int			 p_state;
};

int			 mpath_path_probe(struct scsi_link *);
int			 mpath_path_attach(struct mpath_path *, u_int,
			    const struct mpath_ops *);
void			 mpath_path_status(struct mpath_path *, int);
int			 mpath_path_detach(struct mpath_path *);

void			 mpath_start(struct mpath_path *, struct scsi_xfer *);

struct device		*mpath_bootdv(struct device *);

#endif /* _SCSI_MPATHVAR_H */
