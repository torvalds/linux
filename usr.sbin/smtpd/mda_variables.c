/*	$OpenBSD: mda_variables.c,v 1.10 2024/04/23 13:34:51 jsg Exp $	*/

/*
 * Copyright (c) 2011-2017 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

#define	EXPAND_DEPTH	10

ssize_t mda_expand_format(char *, size_t, const struct deliver *,
    const struct userinfo *, const char *);
static ssize_t mda_expand_token(char *, size_t, const char *,
    const struct deliver *, const struct userinfo *, const char *);
static int mod_lowercase(char *, size_t);
static int mod_uppercase(char *, size_t);
static int mod_strip(char *, size_t);

static struct modifiers {
	char	*name;
	int	(*f)(char *buf, size_t len);
} token_modifiers[] = {
	{ "lowercase",	mod_lowercase },
	{ "uppercase",	mod_uppercase },
	{ "strip",	mod_strip },
	{ "raw",	NULL },		/* special case, must stay last */
};

#define	MAXTOKENLEN	128

static ssize_t
mda_expand_token(char *dest, size_t len, const char *token,
    const struct deliver *dlv, const struct userinfo *ui, const char *mda_command)
{
	char		rtoken[MAXTOKENLEN];
	char		tmp[EXPAND_BUFFER];
	const char     *string = NULL;
	char	       *lbracket, *rbracket, *content, *sep, *mods;
	ssize_t		i;
	ssize_t		begoff, endoff;
	const char     *errstr = NULL;
	int		replace = 1;
	int		raw = 0;

	begoff = 0;
	endoff = EXPAND_BUFFER;
	mods = NULL;

	if (strlcpy(rtoken, token, sizeof rtoken) >= sizeof rtoken)
		return -1;

	/* token[x[:y]] -> extracts optional x and y, converts into offsets */
	if ((lbracket = strchr(rtoken, '[')) &&
	    (rbracket = strchr(rtoken, ']'))) {
		/* ] before [ ... or empty */
		if (rbracket < lbracket || rbracket - lbracket <= 1)
			return -1;

		*lbracket = *rbracket = '\0';
		content  = lbracket + 1;

		if ((sep = strchr(content, ':')) == NULL)
			endoff = begoff = strtonum(content, -EXPAND_BUFFER,
			    EXPAND_BUFFER, &errstr);
		else {
			*sep = '\0';
			if (content != sep)
				begoff = strtonum(content, -EXPAND_BUFFER,
				    EXPAND_BUFFER, &errstr);
			if (*(++sep)) {
				if (errstr == NULL)
					endoff = strtonum(sep, -EXPAND_BUFFER,
					    EXPAND_BUFFER, &errstr);
			}
		}
		if (errstr)
			return -1;

		/* token:mod_1,mod_2,mod_n -> extract modifiers */
		mods = strchr(rbracket + 1, ':');
	} else {
		if ((mods = strchr(rtoken, ':')) != NULL)
			*mods++ = '\0';
	}

	/* token -> expanded token */
	if (!strcasecmp("sender", rtoken)) {
		if (snprintf(tmp, sizeof tmp, "%s@%s",
			dlv->sender.user, dlv->sender.domain) >= (int)sizeof tmp)
			return -1;
		if (strcmp(tmp, "@") == 0)
			(void)strlcpy(tmp, "", sizeof tmp);
		string = tmp;
	}
	else if (!strcasecmp("rcpt", rtoken)) {
		if (snprintf(tmp, sizeof tmp, "%s@%s",
			dlv->rcpt.user, dlv->rcpt.domain) >= (int)sizeof tmp)
			return -1;
		if (strcmp(tmp, "@") == 0)
			(void)strlcpy(tmp, "", sizeof tmp);
		string = tmp;
	}
	else if (!strcasecmp("dest", rtoken)) {
		if (snprintf(tmp, sizeof tmp, "%s@%s",
			dlv->dest.user, dlv->dest.domain) >= (int)sizeof tmp)
			return -1;
		if (strcmp(tmp, "@") == 0)
			(void)strlcpy(tmp, "", sizeof tmp);
		string = tmp;
	}
	else if (!strcasecmp("sender.user", rtoken))
		string = dlv->sender.user;
	else if (!strcasecmp("sender.domain", rtoken))
		string = dlv->sender.domain;
	else if (!strcasecmp("user.username", rtoken))
		string = ui->username;
	else if (!strcasecmp("user.directory", rtoken)) {
		string = ui->directory;
		replace = 0;
	}
	else if (!strcasecmp("rcpt.user", rtoken))
		string = dlv->rcpt.user;
	else if (!strcasecmp("rcpt.domain", rtoken))
		string = dlv->rcpt.domain;
	else if (!strcasecmp("dest.user", rtoken))
		string = dlv->dest.user;
	else if (!strcasecmp("dest.domain", rtoken))
		string = dlv->dest.domain;
	else if (!strcasecmp("mda", rtoken)) {
		string = mda_command;
		replace = 0;
	}
	else if (!strcasecmp("mbox.from", rtoken)) {
		if (snprintf(tmp, sizeof tmp, "%s@%s",
			dlv->sender.user, dlv->sender.domain) >= (int)sizeof tmp)
			return -1;
		if (strcmp(tmp, "@") == 0)
			(void)strlcpy(tmp, "MAILER-DAEMON", sizeof tmp);
		string = tmp;
	}
	else
		return -1;

	if (string != tmp) {
		if (string == NULL)
			return -1;
		if (strlcpy(tmp, string, sizeof tmp) >= sizeof tmp)
			return -1;
		string = tmp;
	}

	/*  apply modifiers */
	if (mods != NULL) {
		do {
			if ((sep = strchr(mods, '|')) != NULL)
				*sep++ = '\0';
			for (i = 0; (size_t)i < nitems(token_modifiers); ++i) {
				if (!strcasecmp(token_modifiers[i].name, mods)) {
					if (token_modifiers[i].f == NULL) {
						raw = 1;
						break;
					}
					if (!token_modifiers[i].f(tmp, sizeof tmp))
						return -1; /* modifier error */
					break;
				}
			}
			if ((size_t)i == nitems(token_modifiers))
				return -1; /* modifier not found */
		} while ((mods = sep) != NULL);
	}

	if (!raw && replace)
		for (i = 0; (size_t)i < strlen(tmp); ++i)
			if (strchr(MAILADDR_ESCAPE, tmp[i]))
				tmp[i] = ':';

	/* expanded string is empty */
	i = strlen(string);
	if (i == 0)
		return 0;

	/* begin offset beyond end of string */
	if (begoff >= i)
		return -1;

	/* end offset beyond end of string, make it end of string */
	if (endoff >= i)
		endoff = i - 1;

	/* negative begin offset, make it relative to end of string */
	if (begoff < 0)
		begoff += i;
	/* negative end offset, make it relative to end of string,
	 * note that end offset is inclusive.
	 */
	if (endoff < 0)
		endoff += i - 1;

	/* check that final offsets are valid */
	if (begoff < 0 || endoff < 0 || endoff < begoff)
		return -1;
	endoff += 1; /* end offset is inclusive */

	/* check that substring does not exceed destination buffer length */
	i = endoff - begoff;
	if ((size_t)i + 1 >= len)
		return -1;

	string += begoff;
	for (; i; i--) {
		*dest = *string;
		dest++;
		string++;
	}

	return endoff - begoff;
}


