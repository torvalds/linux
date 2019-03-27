/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	__USAGE_H
#define	__USAGE_H

static const char nand_help_usage[] =
	"Usage: %s help topic=<cmd>\n"
	"\n"
	"Arguments:\n"
	"\tcmd\t- [help|read|write|erase|readoob|writeoob|info]\n"
	"\n";

static const char nand_read_usage[] =
	"Usage: %s read dev=<gnand_device> (block|page|pos)=n [count=n]\n"
	"\n"
	"Arguments:\n"
	"\tdev\t- path to gnand device node\n"
	"\tblock\t- starting block or\n"
	"\tpage\t- starting page or\n"
	"\tpos\t- starting position (in bytes, must be page-aligned)\n"
	"\tout\t- output file (hexdump to stdout if not supplied)\n"
	"\n"
	"Note that you can only specify only one of: 'block', 'page', 'pos'\n"
	"parameters at once. 'count' parameter is meaningful in terms of used\n"
	"unit (page, block or byte).\n";

static const char nand_write_usage[] =
	"Usage: %s write dev=<gnand_device> in=<file> (block|page|pos)=n [count=n]\n"
	"\n"
	"Arguments:\n"
	"\tdev\t- path to gnand device node\n"
	"\tin\t- path to input file which be writed to gnand\n"
	"\tblock\t- starting block or\n"
	"\tpage\t- starting page or\n"
	"\tpos\t- starting position (in bytes, must be page-aligned)\n"
	"\tcount\t- byte/page/block count\n"
	"\n"
	"";

static const char nand_erase_usage[] =
	"Usage: %s erase dev=<gnand_device> (block|page|pos)=n [count=n]\n"
	"\n"
	"Arguments:\n"
	"\tdev\t- path to gnand device node\n"
	"\tblock\t- starting block or\n"
	"\tpage\t- starting page or\n"
	"\tpos\t- starting position (in bytes, muse be block-aligned)\n"
	"\tcount\t- byte/page/block count\n"
	"\n"
	"NOTE: position and count for erase operation MUST be block-aligned\n";

static const char nand_read_oob_usage[] =
	"Usage: %s readoob dev=<gnand_device> page=n [out=file] [count=n]\n"
	"\n"
	"Arguments:\n"
	"\tdev\t- path to gnand device node\n"
	"\tpage\t- page (page) number\n"
	"\tout\t- outut file (hexdump to stdout if not supplied)\n"
	"\tcount\t- page count (default is 1)\n"
	"\n"
	"If you supply count parameter with value other than 1, data will be\n"
	"read from subsequent page's OOB areas\n";

static const char nand_write_oob_usage[] =
	"Usage: %s writeoob dev=<gnand_device> in=<file> page=n [count=n]\n"
	"\n"
	"\tdev\t- path to gnand device node\n"
	"\tin\t- path to file containing data which will be written\n"
	"\tpage\t- page (page) number\n"
	"\n"
	"If you supply count parameter with value other than 1, data will be\n"
	"written to subsequent page's OOB areas\n";

static const char nand_info_usage[] =
	"Usage: %s info dev=<gnand_device>\n"
	"\n"
	"Arguments:\n"
	"\tdev\t- path to gnand device node\n";

static const char nand_stats_usage[] =
	"Usage: %s stats dev=<gnand_device> (page|block)=<n>\n"
	"\n"
	"Arguments:\n"
	"\tdev\t- path to gnand device node\n";

#endif	/* __USAGE_H */
