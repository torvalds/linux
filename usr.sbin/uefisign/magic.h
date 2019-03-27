/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */

/*
 * This file contains Authenticode-specific ASN.1 "configuration", used,
 * after being processed by asprintf(3), as an input to ASN1_generate_nconf(3).
 */
static const char *magic_fmt =
"asn1 = SEQUENCE:SpcIndirectDataContent\n"
"\n"
"[SpcIndirectDataContent]\n"
"a = SEQUENCE:SpcAttributeTypeAndOptionalValue\n"
"b = SEQUENCE:DigestInfo\n"
"\n"
"[SpcAttributeTypeAndOptionalValue]\n"
"# SPC_PE_IMAGE_DATAOBJ\n"
"a = OID:1.3.6.1.4.1.311.2.1.15\n"
"b = SEQUENCE:SpcPeImageData\n"
"\n"
"[SpcPeImageData]\n"
"a = FORMAT:HEX,BITSTRING:00\n"
/*
 * Well, there should be some other struct here, "SPCLink", but it doesn't
 * appear to be necessary for UEFI, and I have no idea how to synthesize it,
 * as it uses the CHOICE type.
 */
"\n"
"[DigestInfo]\n"
"a = SEQUENCE:AlgorithmIdentifier\n"
/*
 * Here goes the digest computed from PE headers and sections.
 */
"b = FORMAT:HEX,OCTETSTRING:%s\n"
"\n"
"[AlgorithmIdentifier]\n"
"a = OBJECT:sha256\n"
"b = NULL\n";
