/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994-1996 Søren Schmidt
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

/*
 * Linux syscalls return negative errno's, we do positive and map them
 * Reference:
 *   FreeBSD: src/sys/sys/errno.h
 *   Linux:   include/uapi/asm-generic/errno-base.h
 *            include/uapi/asm-generic/errno.h
 */
const int linux_errtbl[ELAST + 1] = {
	-0,
	-1,
	-2,
	-3,
	-4,
	-5,
	-6,
	-7,
	-8,
	-9,

	-10,
	-35,	/* EDEADLK */
	-12,
	-13,
	-14,
	-15,
	-16,
	-17,
	-18,
	-19,

	-20,
	-21,
	-22,
	-23,
	-24,
	-25,
	-26,
	-27,
	-28,
	-29,

	-30,
	-31,
	-32,
	-33,
	-34,
	-11,	/* EAGAIN */
	-115,
	-114,
	-88,
	-89,

	-90,
	-91,
	-92,
	-93,
	-94,
	-95,
	-96,
	-97,
	-98,
	-99,

	-100,
	-101,
	-102,
	-103,
	-104,
	-105,
	-106,
	-107,
	-108,
	-109,

	-110,
	-111,
	-40,
	-36,
	-112,
	-113,
	-39,
	-11,
	-87,
	-122,

	-116,
	-66,
	-6,	/* EBADRPC -> ENXIO */
	-6,	/* ERPCMISMATCH -> ENXIO */
	-6,	/* EPROGUNAVAIL -> ENXIO */
	-6,	/* EPROGMISMATCH -> ENXIO */
	-6,	/* EPROCUNAVAIL -> ENXIO */
	-37,
	-38,
	-9,

	-6,	/* EAUTH -> ENXIO */
	-6,	/* ENEEDAUTH -> ENXIO */
	-43,
	-42,
	-75,
	-125,
	-84,
	-61,
	-22,	/* EDOOFUS -> EINVAL */
	-74,

	-72,
	-67,
	-71,
	-1,	/* ENOTCAPABLE -> EPERM */
	-1,	/* ECAPMODE -> EPERM */
	-131,	/* ENOTRECOVERABLE */
	-130,	/* EOWNERDEAD */
	-22,	/* EINTEGRITY -> EINVAL */
};

_Static_assert(ELAST == 97,
    "missing errno entries in linux_errtbl");
