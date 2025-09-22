/*	$OpenBSD: token.c,v 1.19 2015/10/05 17:31:17 millert Exp $	*/

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
 *	BSDI $From: token.c,v 1.2 1996/08/28 22:07:55 prb Exp $
 */

/*
 * DES functions for one-way encrypting Authentication Tokens.
 * All knowledge of DES is confined to this file.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <ctype.h>
#include <stdio.h>
#include <syslog.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/des.h>

#include "token.h"
#include "tokendb.h"

/*
 * Define a union of various types of arguments to DES functions.
 * All native DES types are modulo 8 bytes in length. Cipher text
 * needs a trailing null byte.
 */

typedef	union {
	DES_cblock	cb;
	char		ct[9];
	uint32_t	ul[2];
} TOKEN_CBlock;

/*
 * Static definition of random number challenge for token.
 * Challenge length is 8 bytes, left-justified with trailing null byte.
 */

static	TOKEN_CBlock tokennumber;

/*
 * Static function prototypes
 */

static	void	tokenseed(TOKEN_CBlock *);
static	void	lcase(char *);
static	void	h2d(char *);
static	void	h2cb(char *, TOKEN_CBlock *);
static	void	cb2h(TOKEN_CBlock, char *);

/*
 * Generate random DES cipherblock seed. Feedback key into
 * new_random_key to strengthen.
 */

static void
tokenseed(TOKEN_CBlock *cb)
{
	cb->ul[0] = arc4random();
	cb->ul[1] = arc4random();
}

/*
 * Send a random challenge string to the token. The challenge
 * is always base 10 as there are no alpha keys on the keyboard.
 */

void
tokenchallenge(char *user, char *challenge, int size, char *card_type)
{
	TOKENDB_Rec tr;
	TOKEN_CBlock cb;
	DES_key_schedule ks;
	int r, c;

	r = 1;	/* no reduced input mode by default! */

	if ((tt->modes & TOKEN_RIM) &&
	    tokendb_getrec(user, &tr) == 0 &&
	    (tr.mode & TOKEN_RIM)) {
		c = 0;
		while ((r = tokendb_lockrec(user, &tr, TOKEN_LOCKED)) == 1) {
			if (c++ >= 60)
				break;
			sleep(1);
		}
		tr.flags &= ~TOKEN_LOCKED;
		if (r == 0 && tr.rim[0]) {
			h2cb(tr.secret, &cb);
			DES_fixup_key_parity(&cb.cb);
			DES_key_sched(&cb.cb, &ks);
			DES_ecb_encrypt(&tr.rim, &cb.cb, &ks, DES_ENCRYPT);
			memcpy(tr.rim, cb.cb, 8);
			for (r = 0; r < 8; ++r) {
				if ((tr.rim[r] &= 0xf) > 9)
					tr.rim[r] -= 10;
				tr.rim[r] |= 0x30;
			}
			r = 0;		/* reset it back */
			memcpy(tokennumber.ct, tr.rim, 8);
			tokennumber.ct[8] = 0;
			tokendb_putrec(user, &tr);
		}
	}
	if (r != 0 || tr.rim[0] == '\0') {
		memset(tokennumber.ct, 0, sizeof(tokennumber.ct));
		snprintf(tokennumber.ct, sizeof(tokennumber.ct), "%8.8u",
		    arc4random());
		if (r == 0) {
			memcpy(tr.rim, tokennumber.ct, 8);
			tokendb_putrec(user, &tr);
		}
	}

	snprintf(challenge, size, "%s Challenge \"%s\"\r\n%s Response: ",
	    card_type, tokennumber.ct, card_type);
}

/*
 * Verify response from user against token's predicted cipher
 * of the random number challenge.
 */

int
tokenverify(char *username, char *challenge, char *response)
{
	char	*state;
	TOKENDB_Rec tokenrec;
	TOKEN_CBlock tmp;
	TOKEN_CBlock cmp_text;
	TOKEN_CBlock user_seed;
	TOKEN_CBlock cipher_text;
	DES_key_schedule key_schedule;


	memset(cmp_text.ct, 0, sizeof(cmp_text.ct));
	memset(user_seed.ct, 0, sizeof(user_seed.ct));
	memset(cipher_text.ct, 0, sizeof(cipher_text.ct));
	memset(tokennumber.ct, 0, sizeof(tokennumber.ct));

	(void)strtok(challenge, "\"");
	state = strtok(NULL, "\"");
	tmp.ul[0] = strtoul(state, NULL, 10);
	snprintf(tokennumber.ct, sizeof(tokennumber.ct), "%8.8u",tmp.ul[0]);

	/*
	 * Retrieve the db record for the user. Nuke it as soon as
	 * we have translated out the user's shared secret just in
	 * case we (somehow) get core dumped...
	 */

	if (tokendb_getrec(username, &tokenrec))
		return (-1);

	h2cb(tokenrec.secret, &user_seed);
	explicit_bzero(&tokenrec.secret, sizeof(tokenrec.secret));

	if (!(tokenrec.flags & TOKEN_ENABLED))
		return (-1);

	/*
	 * Compute the anticipated response in hex. Nuke the user's
	 * shared secret asap.
	 */

	DES_fixup_key_parity(&user_seed.cb);
	DES_key_sched(&user_seed.cb, &key_schedule);
	explicit_bzero(user_seed.ct, sizeof(user_seed.ct));
	DES_ecb_encrypt(&tokennumber.cb, &cipher_text.cb, &key_schedule,
	    DES_ENCRYPT);
	explicit_bzero(&key_schedule, sizeof(key_schedule));

	/*
	 * The token thinks it's descended from VAXen.  Deal with i386
	 * endian-ness of binary cipher prior to generating ascii from first
	 * 32 bits.
	 */

	HTONL(cipher_text.ul[0]);
	snprintf(cmp_text.ct, sizeof(cmp_text.ct), "%8.8x", cipher_text.ul[0]);

	if (tokenrec.mode & TOKEN_PHONEMODE) {
		/*
		 * If we are a CRYPTOCard, we need to see if we are in
		 * "telephone number mode".  If so, transmogrify the fourth
		 * digit of the cipher.  Lower case response just in case
		 * it's * hex.  Compare hex cipher with anticipated response
		 * from token.
		 */

		lcase(response);

		if (response[3] == '-')
			cmp_text.ct[3] = '-';
	}

	if ((tokenrec.mode & TOKEN_HEXMODE) && !strcmp(response, cmp_text.ct))
		return (0);

	/*
	 * No match against the computed hex cipher.  The token could be
	 * in decimal mode.  Pervert the string to magic decimal equivalent.
	 */

	h2d(cmp_text.ct);

	if ((tokenrec.mode & TOKEN_DECMODE) && !strcmp(response, cmp_text.ct))
		return (0);

	return (-1);
}

