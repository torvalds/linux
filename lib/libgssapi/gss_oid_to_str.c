/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000 The Regents of the University of Michigan.
 * All rights reserved.
 *
 * Copyright (c) 2000 Dug Song <dugsong@UMICH.EDU>.
 * All rights reserved, all wrongs reversed.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* $FreeBSD$ */

#include <gssapi/gssapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "utils.h"

OM_uint32
gss_oid_to_str(OM_uint32 *minor_status, gss_OID oid, gss_buffer_t oid_str)
{
	char		numstr[128];
	unsigned long	number;
	int		numshift;
	size_t		string_length;
	size_t		i;
	unsigned char	*cp;
	char		*bp;

	*minor_status = 0;
	_gss_buffer_zero(oid_str);

	if (oid == GSS_C_NULL_OID)
		return (GSS_S_FAILURE);

	/* Decoded according to krb5/gssapi_krb5.c */

	/* First determine the size of the string */
	string_length = 0;
	number = 0;
	numshift = 0;
	cp = (unsigned char *) oid->elements;
	number = (unsigned long) cp[0];
	sprintf(numstr, "%ld ", number/40);
	string_length += strlen(numstr);
	sprintf(numstr, "%ld ", number%40);
	string_length += strlen(numstr);
	for (i=1; i<oid->length; i++) {
		if ( (size_t) (numshift+7) < (sizeof(unsigned long)*8)) {
			number = (number << 7) | (cp[i] & 0x7f);
			numshift += 7;
		}
		else {
			*minor_status = 0;
			return(GSS_S_FAILURE);
		}
		if ((cp[i] & 0x80) == 0) {
			sprintf(numstr, "%ld ", number);
			string_length += strlen(numstr);
			number = 0;
			numshift = 0;
		}
	}
	/*
	 * If we get here, we've calculated the length of "n n n ... n ".
	 * Add 4 here for "{ " and "}\0".
	 */
	string_length += 4;
	if ((bp = (char *) malloc(string_length))) {
		strcpy(bp, "{ ");
		number = (unsigned long) cp[0];
		sprintf(numstr, "%ld ", number/40);
		strcat(bp, numstr);
		sprintf(numstr, "%ld ", number%40);
		strcat(bp, numstr);
		number = 0;
		cp = (unsigned char *) oid->elements;
		for (i=1; i<oid->length; i++) {
			number = (number << 7) | (cp[i] & 0x7f);
			if ((cp[i] & 0x80) == 0) {
				sprintf(numstr, "%ld ", number);
				strcat(bp, numstr);
				number = 0;
			}
		}
		strcat(bp, "}");
		oid_str->length = strlen(bp)+1;
		oid_str->value = (void *) bp;
		*minor_status = 0;
		return(GSS_S_COMPLETE);
	}
	*minor_status = ENOMEM;
	return(GSS_S_FAILURE);
}
