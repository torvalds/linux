/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2001 Peter Wemm <peter@FreeBSD.org>
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
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/bus.h>

#define	FBACK_MDENV	0	/* MD env (e.g. loader.conf) */
#define	FBACK_STENV	1	/* Static env */
#define	FBACK_STATIC	2	/* static_hints */

/*
 * We'll use hintenv_merged to indicate that the dynamic environment has been
 * properly prepared for hint usage.  This implies that the dynamic environment
 * has already been setup (dynamic_kenv) and that we have added any supplied
 * static_hints to the dynamic environment.
 */
static bool	hintenv_merged;
/* Static environment and static hints cannot change, so we'll skip known bad */
static bool	stenv_skip;
static bool	sthints_skip;
/*
 * Access functions for device resources.
 */

static void
static_hints_to_env(void *data __unused)
{
	const char *cp;
	char *line, *eq;
	int eqidx, i;

	cp = static_hints;
	while (cp && *cp != '\0') {
		eq = strchr(cp, '=');
		if (eq == NULL)
			/* Bad hint value */
			continue;
		eqidx = eq - cp;

		i = strlen(cp);
		line = malloc(i + 1, M_TEMP, M_WAITOK);
		strcpy(line, cp);
		line[eqidx] = line[i] = '\0';
		/*
		 * Before adding a hint to the dynamic environment, check if
		 * another value for said hint has already been added.  This is
		 * needed because static environment overrides static hints and
		 * dynamic environment overrides all.
		 */
		if (testenv(line) == 0)
			kern_setenv(line, line + eqidx + 1);
		free(line, M_TEMP);
		cp += i + 1;
	}
	hintenv_merged = true;
}

/* Any time after dynamic env is setup */
SYSINIT(hintenv, SI_SUB_KMEM + 1, SI_ORDER_SECOND, static_hints_to_env, NULL);

/*
 * Checks the environment to see if we even have any hints.  If it has no hints,
 * then res_find can take the hint that there's no point in searching it and
 * either move on to the next environment or fail early.
 */
static bool
_res_checkenv(char *envp)
{
	char *cp;

	cp = envp;
	while (cp) {
		if (strncmp(cp, "hint.", 5) == 0)
			return (true);
		while (*cp != '\0')
			cp++;
		cp++;
		if (*cp == '\0')
			break;
	}
	return (false);
}

/*
 * Evil wildcarding resource string lookup.
 * This walks the supplied env string table and returns a match.
 * The start point can be remembered for incremental searches.
 */
