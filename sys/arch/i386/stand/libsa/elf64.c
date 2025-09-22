/*	$OpenBSD: elf64.c,v 1.1 2007/05/30 01:25:43 tom Exp $	*/

/*
 * Copyright (c) 2007 Tom Cosgrove <tom@openbsd.org>
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

#include <lib/libkern/libkern.h>
#include <lib/libsa/stand.h>

#include <sys/param.h>
#include <sys/exec.h>

#include "../../../../lib/libsa/loadfile.h"

#undef ELFSIZE
#define ELFSIZE  64

#include <sys/exec_elf.h>

#include "../../../../lib/libsa/loadfile_elf.c"
