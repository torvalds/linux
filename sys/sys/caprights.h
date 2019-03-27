/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef _SYS_CAPRIGHTS_H_
#define	_SYS_CAPRIGHTS_H_

/*
 * The top two bits in the first element of the cr_rights[] array contain
 * total number of elements in the array - 2. This means if those two bits are
 * equal to 0, we have 2 array elements.
 * The top two bits in all remaining array elements should be 0.
 * The next five bits contain array index. Only one bit is used and bit position
 * in this five-bits range defines array index. This means there can be at most
 * five array elements.
 */
#define	CAP_RIGHTS_VERSION_00	0
/*
#define	CAP_RIGHTS_VERSION_01	1
#define	CAP_RIGHTS_VERSION_02	2
#define	CAP_RIGHTS_VERSION_03	3
*/
#define	CAP_RIGHTS_VERSION	CAP_RIGHTS_VERSION_00

struct cap_rights {
	uint64_t	cr_rights[CAP_RIGHTS_VERSION + 2];
};

#ifndef	_CAP_RIGHTS_T_DECLARED
#define	_CAP_RIGHTS_T_DECLARED
typedef	struct cap_rights	cap_rights_t;
#endif

#endif /* !_SYS_CAPRIGHTS_H_ */
