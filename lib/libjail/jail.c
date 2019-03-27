/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 James Gritton.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/jail.h>
#include <sys/linker.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "jail.h"

#define	SJPARAM		"security.jail.param"

#define JPS_IN_ADDR	1
#define JPS_IN6_ADDR	2

#define ARRAY_SANITY	5
#define ARRAY_SLOP	5


static int jailparam_import_enum(const char **values, int nvalues,
    const char *valstr, size_t valsize, int *value);
static int jailparam_type(struct jailparam *jp);
static int kldload_param(const char *name);
static char *noname(const char *name);
static char *nononame(const char *name);

char jail_errmsg[JAIL_ERRMSGLEN];

static const char *bool_values[] = { "false", "true" };
static const char *jailsys_values[] = { "disable", "new", "inherit" };


/*
 * Import a null-terminated parameter list and set a jail with the flags
 * and parameters.
 */
int
jail_setv(int flags, ...)
{
	va_list ap, tap;
	struct jailparam *jp;
	const char *name, *value;
	int njp, jid;

	/* Create the parameter list and import the parameters. */
	va_start(ap, flags);
	va_copy(tap, ap);
	for (njp = 0; va_arg(tap, char *) != NULL; njp++)
		(void)va_arg(tap, char *);
	va_end(tap);
	jp = alloca(njp * sizeof(struct jailparam));
	for (njp = 0; (name = va_arg(ap, char *)) != NULL;) {
		value = va_arg(ap, char *);
		if (jailparam_init(jp + njp, name) < 0)
			goto error;
		if (jailparam_import(jp + njp++, value) < 0)
			goto error;
	}
	va_end(ap);
	jid = jailparam_set(jp, njp, flags);
	jailparam_free(jp, njp);
	return (jid);

 error:
	jailparam_free(jp, njp);
	va_end(ap);
	return (-1);
}

/*
 * Read a null-terminated parameter list, get the referenced jail, and export
 * the parameters to the list.
 */
int
jail_getv(int flags, ...)
{
	va_list ap, tap;
	struct jailparam *jp, *jp_lastjid, *jp_jid, *jp_name, *jp_key;
	char *valarg, *value;
	const char *name, *key_value, *lastjid_value, *jid_value, *name_value;
	int njp, i, jid;

	/* Create the parameter list and find the key. */
	va_start(ap, flags);
	va_copy(tap, ap);
	for (njp = 0; va_arg(tap, char *) != NULL; njp++)
		(void)va_arg(tap, char *);
	va_end(tap);

	jp = alloca(njp * sizeof(struct jailparam));
	va_copy(tap, ap);
	jp_lastjid = jp_jid = jp_name = NULL;
	lastjid_value = jid_value = name_value = NULL;
	for (njp = 0; (name = va_arg(tap, char *)) != NULL; njp++) {
		value = va_arg(tap, char *);
		if (jailparam_init(jp + njp, name) < 0) {
			va_end(tap);
			goto error;
		}
		if (!strcmp(jp[njp].jp_name, "lastjid")) {
			jp_lastjid = jp + njp;
			lastjid_value = value;
		} else if (!strcmp(jp[njp].jp_name, "jid")) {
			jp_jid = jp + njp;
			jid_value = value;
		} if (!strcmp(jp[njp].jp_name, "name")) {
			jp_name = jp + njp;
			name_value = value;
		}
	}
	va_end(tap);
	/* Import the key parameter. */
	if (jp_lastjid != NULL) {
		jp_key = jp_lastjid;
		key_value = lastjid_value;
	} else if (jp_jid != NULL && strtol(jid_value, NULL, 10) != 0) {
		jp_key = jp_jid;
		key_value = jid_value;
	} else if (jp_name != NULL) {
		jp_key = jp_name;
		key_value = name_value;
	} else {
		strlcpy(jail_errmsg, "no jail specified", JAIL_ERRMSGLEN);
		errno = ENOENT;
		goto error;
	}
	if (jailparam_import(jp_key, key_value) < 0)
		goto error;
	/* Get the jail and export the parameters. */
	jid = jailparam_get(jp, njp, flags);
	if (jid < 0)
		goto error;
	for (i = 0; i < njp; i++) {
		(void)va_arg(ap, char *);
		valarg = va_arg(ap, char *);
		if (jp + i != jp_key) {
			/* It's up to the caller to ensure there's room. */
			if ((jp[i].jp_ctltype & CTLTYPE) == CTLTYPE_STRING)
				strcpy(valarg, jp[i].jp_value);
			else {
				value = jailparam_export(jp + i);
				if (value == NULL)
					goto error;
				strcpy(valarg, value);
				free(value);
			}
		}
	}
	jailparam_free(jp, njp);
	va_end(ap);
	return (jid);

 error:
	jailparam_free(jp, njp);
	va_end(ap);
	return (-1);
}

