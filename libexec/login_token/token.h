/*	$OpenBSD: token.h,v 1.5 2020/09/06 17:08:29 mortimer Exp $	*/

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
 *	BSDI $From: token.h,v 1.1 1996/08/26 20:13:10 prb Exp $
 */

/*
 * Operations accepted by the token admin commands
 */

#define	TOKEN_DISABLE	0x1	/* disable user account		*/
#define	TOKEN_ENABLE	0x2	/* enable user account		*/
#define	TOKEN_INITUSER	0x4	/* add/init user account	*/
#define	TOKEN_RMUSER	0x8	/* remove user account		*/
#define	TOKEN_UNLOCK	0x10	/* force unlock db record	*/

/*
 * Flags for options to modify TOKEN_INITUSER
 */

#define	TOKEN_FORCEINIT	0x100	/* reinit existing account	*/
#define	TOKEN_GENSECRET	0x200	/* gen shared secret for token	*/

/*
 * Structure describing different token cards
 */
struct token_types {
	char	*name;		/* name of card */
	char	*proper;	/* proper name of card */
	char	*db;		/* path to database */
	char	map[6];		/* how A-F map to decimal */
	int	options;	/* various available options */
	u_int	modes;		/* available modes */
	u_int	defmode;	/* default mode (if none specified) */
};

extern struct token_types *tt;		/* what type we are running as now */

/*
 * Options
 */
#define	TOKEN_PHONE	0x0001	/* Allow phone number representation */
#define	TOKEN_HEXINIT	0x0002	/* Allow initialization in hex (and octal) */

/*
 * Function prototypes for commands involving intimate DES knowledge
 */

extern	void	tokenchallenge(char *, char *, int, char *);
extern	int	tokenverify(char *, char *, char *);
extern	int	tokenuserinit(int, char *, u_char *, u_int);
extern	int	token_init(char *);
extern	u_int	token_mode(char *);
extern	char *	token_getmode(u_int);
