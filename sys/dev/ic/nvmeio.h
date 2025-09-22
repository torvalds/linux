/* $OpenBSD: nvmeio.h,v 1.1 2024/05/24 12:04:07 krw Exp $	*/
/*
 * Copyright (c) 2023 Kenneth R Westerback <krw@openbsd.org>
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

#define NVME_PASSTHROUGH_CMD	_IOWR('n', 0, struct nvme_pt_cmd)

struct nvme_pt_status {
	int			ps_dv_unit;
	int			ps_nsid;
	int			ps_flags;
	uint32_t		ps_csts;
	uint32_t		ps_cc;
};

struct nvme_pt_cmd {
	/* Commands may arrive via /dev/bio. */
	struct bio		pt_bio;

	/* The sqe fields that the caller may specify. */
	uint8_t			pt_opcode;
	uint32_t		pt_nsid;
	uint32_t		pt_cdw10;
	uint32_t		pt_cdw11;
	uint32_t		pt_cdw12;
	uint32_t		pt_cdw13;
	uint32_t		pt_cdw14;
	uint32_t		pt_cdw15;

	caddr_t			pt_status;
	uint32_t		pt_statuslen;

	caddr_t			pt_databuf;	/* User space address. */
	uint32_t		pt_databuflen;	/* Length of buffer. */
};