ssize_t
mda_expand_format(char *buf, size_t len, const struct deliver *dlv,
    const struct userinfo *ui, const char *mda_command)
{
	char		tmpbuf[EXPAND_BUFFER], *ptmp, *pbuf, *ebuf;
	char		exptok[EXPAND_BUFFER];
	ssize_t		exptoklen;
	char		token[MAXTOKENLEN];
	size_t		ret, tmpret, toklen;

	if (len < sizeof tmpbuf) {
		log_warnx("mda_expand_format: tmp buffer < rule buffer");
		return -1;
	}

	memset(tmpbuf, 0, sizeof tmpbuf);
	pbuf = buf;
	ptmp = tmpbuf;
	ret = tmpret = 0;

	/* special case: ~/ only allowed expanded at the beginning */
	if (strncmp(pbuf, "~/", 2) == 0) {
		tmpret = snprintf(ptmp, sizeof tmpbuf, "%s/", ui->directory);
		if (tmpret >= sizeof tmpbuf) {
			log_warnx("warn: user directory for %s too large",
			    ui->directory);
			return 0;
		}
		ret  += tmpret;
		ptmp += tmpret;
		pbuf += 2;
	}

	/* expansion loop */
	for (; *pbuf && ret < sizeof tmpbuf; ret += tmpret) {
		if (*pbuf == '%' && *(pbuf + 1) == '%') {
			*ptmp++ = *pbuf++;
			pbuf  += 1;
			tmpret = 1;
			continue;
		}

		if (*pbuf != '%' || *(pbuf + 1) != '{') {
			*ptmp++ = *pbuf++;
			tmpret = 1;
			continue;
		}

		/* %{...} otherwise fail */
		if ((ebuf = strchr(pbuf+2, '}')) == NULL)
			return 0;

		/* extract token from %{token} */
		toklen = ebuf - (pbuf+2);
		if (toklen >= sizeof token)
			return 0;

		memcpy(token, pbuf+2, toklen);
		token[toklen] = '\0';

		exptoklen = mda_expand_token(exptok, sizeof exptok, token, dlv,
		    ui, mda_command);
		if (exptoklen == -1)
			return -1;

		/* writing expanded token at ptmp will overflow tmpbuf */
		if (sizeof (tmpbuf) - (ptmp - tmpbuf) <= (size_t)exptoklen)
			return -1;

		memcpy(ptmp, exptok, exptoklen);
		pbuf   = ebuf + 1;
		ptmp  += exptoklen;
		tmpret = exptoklen;
	}
	if (ret >= sizeof tmpbuf)
		return -1;

	if ((ret = strlcpy(buf, tmpbuf, len)) >= len)
		return -1;

	return ret;
}

static int
mod_lowercase(char *buf, size_t len)
{
	char tmp[EXPAND_BUFFER];

	if (!lowercase(tmp, buf, sizeof tmp))
		return 0;
	if (strlcpy(buf, tmp, len) >= len)
		return 0;
	return 1;
}

static int
mod_uppercase(char *buf, size_t len)
{
	char tmp[EXPAND_BUFFER];

	if (!uppercase(tmp, buf, sizeof tmp))
		return 0;
	if (strlcpy(buf, tmp, len) >= len)
		return 0;
	return 1;
}

static int
mod_strip(char *buf, size_t len)
{
	char *tag, *at;
	unsigned int i;

	/* gilles+hackers -> gilles */
	if ((tag = strchr(buf, *env->sc_subaddressing_delim)) != NULL) {
		/* gilles+hackers@poolp.org -> gilles@poolp.org */
		if ((at = strchr(tag, '@')) != NULL) {
			for (i = 0; i <= strlen(at); ++i)
				tag[i] = at[i];
		} else
			*tag = '\0';
	}
	return 1;
}