/*
 * Return a list of all known parameters.
 */
int
jailparam_all(struct jailparam **jpp)
{
	struct jailparam *jp, *tjp;
	size_t mlen1, mlen2, buflen;
	unsigned njp, nlist;
	int mib1[CTL_MAXNAME], mib2[CTL_MAXNAME - 2];
	char buf[MAXPATHLEN];

	njp = 0;
	nlist = 32;
	jp = malloc(nlist * sizeof(*jp));
	if (jp == NULL) {
		strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (-1);
	}
	mib1[0] = 0;
	mib1[1] = 2;
	mlen1 = CTL_MAXNAME - 2;
	if (sysctlnametomib(SJPARAM, mib1 + 2, &mlen1) < 0) {
		snprintf(jail_errmsg, JAIL_ERRMSGLEN,
		    "sysctlnametomib(" SJPARAM "): %s", strerror(errno));
		goto error;
	}
	for (;; njp++) {
		/* Get the next parameter. */
		mlen2 = sizeof(mib2);
		if (sysctl(mib1, mlen1 + 2, mib2, &mlen2, NULL, 0) < 0) {
			if (errno == ENOENT) {
				/* No more entries. */
				break;
			}
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "sysctl(0.2): %s", strerror(errno));
			goto error;
		}
		if (mib2[0] != mib1[2] ||
		    mib2[1] != mib1[3] ||
		    mib2[2] != mib1[4])
			break;
		/* Convert it to an ascii name. */
		memcpy(mib1 + 2, mib2, mlen2);
		mlen1 = mlen2 / sizeof(int);
		mib1[1] = 1;
		buflen = sizeof(buf);
		if (sysctl(mib1, mlen1 + 2, buf, &buflen, NULL, 0) < 0) {
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "sysctl(0.1): %s", strerror(errno));
			goto error;
		}
		if (buf[buflen - 2] == '.')
			buf[buflen - 2] = '\0';
		/* Add the parameter to the list */
		if (njp >= nlist) {
			nlist *= 2;
			tjp = reallocarray(jp, nlist, sizeof(*jp));
			if (tjp == NULL)
				goto error;
			jp = tjp;
		}
		if (jailparam_init(jp + njp, buf + sizeof(SJPARAM)) < 0)
			goto error;
		mib1[1] = 2;
	}
	jp = reallocarray(jp, njp, sizeof(*jp));
	*jpp = jp;
	return (njp);

 error:
	jailparam_free(jp, njp);
	free(jp);
	return (-1);
}

/*
 * Clear a jail parameter and copy in its name.
 */
int
jailparam_init(struct jailparam *jp, const char *name)
{

	memset(jp, 0, sizeof(*jp));
	jp->jp_name = strdup(name);
	if (jp->jp_name == NULL) {
		strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (-1);
	}
	if (jailparam_type(jp) < 0) {
		jailparam_free(jp, 1);
		jp->jp_name = NULL;
		jp->jp_value = NULL;
		return (-1);
	}
	return (0);
}

/*
 * Put a name and value into a jail parameter element, converting the value
 * to internal form.
 */