static int
res_find(char **hintp_cookie, int *line, int *startln,
    const char *name, int *unit, const char *resname, const char *value,
    const char **ret_name, int *ret_namelen, int *ret_unit,
    const char **ret_resname, int *ret_resnamelen, const char **ret_value)
{
	int fbacklvl = FBACK_MDENV, i = 0, n = 0;
	char r_name[32];
	int r_unit;
	char r_resname[32];
	char r_value[128];
	const char *s, *cp;
	char *hintp, *p;
	bool dyn_used = false;


	/*
	 * We are expecting that the caller will pass us a hintp_cookie that
	 * they are tracking.  Upon entry, if *hintp_cookie is *not* set, this
	 * indicates to us that we should be figuring out based on the current
	 * environment where to search.  This keeps us sane throughout the
	 * entirety of a single search.
	 */
	if (*hintp_cookie == NULL) {
		hintp = NULL;
		if (hintenv_merged) {
			/*
			 * static_hints, if it was previously used, has
			 * already been folded in to the environment
			 * by this point.
			 */
			mtx_lock(&kenv_lock);
			cp = kenvp[0];
			for (i = 0; cp != NULL; cp = kenvp[++i]) {
				if (!strncmp(cp, "hint.", 5)) {
					hintp = kenvp[0];
					break;
				}
			}
			mtx_unlock(&kenv_lock);
			dyn_used = true;
		} else {
			/*
			 * We'll have a chance to keep coming back here until
			 * we've actually exhausted all of our possibilities.
			 * We might have chosen the MD/Static env because it
			 * had some kind of hints, but perhaps it didn't have
			 * the hint we are looking for.  We don't provide any
			 * fallback when searching the dynamic environment.
			 */
fallback:
			if (dyn_used || fbacklvl >= FBACK_STATIC)
				return (ENOENT);

			switch (fbacklvl) {
			case FBACK_MDENV:
				fbacklvl++;
				if (_res_checkenv(md_envp)) {
					hintp = md_envp;
					break;
				}

				/* FALLTHROUGH */
			case FBACK_STENV:
				fbacklvl++;
				if (!stenv_skip && _res_checkenv(kern_envp)) {
					hintp = kern_envp;
					break;
				} else
					stenv_skip = true;

				/* FALLTHROUGH */
			case FBACK_STATIC:
				fbacklvl++;
				/* We'll fallback to static_hints if needed/can */
				if (!sthints_skip &&
				    _res_checkenv(static_hints))
					hintp = static_hints;
				else
					sthints_skip = true;

				break;
			default:
				return (ENOENT);
			}
		}

		if (hintp == NULL)
			return (ENOENT);
		*hintp_cookie = hintp;
	} else {
		hintp = *hintp_cookie;
		if (hintenv_merged && hintp == kenvp[0])
			dyn_used = true;
		else
			/*
			 * If we aren't using the dynamic environment, we need
			 * to run through the proper fallback procedure again.
			 * This is so that we do continuations right if we're
			 * working with *line and *startln.
			 */
			goto fallback;
	}

	if (dyn_used) {
		mtx_lock(&kenv_lock);
		i = 0;
	}

	cp = hintp;
	while (cp) {
		(*line)++;
		if (strncmp(cp, "hint.", 5) != 0)
			goto nexthint;
		n = sscanf(cp, "hint.%32[^.].%d.%32[^=]=%127s", r_name, &r_unit,
		    r_resname, r_value);
		if (n != 4) {
			printf("CONFIG: invalid hint '%s'\n", cp);
			p = strchr(cp, 'h');
			*p = 'H';
			goto nexthint;
		}
		if (startln && *startln >= 0 && *line < *startln)
			goto nexthint;
		if (name && strcmp(name, r_name) != 0)
			goto nexthint;
		if (unit && *unit != r_unit)
			goto nexthint;
		if (resname && strcmp(resname, r_resname) != 0)
			goto nexthint;
		if (value && strcmp(value, r_value) != 0)
			goto nexthint;
		/* Successfully found a hint matching all criteria */
		break;
nexthint:
		if (dyn_used) {
			cp = kenvp[++i];
			if (cp == NULL)
				break;
		} else {
			while (*cp != '\0')
				cp++;
			cp++;
			if (*cp == '\0') {
				cp = NULL;
				break;
			}
		}
	}
	if (dyn_used)
		mtx_unlock(&kenv_lock);
	if (cp == NULL)
		goto fallback;

	s = cp;
	/* This is a bit of a hack, but at least is reentrant */
	/* Note that it returns some !unterminated! strings. */
	s = strchr(s, '.') + 1;		/* start of device */
	if (ret_name)
		*ret_name = s;
	s = strchr(s, '.') + 1;		/* start of unit */
	if (ret_namelen && ret_name)
		*ret_namelen = s - *ret_name - 1; /* device length */
	if (ret_unit)
		*ret_unit = r_unit;
	s = strchr(s, '.') + 1;		/* start of resname */
	if (ret_resname)
		*ret_resname = s;
	s = strchr(s, '=') + 1;		/* start of value */
	if (ret_resnamelen && ret_resname)
		*ret_resnamelen = s - *ret_resname - 1; /* value len */
	if (ret_value)
		*ret_value = s;
	if (startln)			/* line number for anchor */
		*startln = *line + 1;
	return 0;
}

/*
 * Search all the data sources for matches to our query.  We look for
 * dynamic hints first as overrides for static or fallback hints.
 */
static int
resource_find(int *line, int *startln,
    const char *name, int *unit, const char *resname, const char *value,
    const char **ret_name, int *ret_namelen, int *ret_unit,
    const char **ret_resname, int *ret_resnamelen, const char **ret_value)
{
	int i;
	int un;
	char *hintp;

	*line = 0;
	hintp = NULL;

	/* Search for exact unit matches first */
	i = res_find(&hintp, line, startln, name, unit, resname, value,
	    ret_name, ret_namelen, ret_unit, ret_resname, ret_resnamelen,
	    ret_value);
	if (i == 0)
		return 0;
	if (unit == NULL)
		return ENOENT;
	/* If we are still here, search for wildcard matches */
	un = -1;
	i = res_find(&hintp, line, startln, name, &un, resname, value,
	    ret_name, ret_namelen, ret_unit, ret_resname, ret_resnamelen,
	    ret_value);
	if (i == 0)
		return 0;
	return ENOENT;
}