/*
 * Initialize a new user record in the token database.
 */

int
tokenuserinit(int flags, char *username, unsigned char *usecret, unsigned mode)
{
	TOKENDB_Rec tokenrec;
	TOKEN_CBlock secret;
	TOKEN_CBlock nulls;
	TOKEN_CBlock checksum;
	TOKEN_CBlock checktxt;
	DES_key_schedule key_schedule;

	memset(&secret, 0, sizeof(secret));

	/*
	 * If no user secret passed in, create one
	 */

	if ( (flags & TOKEN_GENSECRET) )
		tokenseed(&secret);
	else
		memcpy(&secret, usecret, sizeof(DES_cblock));

	DES_fixup_key_parity(&secret.cb);

	/*
	 * Check if the db record already exists.  If there's no
	 * force-init flag and it exists, go away. Else,
	 * create the user's db record and put to the db.
	 */


	if (!(flags & TOKEN_FORCEINIT) &&
	    tokendb_getrec(username, &tokenrec) == 0)
		return (1);

	memset(&tokenrec, 0, sizeof(tokenrec));
	strlcpy(tokenrec.uname, username, sizeof(tokenrec.uname));
	cb2h(secret, tokenrec.secret);
	tokenrec.mode = 0;
	tokenrec.flags = TOKEN_ENABLED | TOKEN_USEMODES;
	tokenrec.mode = mode;
	memset(tokenrec.reserved_char1, 0, sizeof(tokenrec.reserved_char1));
	memset(tokenrec.reserved_char2, 0, sizeof(tokenrec.reserved_char2));

	if (tokendb_putrec(username, &tokenrec))
		return (-1);

	/*
	 * Check if the shared secret was generated here. If so, we
	 * need to inform the user about it in order that it can be
	 * programmed into the token. See tokenverify() (above) for
	 * discussion of cipher generation.
	 */

	if (!(flags & TOKEN_GENSECRET)) {
		explicit_bzero(&secret, sizeof(secret));
		return (0);
	}

	printf("Shared secret for %s\'s token: "
	    "%03o %03o %03o %03o %03o %03o %03o %03o\n",
	    username, secret.cb[0], secret.cb[1], secret.cb[2], secret.cb[3],
	    secret.cb[4], secret.cb[5], secret.cb[6], secret.cb[7]);

	DES_key_sched(&secret.cb, &key_schedule);
	explicit_bzero(&secret, sizeof(secret));
	memset(&nulls, 0, sizeof(nulls));
	DES_ecb_encrypt(&nulls.cb, &checksum.cb, &key_schedule, DES_ENCRYPT);
	explicit_bzero(&key_schedule, sizeof(key_schedule));
	HTONL(checksum.ul[0]);
	snprintf(checktxt.ct, sizeof(checktxt.ct), "%8.8x", checksum.ul[0]);
	printf("Hex Checksum: \"%s\"", checktxt.ct);

	h2d(checktxt.ct);
	printf("\tDecimal Checksum: \"%s\"\n", checktxt.ct);

	return (0);
}

/*
 * Magically transform a hex character string into a decimal character
 * string as defined by the token card vendor. The string should have
 * been lowercased by now.
 */

static	void
h2d(char *cp)
{
	int	i;

	for (i=0; i<sizeof(DES_cblock); i++, cp++) {
		if (*cp >= 'a' && *cp <= 'f')
			*cp = tt->map[*cp - 'a'];
	}
}

/*
 * Translate an hex 16 byte ascii representation of an unsigned
 * integer to a DES_cblock.
 */

static	void
h2cb(char *hp, TOKEN_CBlock *cb)
{
	char	scratch[9];

	strlcpy(scratch, hp, sizeof(scratch));
	cb->ul[0] = strtoul(scratch, NULL, 16);

	strlcpy(scratch, hp + 8, sizeof(scratch));
	cb->ul[1] = strtoul(scratch, NULL, 16);
}

/*
 * Translate a DES_cblock to an 16 byte ascii hex representation.
 */

static	void
cb2h(TOKEN_CBlock cb, char* hp)
{
	char	scratch[17];

	snprintf(scratch,   9, "%8.8x", cb.ul[0]);
	snprintf(scratch+8, 9, "%8.8x", cb.ul[1]);
	memcpy(hp, scratch, 16);
}

/*
 * Lowercase possible hex response
 */

static	void
lcase(char *cp)
{
	while (*cp) {
		if (isupper((unsigned char)*cp))
			*cp = tolower((unsigned char)*cp);
		cp++;
	}
}