int
jailparam_import(struct jailparam *jp, const char *value)
{
	char *p, *ep, *tvalue;
	const char *avalue;
	int i, nval, fw;

	if (value == NULL)
		return (0);
	if ((jp->jp_ctltype & CTLTYPE) == CTLTYPE_STRING) {
		jp->jp_value = strdup(value);
		if (jp->jp_value == NULL) {
			strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
			return (-1);
		}
		return (0);
	}
	nval = 1;
	if (jp->jp_elemlen) {
		if (value[0] == '\0' || (value[0] == '-' && value[1] == '\0')) {
			jp->jp_value = strdup("");
			if (jp->jp_value == NULL) {
				strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
				return (-1);
			}
			jp->jp_valuelen = 0;
			return (0);
		}
		for (p = strchr(value, ','); p; p = strchr(p + 1, ','))
			nval++;
		jp->jp_valuelen = jp->jp_elemlen * nval;
	}
	jp->jp_value = malloc(jp->jp_valuelen);
	if (jp->jp_value == NULL) {
		strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (-1);
	}
	avalue = value;
	for (i = 0; i < nval; i++) {
		fw = nval == 1 ? strlen(avalue) : strcspn(avalue, ",");
		switch (jp->jp_ctltype & CTLTYPE) {
		case CTLTYPE_INT:
			if (jp->jp_flags & (JP_BOOL | JP_NOBOOL)) {
				if (!jailparam_import_enum(bool_values, 2,
				    avalue, fw, &((int *)jp->jp_value)[i])) {
					snprintf(jail_errmsg,
					    JAIL_ERRMSGLEN, "%s: "
					    "unknown boolean value \"%.*s\"",
					    jp->jp_name, fw, avalue);
					errno = EINVAL;
					goto error;
				}
				break;
			}
			if (jp->jp_flags & JP_JAILSYS) {
				/*
				 * Allow setting a jailsys parameter to "new"
				 * in a booleanesque fashion.
				 */
				if (value[0] == '\0')
					((int *)jp->jp_value)[i] = JAIL_SYS_NEW;
				else if (!jailparam_import_enum(jailsys_values,
				    sizeof(jailsys_values) /
				    sizeof(jailsys_values[0]), avalue, fw,
				    &((int *)jp->jp_value)[i])) {
					snprintf(jail_errmsg,
					    JAIL_ERRMSGLEN, "%s: "
					    "unknown jailsys value \"%.*s\"",
					    jp->jp_name, fw, avalue);
					errno = EINVAL;
					goto error;
				}
				break;
			}
			((int *)jp->jp_value)[i] = strtol(avalue, &ep, 10);
		integer_test:
			if (ep != avalue + fw) {
				snprintf(jail_errmsg, JAIL_ERRMSGLEN,
				    "%s: non-integer value \"%.*s\"",
				    jp->jp_name, fw, avalue);
				errno = EINVAL;
				goto error;
			}
			break;
		case CTLTYPE_UINT:
			((unsigned *)jp->jp_value)[i] =
			    strtoul(avalue, &ep, 10);
			goto integer_test;
		case CTLTYPE_LONG:
			((long *)jp->jp_value)[i] = strtol(avalue, &ep, 10);
			goto integer_test;
		case CTLTYPE_ULONG:
			((unsigned long *)jp->jp_value)[i] =
			    strtoul(avalue, &ep, 10);
			goto integer_test;
		case CTLTYPE_S64:
			((int64_t *)jp->jp_value)[i] =
			    strtoimax(avalue, &ep, 10);
			goto integer_test;
		case CTLTYPE_U64:
			((uint64_t *)jp->jp_value)[i] =
			    strtoumax(avalue, &ep, 10);
			goto integer_test;
		case CTLTYPE_STRUCT:
			tvalue = alloca(fw + 1);
			strlcpy(tvalue, avalue, fw + 1);
			switch (jp->jp_structtype) {
			case JPS_IN_ADDR:
				if (inet_pton(AF_INET, tvalue,
				    &((struct in_addr *)jp->jp_value)[i]) != 1)
				{
					snprintf(jail_errmsg,
					    JAIL_ERRMSGLEN,
					    "%s: not an IPv4 address: %s",
					    jp->jp_name, tvalue);
					errno = EINVAL;
					goto error;
				}
				break;
			case JPS_IN6_ADDR:
				if (inet_pton(AF_INET6, tvalue,
				    &((struct in6_addr *)jp->jp_value)[i]) != 1)
				{
					snprintf(jail_errmsg,
					    JAIL_ERRMSGLEN,
					    "%s: not an IPv6 address: %s",
					    jp->jp_name, tvalue);
					errno = EINVAL;
					goto error;
				}
				break;
			default:
				goto unknown_type;
			}
			break;
		default:
		unknown_type:
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "unknown type for %s", jp->jp_name);
			errno = ENOENT;
			goto error;
		}
		avalue += fw + 1;
	}
	return (0);

 error:
	free(jp->jp_value);
	jp->jp_value = NULL;
	return (-1);
}

static int
jailparam_import_enum(const char **values, int nvalues, const char *valstr,
    size_t valsize, int *value)
{
	char *ep;
	int i;

	for (i = 0; i < nvalues; i++)
		if (valsize == strlen(values[i]) &&
		    !strncasecmp(valstr, values[i], valsize)) {
			*value = i;
			return 1;
		}
	*value = strtol(valstr, &ep, 10);
	return (ep == valstr + valsize);
}

/*
 * Put a name and value into a jail parameter element, copying the value
 * but not altering it.
 */
int
jailparam_import_raw(struct jailparam *jp, void *value, size_t valuelen)
{

	jp->jp_value = value;
	jp->jp_valuelen = valuelen;
	jp->jp_flags |= JP_RAWVALUE;
	return (0);
}

/*
 * Run the jail_set and jail_get system calls on a parameter list.
 */
