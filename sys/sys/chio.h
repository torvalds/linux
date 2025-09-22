/*	$OpenBSD: chio.h,v 1.9 2025/03/31 08:39:38 jsg Exp $	*/
/*	$NetBSD: chio.h,v 1.8 1996/04/03 00:25:21 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Jason R. Thorpe <thorpej@and.com>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by Jason R. Thorpe
 *	for And Communications, http://www.and.com/
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_CHIO_H_
#define _SYS_CHIO_H_

/*
 * Element types.  Used as "to" and "from" type indicators in move
 * and exchange operations.
 *
 * Note that code in sys/scsi/ch.c relies on these values (uses them
 * as offsets in an array, and other evil), so don't muck with them
 * unless you know what you're doing.
 */
#define CHET_MT		0	/* medium transport (picker) */
#define CHET_ST		1	/* storage transport (slot) */
#define CHET_IE		2	/* import/export (portal) */
#define CHET_DT		3	/* data transfer (drive) */

/*
 * Maximum length of a volume identification string
 */                                               
#define CH_VOLTAG_MAXLEN 32

/*
 * Structure used to execute a MOVE MEDIUM command.
 */
struct changer_move {
	int	cm_fromtype;	/* element type to move from */
	int	cm_fromunit;	/* logical unit of from element */
	int	cm_totype;	/* element type to move to */
	int	cm_tounit;	/* logical unit of to element */
	int	cm_flags;	/* misc. flags */
};

/* cm_flags */
#define CM_INVERT	0x01	/* invert media */

/*
 * Structure used to execute an EXCHANGE MEDIUM command.  In an
 * exchange operation, the following steps occur:
 *
 *	- media from source is moved to first destination.
 *
 *	- media previously occupying first destination is moved
 *	  to the second destination.
 *
 * The second destination may or may not be the same as the source.
 * In the case of a simple exchange, the source and second destination
 * are the same.
 */
struct changer_exchange {
	int	ce_srctype;	/* element type of source */
	int	ce_srcunit;	/* logical unit of source */
	int	ce_fdsttype;	/* element type of first destination */
	int	ce_fdstunit;	/* logical unit of first destination */
	int	ce_sdsttype;	/* element type of second destination */
	int	ce_sdstunit;	/* logical unit of second destination */
	int	ce_flags;	/* misc. flags */
};

/* ce_flags */
#define CE_INVERT1	0x01	/* invert media 1 */
#define CE_INVERT2	0x02	/* invert media 2 */

/*
 * Structure used to execute a POSITION TO ELEMENT command.  This
 * moves the current picker in front of the specified element.
 */
struct changer_position {
	int	cp_type;	/* element type */
	int	cp_unit;	/* logical unit of element */
	int	cp_flags;	/* misc. flags */
};

/* cp_flags */
#define CP_INVERT	0x01	/* invert picker */

/*
 * Data returned by CHIOGPARAMS.
 */
struct changer_params {
	int	cp_curpicker;	/* current picker */
	int	cp_npickers;	/* number of pickers */
	int	cp_nslots;	/* number of slots */
	int	cp_nportals;	/* number of import/export portals */
	int	cp_ndrives;	/* number of drives */
};

struct changer_voltag {
	u_char		cv_volid[CH_VOLTAG_MAXLEN + 1];
	u_int16_t	cv_serial;
};

struct changer_element_status {
	int	 		ces_type;		/* element type */
	u_int8_t 		ces_flags;		/* flags */
	u_int16_t		ces_addr;		/* logical element address */
	u_int8_t		ces_sensecode;	  	/* additional sense code for element */
	u_int8_t		ces_sensequal;	  	/* additional sense code qualifier */
	u_int8_t		ces_source_type;  	/*  element type of source address */
	u_int16_t		ces_source_addr;	/*  source address of medium */
  	struct changer_voltag	ces_pvoltag;		/* primary voltag */
	struct changer_voltag	ces_avoltag;		/* alternate voltag */
};

/*
 * Command used to get element status.
 */
struct changer_element_status_request {
	int				cesr_type;  /* element type */
	int				cesr_flags;
#define CESR_VOLTAGS 0x01	

	struct changer_element_status	*cesr_data; /* pre-allocated data storage */
};

/*
 * Data returned by CHIOGSTATUS is an array of flags bytes.
 * Not all flags have meaning for all element types.
 */
#define CESTATUS_FULL		0x01	/* element is full */
#define CESTATUS_IMPEXP		0x02	/* media deposited by operator */
#define CESTATUS_EXCEPT		0x04	/* element in abnormal state */
#define CESTATUS_ACCESS		0x08	/* media accessible by picker */
#define CESTATUS_EXENAB		0x10	/* element supports exporting */
#define CESTATUS_INENAB		0x20	/* element supports importing */

#define CESTATUS_PICKER_MASK	0x05	/* flags valid for pickers */
#define CESTATUS_SLOT_MASK	0x0c	/* flags valid for slots */
#define CESTATUS_PORTAL_MASK	0x3f	/* flags valid for portals */
#define CESTATUS_DRIVE_MASK	0x0c	/* flags valid for drives */

#define CESTATUS_BITS	\
	"\20\6INEAB\5EXENAB\4ACCESS\3EXCEPT\2IMPEXP\1FULL"

#define CHIOMOVE	_IOW('c', 0x41, struct changer_move)
#define CHIOEXCHANGE	_IOW('c', 0x42, struct changer_exchange)
#define CHIOPOSITION	_IOW('c', 0x43, struct changer_position)
#define CHIOGPICKER	_IOR('c', 0x44, int)
#define CHIOSPICKER	_IOW('c', 0x45, int)
#define CHIOGPARAMS	_IOR('c', 0x46, struct changer_params)
#define CHIOGSTATUS	_IOW('c', 0x48, struct changer_element_status_request)

#endif /* _SYS_CHIO_H_ */
