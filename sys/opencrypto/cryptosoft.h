/*	$FreeBSD$	*/
/*	$OpenBSD: cryptosoft.h,v 1.10 2002/04/22 23:10:09 deraadt Exp $	*/

/*-
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 * Copyright (c) 2000 Angelos D. Keromytis
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all source code copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#ifndef _CRYPTO_CRYPTOSOFT_H_
#define _CRYPTO_CRYPTOSOFT_H_

/* Software session entry */
struct swcr_data {
	int		sw_alg;		/* Algorithm */
	union {
		struct {
			u_int8_t	 *SW_ictx;
			u_int8_t	 *SW_octx;
			u_int16_t	 SW_klen;
			u_int16_t	 SW_mlen;
			struct auth_hash *SW_axf;
		} SWCR_AUTH;
		struct {
			u_int8_t	 *SW_kschedule;
			struct enc_xform *SW_exf;
		} SWCR_ENC;
		struct {
			u_int32_t	 SW_size;
			struct comp_algo *SW_cxf;
		} SWCR_COMP;
	} SWCR_UN;

#define sw_ictx		SWCR_UN.SWCR_AUTH.SW_ictx
#define sw_octx		SWCR_UN.SWCR_AUTH.SW_octx
#define sw_klen		SWCR_UN.SWCR_AUTH.SW_klen
#define sw_mlen		SWCR_UN.SWCR_AUTH.SW_mlen
#define sw_axf		SWCR_UN.SWCR_AUTH.SW_axf
#define sw_kschedule	SWCR_UN.SWCR_ENC.SW_kschedule
#define sw_exf		SWCR_UN.SWCR_ENC.SW_exf
#define sw_size		SWCR_UN.SWCR_COMP.SW_size
#define sw_cxf		SWCR_UN.SWCR_COMP.SW_cxf
};

struct swcr_session {
	struct mtx	swcr_lock;
	struct swcr_data swcr_algorithms[2];
	unsigned swcr_nalgs;
};

#ifdef _KERNEL
extern u_int8_t hmac_ipad_buffer[];
extern u_int8_t hmac_opad_buffer[];
#endif /* _KERNEL */

#endif /* _CRYPTO_CRYPTO_H_ */