int
jailparam_set(struct jailparam *jp, unsigned njp, int flags)
{
	struct iovec *jiov;
	char *nname;
	int i, jid, bool0;
	unsigned j;

	jiov = alloca(sizeof(struct iovec) * 2 * (njp + 1));
	bool0 = 0;
	for (i = j = 0; j < njp; j++) {
		jiov[i].iov_base = jp[j].jp_name;
		jiov[i].iov_len = strlen(jp[j].jp_name) + 1;
		i++;
		if (jp[j].jp_flags & (JP_BOOL | JP_NOBOOL)) {
			/*
			 * Set booleans without values.  If one has a value of
			 * zero, change it to (or from) its "no" counterpart.
			 */
			jiov[i].iov_base = NULL;
			jiov[i].iov_len = 0;
			if (jp[j].jp_value != NULL &&
			    jp[j].jp_valuelen == sizeof(int) &&
			    !*(int *)jp[j].jp_value) {
				bool0 = 1;
				nname = jp[j].jp_flags & JP_BOOL
				    ? noname(jp[j].jp_name)
				    : nononame(jp[j].jp_name);
				if (nname == NULL) {
					njp = j;
					jid = -1;
					goto done;
				}
				jiov[i - 1].iov_base = nname;
				jiov[i - 1].iov_len = strlen(nname) + 1;
				
			}
		} else {
			/*
			 * Try to fill in missing values with an empty string.
			 */
			if (jp[j].jp_value == NULL && jp[j].jp_valuelen > 0 &&
			    jailparam_import(jp + j, "") < 0) {
				njp = j;
				jid = -1;
				goto done;
			}
			jiov[i].iov_base = jp[j].jp_value;
			jiov[i].iov_len =
			    (jp[j].jp_ctltype & CTLTYPE) == CTLTYPE_STRING
			    ? strlen(jp[j].jp_value) + 1
			    : jp[j].jp_valuelen;
		}
		i++;
	}
	jiov[i].iov_base = __DECONST(char *, "errmsg");
	jiov[i].iov_len = sizeof("errmsg");
	i++;
	jiov[i].iov_base = jail_errmsg;
	jiov[i].iov_len = JAIL_ERRMSGLEN;
	i++;
	jail_errmsg[0] = 0;
	jid = jail_set(jiov, i, flags);
	if (jid < 0 && !jail_errmsg[0])
		snprintf(jail_errmsg, sizeof(jail_errmsg), "jail_set: %s",
		    strerror(errno));
 done:
	if (bool0)
		for (j = 0; j < njp; j++)
			if ((jp[j].jp_flags & (JP_BOOL | JP_NOBOOL)) &&
			    jp[j].jp_value != NULL &&
			    jp[j].jp_valuelen == sizeof(int) &&
			    !*(int *)jp[j].jp_value)
				free(jiov[j * 2].iov_base);
	return (jid);
}

