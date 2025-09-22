/*	$OpenBSD: tokendb.h,v 1.6 2022/12/26 20:06:43 jmc Exp $	*/

/*-
 * Copyright (c) 1995 Migration Associates Corp. All Rights Reserved
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
 *    must display the following acknowledgement:
 *      This product includes software developed by Berkeley Software Design,
 *      Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: tokendb.h,v 1.1 1996/08/26 20:13:11 prb Exp $
 */

/*
 * Structure defining a record for a user.  All fields 
 * stored in ascii to facilitate backup/reconstruction.
 * A null byte is required after the share secret field.
 */

typedef	struct	{
	char	uname[LOGIN_NAME_MAX];	/* user login name	*/
	char	secret[16];		/* token shared secret	*/
	unsigned flags;			/* record flags		*/
	unsigned mode;			/* token mode flags	*/
	time_t	lock_time;		/* time of record lock	*/
	u_char	rim[8];			/* reduced input mode 	*/
	char	reserved_char1[8];
	char	reserved_char2[80];
} TOKENDB_Rec;

/*
 * Record flag values
 */

#define	TOKEN_LOCKED	0x1		/* record locked for updating	*/
#define	TOKEN_ENABLED	0x2		/* user login method enabled	*/
#define	TOKEN_LOGIN	0x4		/* login in progress lock	*/
#define	TOKEN_USEMODES	0x8		/* use the mode field		*/

#define	TOKEN_DECMODE	0x1		/* allow decimal results */
#define	TOKEN_HEXMODE	0x2		/* allow hex results */
#define	TOKEN_PHONEMODE	0x4		/* allow phone book results */
#define	TOKEN_RIM	0x8		/* reduced input mode */

#define	TOKEN_GROUP	"_token"	/* group that owns token database */

/*
 * Function prototypes for routines which manipulate the 
 * database for the token.  These routines have no knowledge
 * of DES or encryption.  However, they will manipulate the
 * flags field of the database record with complete abandon.
 */

extern	int	tokendb_delrec(char *);
extern	int	tokendb_getrec(char *, TOKENDB_Rec *);
extern	int	tokendb_putrec(char *, TOKENDB_Rec *);
extern	int	tokendb_firstrec(int, TOKENDB_Rec *);
extern	int	tokendb_nextrec(int, TOKENDB_Rec *);
extern	int	tokendb_lockrec(char *, TOKENDB_Rec *, unsigned);
