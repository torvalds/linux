/*	$OpenBSD: spmivar.h,v 1.1 2021/05/26 20:52:21 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

#define SPMI_CMD_EXT_WRITEL	0x30
#define SPMI_CMD_EXT_READL	0x38

typedef struct spmi_controller {
	void	*sc_cookie;
	int	(*sc_cmd_read)(void *, uint8_t, uint8_t, uint16_t,
		    void *, size_t);
	int	(*sc_cmd_write)(void *, uint8_t, uint8_t, uint16_t,
		    const void *, size_t);
} *spmi_tag_t;

struct spmi_attach_args {
	spmi_tag_t	sa_tag;
	uint8_t		sa_sid;
	char		*sa_name;
	int		sa_node;
};

#define spmi_cmd_read(sc, sid, cmd, addr, buf, len)			\
    (*(sc)->sc_cmd_read)((sc)->sc_cookie, (sid), (cmd), (addr), (buf), (len))
#define spmi_cmd_write(sc, sid, cmd, addr, buf, len)			\
    (*(sc)->sc_cmd_write)((sc)->sc_cookie, (sid), (cmd), (addr), (buf), (len))