int
jailparam_get(struct jailparam *jp, unsigned njp, int flags)
{
	struct iovec *jiov;
	struct jailparam *jp_lastjid, *jp_jid, *jp_name, *jp_key;
	int i, ai, ki, jid, arrays, sanity;
	unsigned j;

	/*
	 * Get the types for all parameters.
	 * Find the key and any array parameters.
	 */
	jiov = alloca(sizeof(struct iovec) * 2 * (njp + 1));
	jp_lastjid = jp_jid = jp_name = NULL;
	arrays = 0;
	for (ai = j = 0; j < njp; j++) {
		if (!strcmp(jp[j].jp_name, "lastjid"))
			jp_lastjid = jp + j;
		else if (!strcmp(jp[j].jp_name, "jid"))
			jp_jid = jp + j;
		else if (!strcmp(jp[j].jp_name, "name"))
			jp_name = jp + j;
		else if (jp[j].jp_elemlen && !(jp[j].jp_flags & JP_RAWVALUE)) {
			arrays = 1;
			jiov[ai].iov_base = jp[j].jp_name;
			jiov[ai].iov_len = strlen(jp[j].jp_name) + 1;
			ai++;
			jiov[ai].iov_base = NULL;
			jiov[ai].iov_len = 0;
			ai++;
		}
	}
	jp_key = jp_lastjid ? jp_lastjid :
	    jp_jid && jp_jid->jp_valuelen == sizeof(int) &&
	    jp_jid->jp_value && *(int *)jp_jid->jp_value ? jp_jid : jp_name;
	if (jp_key == NULL || jp_key->jp_value == NULL) {
		strlcpy(jail_errmsg, "no jail specified", JAIL_ERRMSGLEN);
		errno = ENOENT;
		return (-1);
	}
	ki = ai;
	jiov[ki].iov_base = jp_key->jp_name;
	jiov[ki].iov_len = strlen(jp_key->jp_name) + 1;
	ki++;
	jiov[ki].iov_base = jp_key->jp_value;
	jiov[ki].iov_len = (jp_key->jp_ctltype & CTLTYPE) == CTLTYPE_STRING
	    ? strlen(jp_key->jp_value) + 1 : jp_key->jp_valuelen;
	ki++;
	jiov[ki].iov_base = __DECONST(char *, "errmsg");
	jiov[ki].iov_len = sizeof("errmsg");
	ki++;
	jiov[ki].iov_base = jail_errmsg;
	jiov[ki].iov_len = JAIL_ERRMSGLEN;
	ki++;
	jail_errmsg[0] = 0;
	if (arrays && jail_get(jiov, ki, flags) < 0) {
		if (!jail_errmsg[0])
			snprintf(jail_errmsg, sizeof(jail_errmsg),
			    "jail_get: %s", strerror(errno));
		return (-1);
	}
	/* Allocate storage for all parameters. */
	for (ai = j = 0, i = ki; j < njp; j++) {
		if (jp[j].jp_elemlen && !(jp[j].jp_flags & JP_RAWVALUE)) {
			ai++;
			jiov[ai].iov_len += jp[j].jp_elemlen * ARRAY_SLOP;
			if (jp[j].jp_valuelen >= jiov[ai].iov_len)
				jiov[ai].iov_len = jp[j].jp_valuelen;
			else {
				jp[j].jp_valuelen = jiov[ai].iov_len;
				if (jp[j].jp_value != NULL)
					free(jp[j].jp_value);
				jp[j].jp_value = malloc(jp[j].jp_valuelen);
				if (jp[j].jp_value == NULL) {
					strerror_r(errno, jail_errmsg,
					    JAIL_ERRMSGLEN);
					return (-1);
				}
			}
			jiov[ai].iov_base = jp[j].jp_value;
			memset(jiov[ai].iov_base, 0, jiov[ai].iov_len);
			ai++;
		} else if (jp + j != jp_key) {
			jiov[i].iov_base = jp[j].jp_name;
			jiov[i].iov_len = strlen(jp[j].jp_name) + 1;
			i++;
			if (jp[j].jp_value == NULL &&
			    !(jp[j].jp_flags & JP_RAWVALUE)) {
				jp[j].jp_value = malloc(jp[j].jp_valuelen);
				if (jp[j].jp_value == NULL) {
					strerror_r(errno, jail_errmsg,
					    JAIL_ERRMSGLEN);
					return (-1);
				}
			}
			jiov[i].iov_base = jp[j].jp_value;
			jiov[i].iov_len = jp[j].jp_valuelen;
			memset(jiov[i].iov_base, 0, jiov[i].iov_len);
			i++;
		}
	}
	/*
	 * Get the prison.  If there are array elements, retry a few times
	 * in case their sizes changed from under us.
	 */
	for (sanity = 0;; sanity++) {
		jid = jail_get(jiov, i, flags);
		if (jid >= 0 || !arrays || sanity == ARRAY_SANITY ||
		    errno != EINVAL || jail_errmsg[0])
			break;
		for (ai = j = 0; j < njp; j++) {
			if (jp[j].jp_elemlen &&
			    !(jp[j].jp_flags & JP_RAWVALUE)) {
				ai++;
				jiov[ai].iov_base = NULL;
				jiov[ai].iov_len = 0;
				ai++;
			}
		}
		if (jail_get(jiov, ki, flags) < 0)
			break;
		for (ai = j = 0; j < njp; j++) {
			if (jp[j].jp_elemlen &&
			    !(jp[j].jp_flags & JP_RAWVALUE)) {
				ai++;
				jiov[ai].iov_len +=
				    jp[j].jp_elemlen * ARRAY_SLOP;
				if (jp[j].jp_valuelen >= jiov[ai].iov_len)
					jiov[ai].iov_len = jp[j].jp_valuelen;
				else {
					jp[j].jp_valuelen = jiov[ai].iov_len;
					if (jp[j].jp_value != NULL)
						free(jp[j].jp_value);
					jp[j].jp_value =
					    malloc(jiov[ai].iov_len);
					if (jp[j].jp_value == NULL) {
						strerror_r(errno, jail_errmsg,
						    JAIL_ERRMSGLEN);
						return (-1);
					}
				}
				jiov[ai].iov_base = jp[j].jp_value;
				memset(jiov[ai].iov_base, 0, jiov[ai].iov_len);
				ai++;
			}
		}
	}
	if (jid < 0 && !jail_errmsg[0])
		snprintf(jail_errmsg, sizeof(jail_errmsg),
		    "jail_get: %s", strerror(errno));
	for (ai = j = 0, i = ki; j < njp; j++) {
		if (jp[j].jp_elemlen && !(jp[j].jp_flags & JP_RAWVALUE)) {
			ai++;
			jp[j].jp_valuelen = jiov[ai].iov_len;
			ai++;
		} else if (jp + j != jp_key) {
			i++;
			jp[j].jp_valuelen = jiov[i].iov_len;
			i++;
		}
	}
	return (jid);
}