int
resource_int_value(const char *name, int unit, const char *resname, int *result)
{
	int error;
	const char *str;
	char *op;
	unsigned long val;
	int line;

	line = 0;
	error = resource_find(&line, NULL, name, &unit, resname, NULL,
	    NULL, NULL, NULL, NULL, NULL, &str);
	if (error)
		return error;
	if (*str == '\0') 
		return EFTYPE;
	val = strtoul(str, &op, 0);
	if (*op != '\0') 
		return EFTYPE;
	*result = val;
	return 0;
}

int
resource_long_value(const char *name, int unit, const char *resname,
    long *result)
{
	int error;
	const char *str;
	char *op;
	unsigned long val;
	int line;

	line = 0;
	error = resource_find(&line, NULL, name, &unit, resname, NULL,
	    NULL, NULL, NULL, NULL, NULL, &str);
	if (error)
		return error;
	if (*str == '\0') 
		return EFTYPE;
	val = strtoul(str, &op, 0);
	if (*op != '\0') 
		return EFTYPE;
	*result = val;
	return 0;
}

int
resource_string_value(const char *name, int unit, const char *resname,
    const char **result)
{
	int error;
	const char *str;
	int line;

	line = 0;
	error = resource_find(&line, NULL, name, &unit, resname, NULL,
	    NULL, NULL, NULL, NULL, NULL, &str);
	if (error)
		return error;
	*result = str;
	return 0;
}

/*
 * This is a bit nasty, but allows us to not modify the env strings.
 */
static const char *
resource_string_copy(const char *s, int len)
{
	static char stringbuf[256];
	static int offset = 0;
	const char *ret;

	if (len == 0)
		len = strlen(s);
	if (len > 255)
		return NULL;
	if ((offset + len + 1) > 255)
		offset = 0;
	bcopy(s, &stringbuf[offset], len);
	stringbuf[offset + len] = '\0';
	ret = &stringbuf[offset];
	offset += len + 1;
	return ret;
}

/*
 * err = resource_find_match(&anchor, &name, &unit, resname, value)
 * Iteratively fetch a list of devices wired "at" something
 * res and value are restrictions.  eg: "at", "scbus0".
 * For practical purposes, res = required, value = optional.
 * *name and *unit are set.
 * set *anchor to zero before starting.
 */
int
resource_find_match(int *anchor, const char **name, int *unit,
    const char *resname, const char *value)
{
	const char *found_name;
	int found_namelen;
	int found_unit;
	int ret;
	int newln;

	newln = *anchor;
	ret = resource_find(anchor, &newln, NULL, NULL, resname, value,
	    &found_name, &found_namelen, &found_unit, NULL, NULL, NULL);
	if (ret == 0) {
		*name = resource_string_copy(found_name, found_namelen);
		*unit = found_unit;
	}
	*anchor = newln;
	return ret;
}


/*
 * err = resource_find_dev(&anchor, name, &unit, res, value);
 * Iterate through a list of devices, returning their unit numbers.
 * res and value are optional restrictions.  eg: "at", "scbus0".
 * *unit is set to the value.
 * set *anchor to zero before starting.
 */
int
resource_find_dev(int *anchor, const char *name, int *unit,
    const char *resname, const char *value)
{
	int found_unit;
	int newln;
	int ret;

	newln = *anchor;
	ret = resource_find(anchor, &newln, name, NULL, resname, value,
	    NULL, NULL, &found_unit, NULL, NULL, NULL);
	if (ret == 0) {
		*unit = found_unit;
	}
	*anchor = newln;
	return ret;
}

/*
 * Check to see if a device is disabled via a disabled hint.
 */
int
resource_disabled(const char *name, int unit)
{
	int error, value;

	error = resource_int_value(name, unit, "disabled", &value);
	if (error)
	       return (0);
	return (value);
}

/*
 * Clear a value associated with a device by removing it from
 * the kernel environment.  This only removes a hint for an
 * exact unit.
 */
int
resource_unset_value(const char *name, int unit, const char *resname)
{
	char varname[128];
	const char *retname, *retvalue;
	int error, line;
	size_t len;

	line = 0;
	error = resource_find(&line, NULL, name, &unit, resname, NULL,
	    &retname, NULL, NULL, NULL, NULL, &retvalue);
	if (error)
		return (error);

	retname -= strlen("hint.");
	len = retvalue - retname - 1;
	if (len > sizeof(varname) - 1)
		return (ENAMETOOLONG);
	memcpy(varname, retname, len);
	varname[len] = '\0';
	return (kern_unsetenv(varname));
}
