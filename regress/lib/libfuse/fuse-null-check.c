/* $OpenBSD: fuse-null-check.c,v 1.2 2018/07/20 12:05:08 helg Exp $ */
/*
 * Copyright (c) 2018 Helg Bredow <helg@openbsd.org>
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

#include <fuse.h>
#include <stdlib.h>

int
main(void)
{
	fuse_main(0, NULL, NULL, NULL);
	fuse_new(NULL, NULL, NULL, 0, NULL);
	fuse_setup(0, NULL, NULL, 0, NULL, NULL, NULL);
	fuse_mount(NULL, NULL);
	fuse_remove_signal_handlers(NULL);
	fuse_set_signal_handlers(NULL);
	fuse_get_session(NULL);
	fuse_is_lib_option(NULL);
	fuse_loop(NULL);
	fuse_chan_fd(NULL);
	fuse_unmount(NULL, NULL);
	fuse_destroy(NULL);
	fuse_teardown(NULL, NULL);
	fuse_invalidate(NULL, NULL);

	return (0);
}