/*
 * Convert a jail parameter's value to external form.
 */
char *
jailparam_export(struct jailparam *jp)
{
	size_t *valuelens;
	char *value, *tvalue, **values;
	size_t valuelen;
	int i, nval, ival;
	char valbuf[INET6_ADDRSTRLEN];

	if ((jp->jp_ctltype & CTLTYPE) == CTLTYPE_STRING) {
		value = strdup(jp->jp_value);
		if (value == NULL)
			strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (value);
	}
	nval = jp->jp_elemlen ? jp->jp_valuelen / jp->jp_elemlen : 1;
	if (nval == 0) {
		value = strdup("");
		if (value == NULL)
			strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (value);
	}
	values = alloca(nval * sizeof(char *));
	valuelens = alloca(nval * sizeof(size_t));
	valuelen = 0;
	for (i = 0; i < nval; i++) {
		switch (jp->jp_ctltype & CTLTYPE) {
		case CTLTYPE_INT:
			ival = ((int *)jp->jp_value)[i];
			if ((jp->jp_flags & (JP_BOOL | JP_NOBOOL)) &&
			    (unsigned)ival < 2) {
				strlcpy(valbuf, bool_values[ival],
				    sizeof(valbuf));
				break;
			}
			if ((jp->jp_flags & JP_JAILSYS) &&
			    (unsigned)ival < sizeof(jailsys_values) /
			    sizeof(jailsys_values[0])) {
				strlcpy(valbuf, jailsys_values[ival],
				    sizeof(valbuf));
				break;
			}
			snprintf(valbuf, sizeof(valbuf), "%d", ival);
			break;
		case CTLTYPE_UINT:
			snprintf(valbuf, sizeof(valbuf), "%u",
			    ((unsigned *)jp->jp_value)[i]);
			break;
		case CTLTYPE_LONG:
			snprintf(valbuf, sizeof(valbuf), "%ld",
			    ((long *)jp->jp_value)[i]);
			break;
		case CTLTYPE_ULONG:
			snprintf(valbuf, sizeof(valbuf), "%lu",
			    ((unsigned long *)jp->jp_value)[i]);
			break;
		case CTLTYPE_S64:
			snprintf(valbuf, sizeof(valbuf), "%jd",
			    (intmax_t)((int64_t *)jp->jp_value)[i]);
			break;
		case CTLTYPE_U64:
			snprintf(valbuf, sizeof(valbuf), "%ju",
			    (uintmax_t)((uint64_t *)jp->jp_value)[i]);
			break;
		case CTLTYPE_STRUCT:
			switch (jp->jp_structtype) {
			case JPS_IN_ADDR:
				if (inet_ntop(AF_INET,
				    &((struct in_addr *)jp->jp_value)[i],
				    valbuf, sizeof(valbuf)) == NULL) {
					strerror_r(errno, jail_errmsg,
					    JAIL_ERRMSGLEN);
					return (NULL);
				}
				break;
			case JPS_IN6_ADDR:
				if (inet_ntop(AF_INET6,
				    &((struct in6_addr *)jp->jp_value)[i],
				    valbuf, sizeof(valbuf)) == NULL) {
					strerror_r(errno, jail_errmsg,
					    JAIL_ERRMSGLEN);
					return (NULL);
				}
				break;
			default:
				goto unknown_type;
			}
			break;
		default:
		unknown_type:
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "unknown type for %s", jp->jp_name);
			errno = ENOENT;
			return (NULL);
		}
		valuelens[i] = strlen(valbuf) + 1;
		valuelen += valuelens[i];
		values[i] = alloca(valuelens[i]);
		strcpy(values[i], valbuf);
	}
	value = malloc(valuelen);
	if (value == NULL)
		strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
	else {
		tvalue = value;
		for (i = 0; i < nval; i++) {
			strcpy(tvalue, values[i]);
			if (i < nval - 1) {
				tvalue += valuelens[i];
				tvalue[-1] = ',';
			}
		}
	}
	return (value);
}

/*
 * Free the contents of a jail parameter list (but not the list itself).
 */
