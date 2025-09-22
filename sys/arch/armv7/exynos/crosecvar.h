/* $OpenBSD: crosecvar.h,v 1.2 2015/07/19 01:13:27 bmercer Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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

#ifndef CROSECVAR_H
#define CROSECVAR_H

#include <sys/timeout.h>
#include <sys/task.h>
#include <dev/i2c/i2cvar.h>
#include <armv7/exynos/ec_commands.h>

/* message sizes */
#define MSG_HEADER		0xec
#define MSG_HEADER_BYTES	3
#define MSG_TRAILER_BYTES	2
#define MSG_PROTO_BYTES		(MSG_HEADER_BYTES + MSG_TRAILER_BYTES)
#define MSG_BYTES		(EC_HOST_PARAM_SIZE + MSG_PROTO_BYTES)
#define MSG_BYTES_ALIGNED	((MSG_BYTES+8) & ~8)

#define min(a,b)	(((a)<(b))?(a):(b))

struct cros_ec_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;

	int cmd_version_is_supported;
	struct {
		int rows;
		int cols;
		int switches;
		uint8_t *state;

		/* wskbd bits */
		struct device *wskbddev;
		int rawkbd;
		int polling;

		/* polling */
		struct timeout timeout;
		struct taskq *taskq;
		struct task task;
	} keyboard;
	uint8_t in[MSG_BYTES_ALIGNED];
	uint8_t out[MSG_BYTES_ALIGNED];
};

int	cros_ec_check_version(struct cros_ec_softc *);
int	cros_ec_scan_keyboard(struct cros_ec_softc *, uint8_t *, int);
int	cros_ec_info(struct cros_ec_softc *, struct ec_response_cros_ec_info *);

int	cros_ec_init_keyboard(struct cros_ec_softc *);

#endif /* !CROSECVAR_H */
