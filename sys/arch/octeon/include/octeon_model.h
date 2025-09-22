/*	$OpenBSD: octeon_model.h,v 1.9 2024/06/26 01:40:49 jsg Exp $	*/

/*
 * Copyright (c) 2007
 *      Internet Initiative Japan, Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MIPS_OCTEON_MODEL_H_
#define _MIPS_OCTEON_MODEL_H_

#define OCTEON_MODEL_CN38XX_REV1	0x000d0000
#define OCTEON_MODEL_CN38XX_REV2	0x000d0001
#define OCTEON_MODEL_CN38XX_REV3	0x000d0003
#define OCTEON_MODEL_CN3100             0x000d0100
#define OCTEON_MODEL_CN3020             0x000d0110
#define OCTEON_MODEL_CN3010             0x000d0200
#define OCTEON_MODEL_CN3005             0x000d0210
#define OCTEON_MODEL_CN5010		0x000d0600
#define OCTEON_MODEL_CN5010_PASS1_1	0x000d0601
#define OCTEON_MODEL_CN61XX_PASS1_0	0x000d9300
#define OCTEON_MODEL_CN61XX_PASS1_1	0x000d9301

#define OCTEON_MODEL_MASK		0x00ffff10
#define OCTEON_MODEL_REV_MASK		0x00ffff1f
#define OCTEON_MODEL_FAMILY_MASK	0x00ffff00
#define OCTEON_MODEL_FAMILY_REV_MASK    0x00ffff0f

#define OCTEON_MODEL_FAMILY_CN58XX	0x000d0300
#define OCTEON_MODEL_FAMILY_CN56XX	0x000d0400
#define OCTEON_MODEL_FAMILY_CN38XX	0x000d0000
#define OCTEON_MODEL_FAMILY_CN31XX	0x000d0100
#define OCTEON_MODEL_FAMILY_CN30XX	0x000d0200
#define OCTEON_MODEL_FAMILY_CN50XX	0x000d0600
#define OCTEON_MODEL_FAMILY_CN63XX	0x000d9000
#define OCTEON_MODEL_FAMILY_CN68XX	0x000d9100
#define OCTEON_MODEL_FAMILY_CN66XX	0x000d9200
#define OCTEON_MODEL_FAMILY_CN61XX	0x000d9300
#define OCTEON_MODEL_FAMILY_CN78XX	0x000d9500
#define OCTEON_MODEL_FAMILY_CN71XX	0x000d9600
#define OCTEON_MODEL_FAMILY_CN73XX	0x000d9700

/*
 *  get chip id
 */
static inline uint32_t
octeon_get_chipid(void)
{
        uint32_t tmp;

        asm volatile (
            "    .set push              \n"
            "    .set mips64            \n"
            "    .set noreorder         \n"
            "    mfc0   %0, $15, 0      \n"
            "    .set pop               \n"
            : "=&r"(tmp) : );

        return(tmp);
}

#define octeon_model(id) ((id) & OCTEON_MODEL_MASK)
#define octeon_model_revision(id) ((id) & OCTEON_MODEL_REV_MASK)
#define octeon_model_family(id) ((id) & OCTEON_MODEL_FAMILY_MASK)
#define octeon_model_family_revision(id) ((id) & OCTEON_MODEL_FAMILY_REV_MASK)

#endif