void
jailparam_free(struct jailparam *jp, unsigned njp)
{
	unsigned j;

	for (j = 0; j < njp; j++) {
		free(jp[j].jp_name);
		if (!(jp[j].jp_flags & JP_RAWVALUE))
			free(jp[j].jp_value);
	}
}

/*
 * Find a parameter's type and size from its MIB.
 */
static int
jailparam_type(struct jailparam *jp)
{
	char *p, *name, *nname;
	size_t miblen, desclen;
	int i, isarray;
	struct {
	    int i;
	    char s[MAXPATHLEN];
	} desc;
	int mib[CTL_MAXNAME];

	/* The "lastjid" parameter isn't real. */
	name = jp->jp_name;
	if (!strcmp(name, "lastjid")) {
		jp->jp_valuelen = sizeof(int);
		jp->jp_ctltype = CTLTYPE_INT | CTLFLAG_WR;
		return (0);
	}

	/* Find the sysctl that describes the parameter. */
	mib[0] = 0;
	mib[1] = 3;
	snprintf(desc.s, sizeof(desc.s), SJPARAM ".%s", name);
	miblen = sizeof(mib) - 2 * sizeof(int);
	if (sysctl(mib, 2, mib + 2, &miblen, desc.s, strlen(desc.s)) < 0) {
		if (errno != ENOENT) {
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "sysctl(0.3.%s): %s", name, strerror(errno));
			return (-1);
		}
		if (kldload_param(name) >= 0 && sysctl(mib, 2, mib + 2, &miblen,
		    desc.s, strlen(desc.s)) >= 0)
			goto mib_desc;
		/*
		 * The parameter probably doesn't exist.  But it might be
		 * the "no" counterpart to a boolean.
		 */
		nname = nononame(name);
		if (nname == NULL) {
		unknown_parameter:
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "unknown parameter: %s", jp->jp_name);
			errno = ENOENT;
			return (-1);
		}
		name = alloca(strlen(nname) + 1);
		strcpy(name, nname);
		free(nname);
		snprintf(desc.s, sizeof(desc.s), SJPARAM ".%s", name);
		miblen = sizeof(mib) - 2 * sizeof(int);
		if (sysctl(mib, 2, mib + 2, &miblen, desc.s,
		    strlen(desc.s)) < 0)
			goto unknown_parameter;
		jp->jp_flags |= JP_NOBOOL;
	}
 mib_desc:
	mib[1] = 4;
	desclen = sizeof(desc);
	if (sysctl(mib, (miblen / sizeof(int)) + 2, &desc, &desclen,
	    NULL, 0) < 0) {
		snprintf(jail_errmsg, JAIL_ERRMSGLEN,
		    "sysctl(0.4.%s): %s", name, strerror(errno));
		return (-1);
	}
	jp->jp_ctltype = desc.i;
	/* If this came from removing a "no", it better be a boolean. */
	if (jp->jp_flags & JP_NOBOOL) {
		if ((desc.i & CTLTYPE) == CTLTYPE_INT && desc.s[0] == 'B') {
			jp->jp_valuelen = sizeof(int);
			return (0);
		}
		else if ((desc.i & CTLTYPE) != CTLTYPE_NODE)
			goto unknown_parameter;
	}
	/* See if this is an array type. */
	p = strchr(desc.s, '\0');
	isarray  = 0;
	if (p - 2 < desc.s || strcmp(p - 2, ",a"))
		isarray = 0;
	else {
		isarray = 1;
		p[-2] = 0;
	}
	/* Look for types we understand. */
	switch (desc.i & CTLTYPE) {
	case CTLTYPE_INT:
		if (desc.s[0] == 'B')
			jp->jp_flags |= JP_BOOL;
		else if (!strcmp(desc.s, "E,jailsys"))
			jp->jp_flags |= JP_JAILSYS;
	case CTLTYPE_UINT:
		jp->jp_valuelen = sizeof(int);
		break;
	case CTLTYPE_LONG:
	case CTLTYPE_ULONG:
		jp->jp_valuelen = sizeof(long);
		break;
	case CTLTYPE_S64:
	case CTLTYPE_U64:
		jp->jp_valuelen = sizeof(int64_t);
		break;
	case CTLTYPE_STRING:
		desc.s[0] = 0;
		desclen = sizeof(desc.s);
		if (sysctl(mib + 2, miblen / sizeof(int), desc.s, &desclen,
		    NULL, 0) < 0) {
			snprintf(jail_errmsg, JAIL_ERRMSGLEN,
			    "sysctl(" SJPARAM ".%s): %s", name,
			    strerror(errno));
			return (-1);
		}
		jp->jp_valuelen = strtoul(desc.s, NULL, 10);
		break;
	case CTLTYPE_STRUCT:
		if (!strcmp(desc.s, "S,in_addr")) {
			jp->jp_structtype = JPS_IN_ADDR;
			jp->jp_valuelen = sizeof(struct in_addr);
		} else if (!strcmp(desc.s, "S,in6_addr")) {
			jp->jp_structtype = JPS_IN6_ADDR;
			jp->jp_valuelen = sizeof(struct in6_addr);
		} else {
			desclen = 0;
			if (sysctl(mib + 2, miblen / sizeof(int),
			    NULL, &jp->jp_valuelen, NULL, 0) < 0) {
				snprintf(jail_errmsg, JAIL_ERRMSGLEN,
				    "sysctl(" SJPARAM ".%s): %s", name,
				    strerror(errno));
				return (-1);
			}
		}
		break;
	case CTLTYPE_NODE:
		/*
		 * A node might be described by an empty-named child,
		 * which would be immediately before or after the node itself.
		 */
		mib[1] = 1;
		miblen += sizeof(int);
		for (i = -1; i <= 1; i += 2) {
			mib[(miblen / sizeof(int)) + 1] =
			    mib[(miblen / sizeof(int))] + i;
			desclen = sizeof(desc.s);
			if (sysctl(mib, (miblen / sizeof(int)) + 2, desc.s,
			    &desclen, NULL, 0) < 0) {
				if (errno == ENOENT)
					continue;
				snprintf(jail_errmsg, JAIL_ERRMSGLEN,
				    "sysctl(0.1): %s", strerror(errno));
				return (-1);
			}
			if (desclen == sizeof(SJPARAM) + strlen(name) + 2 &&
			    memcmp(SJPARAM ".", desc.s, sizeof(SJPARAM)) == 0 &&
			    memcmp(name, desc.s + sizeof(SJPARAM),
			    desclen - sizeof(SJPARAM) - 2) == 0 &&
			    desc.s[desclen - 2] == '.')
				goto mib_desc;
		}
		goto unknown_parameter;
	default:
		snprintf(jail_errmsg, JAIL_ERRMSGLEN,
		    "unknown type for %s", jp->jp_name);
		errno = ENOENT;
		return (-1);
	}
	if (isarray) {
		jp->jp_elemlen = jp->jp_valuelen;
		jp->jp_valuelen = 0;
	}
	return (0);
}

/*
 * Attempt to load a kernel module matching an otherwise nonexistent parameter.
 */
static int
kldload_param(const char *name)
{
	int kl;

	if (strcmp(name, "linux") == 0 || strncmp(name, "linux.", 6) == 0)
		kl = kldload("linux");
	else if (strcmp(name, "sysvmsg") == 0 || strcmp(name, "sysvsem") == 0 ||
	    strcmp(name, "sysvshm") == 0)
		kl = kldload(name);
	else if (strncmp(name, "allow.mount.", 12) == 0) {
		/* Load the matching filesystem */
		const char *modname = name + 12;

		kl = kldload(modname);
		if (kl < 0 && errno == ENOENT &&
		    strncmp(modname, "no", 2) == 0)
			kl = kldload(modname + 2);
	} else {
		errno = ENOENT;
		return (-1);
	}
	if (kl < 0 && errno == EEXIST) {
		/*
		 * In the module is already loaded, then it must not contain
		 * the parameter.
		 */
		errno = ENOENT;
	}
	return kl;
}

/*
 * Change a boolean parameter name into its "no" counterpart or vice versa.
 */
static char *
noname(const char *name)
{
	char *nname, *p;

	nname = malloc(strlen(name) + 3);
	if (nname == NULL) {
		strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (NULL);
	}
	p = strrchr(name, '.');
	if (p != NULL)
		sprintf(nname, "%.*s.no%s", (int)(p - name), name, p + 1);
	else
		sprintf(nname, "no%s", name);
	return (nname);
}

static char *
nononame(const char *name)
{
	char *p, *nname;

	p = strrchr(name, '.');
	if (strncmp(p ? p + 1 : name, "no", 2)) {
		snprintf(jail_errmsg, sizeof(jail_errmsg),
		    "mismatched boolean: %s", name);
		errno = EINVAL;
		return (NULL);
	}
	nname = malloc(strlen(name) - 1);
	if (nname == NULL) {
		strerror_r(errno, jail_errmsg, JAIL_ERRMSGLEN);
		return (NULL);
	}
	if (p != NULL)
		sprintf(nname, "%.*s.%s", (int)(p - name), name, p + 3);
	else
		strcpy(nname, name + 2);
	return (nname);
}
