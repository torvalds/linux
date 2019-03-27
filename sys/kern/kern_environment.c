/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Michael Smith
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

/*
 * The unified bootloader passes us a pointer to a preserved copy of
 * bootstrap/kernel environment variables.  We convert them to a
 * dynamic array of strings later when the VM subsystem is up.
 *
 * We make these available through the kenv(2) syscall for userland
 * and through kern_getenv()/freeenv() kern_setenv() kern_unsetenv() testenv() for
 * the kernel.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/libkern.h>
#include <sys/kenv.h>
#include <sys/limits.h>

#include <security/mac/mac_framework.h>

static char *_getenv_dynamic_locked(const char *name, int *idx);
static char *_getenv_dynamic(const char *name, int *idx);

static MALLOC_DEFINE(M_KENV, "kenv", "kernel environment");

#define KENV_SIZE	512	/* Maximum number of environment strings */

/* pointer to the config-generated static environment */
char		*kern_envp;

/* pointer to the md-static environment */
char		*md_envp;
static int	md_env_len;
static int	md_env_pos;

static char	*kernenv_next(char *);

/* dynamic environment variables */
char		**kenvp;
struct mtx	kenv_lock;

/*
 * No need to protect this with a mutex since SYSINITS are single threaded.
 */
bool	dynamic_kenv;

#define KENV_CHECK	if (!dynamic_kenv) \
			    panic("%s: called before SI_SUB_KMEM", __func__)

int
sys_kenv(td, uap)
	struct thread *td;
	struct kenv_args /* {
		int what;
		const char *name;
		char *value;
		int len;
	} */ *uap;
{
	char *name, *value, *buffer = NULL;
	size_t len, done, needed, buflen;
	int error, i;

	KASSERT(dynamic_kenv, ("kenv: dynamic_kenv = false"));

	error = 0;
	if (uap->what == KENV_DUMP) {
#ifdef MAC
		error = mac_kenv_check_dump(td->td_ucred);
		if (error)
			return (error);
#endif
		done = needed = 0;
		buflen = uap->len;
		if (buflen > KENV_SIZE * (KENV_MNAMELEN + KENV_MVALLEN + 2))
			buflen = KENV_SIZE * (KENV_MNAMELEN +
			    KENV_MVALLEN + 2);
		if (uap->len > 0 && uap->value != NULL)
			buffer = malloc(buflen, M_TEMP, M_WAITOK|M_ZERO);
		mtx_lock(&kenv_lock);
		for (i = 0; kenvp[i] != NULL; i++) {
			len = strlen(kenvp[i]) + 1;
			needed += len;
			len = min(len, buflen - done);
			/*
			 * If called with a NULL or insufficiently large
			 * buffer, just keep computing the required size.
			 */
			if (uap->value != NULL && buffer != NULL && len > 0) {
				bcopy(kenvp[i], buffer + done, len);
				done += len;
			}
		}
		mtx_unlock(&kenv_lock);
		if (buffer != NULL) {
			error = copyout(buffer, uap->value, done);
			free(buffer, M_TEMP);
		}
		td->td_retval[0] = ((done == needed) ? 0 : needed);
		return (error);
	}

	switch (uap->what) {
	case KENV_SET:
		error = priv_check(td, PRIV_KENV_SET);
		if (error)
			return (error);
		break;

	case KENV_UNSET:
		error = priv_check(td, PRIV_KENV_UNSET);
		if (error)
			return (error);
		break;
	}

	name = malloc(KENV_MNAMELEN + 1, M_TEMP, M_WAITOK);

	error = copyinstr(uap->name, name, KENV_MNAMELEN + 1, NULL);
	if (error)
		goto done;

	switch (uap->what) {
	case KENV_GET:
#ifdef MAC
		error = mac_kenv_check_get(td->td_ucred, name);
		if (error)
			goto done;
#endif
		value = kern_getenv(name);
		if (value == NULL) {
			error = ENOENT;
			goto done;
		}
		len = strlen(value) + 1;
		if (len > uap->len)
			len = uap->len;
		error = copyout(value, uap->value, len);
		freeenv(value);
		if (error)
			goto done;
		td->td_retval[0] = len;
		break;
	case KENV_SET:
		len = uap->len;
		if (len < 1) {
			error = EINVAL;
			goto done;
		}
		if (len > KENV_MVALLEN + 1)
			len = KENV_MVALLEN + 1;
		value = malloc(len, M_TEMP, M_WAITOK);
		error = copyinstr(uap->value, value, len, NULL);
		if (error) {
			free(value, M_TEMP);
			goto done;
		}
#ifdef MAC
		error = mac_kenv_check_set(td->td_ucred, name, value);
		if (error == 0)
#endif
			kern_setenv(name, value);
		free(value, M_TEMP);
		break;
	case KENV_UNSET:
#ifdef MAC
		error = mac_kenv_check_unset(td->td_ucred, name);
		if (error)
			goto done;
#endif
		error = kern_unsetenv(name);
		if (error)
			error = ENOENT;
		break;
	default:
		error = EINVAL;
		break;
	}
done:
	free(name, M_TEMP);
	return (error);
}

/*
 * Populate the initial kernel environment.
 *
 * This is called very early in MD startup, either to provide a copy of the
 * environment obtained from a boot loader, or to provide an empty buffer into
 * which MD code can store an initial environment using kern_setenv() calls.
 *
 * kern_envp is set to the static_env generated by config(8).  This implements
 * the env keyword described in config(5).
 *
 * If len is non-zero, the caller is providing an empty buffer.  The caller will
 * subsequently use kern_setenv() to add up to len bytes of initial environment
 * before the dynamic environment is available.
 *
 * If len is zero, the caller is providing a pre-loaded buffer containing
 * environment strings.  Additional strings cannot be added until the dynamic
 * environment is available.  The memory pointed to must remain stable at least
 * until sysinit runs init_dynamic_kenv() and preferably until after SI_SUB_KMEM
 * is finished so that subr_hints routines may continue to use it until the
 * environments have been fully merged at the end of the pass.  If no initial
 * environment is available from the boot loader, passing a NULL pointer allows
 * the static_env to be installed if it is configured.  In this case, any call
 * to kern_setenv() prior to the setup of the dynamic environment will result in
 * a panic.
 */
void
init_static_kenv(char *buf, size_t len)
{
	char *eval;

	KASSERT(!dynamic_kenv, ("kenv: dynamic_kenv already initialized"));

	/*
	 * We may be called twice, with the second call needed to relocate
	 * md_envp after enabling paging.  md_envp is then garbage if it is
	 * not null and the relocation will move it.  Discard it so as to
	 * not crash using its old value in our first call to kern_getenv().
	 *
	 * The second call gives the same environment as the first except
	 * in silly configurations where the static env disables itself.
	 *
	 * Other env calls don't handle possibly-garbage pointers, so must
	 * not be made between enabling paging and calling here.
	 */
	md_envp = NULL;
	md_env_len = 0;
	md_env_pos = 0;

	/*
	 * Give the static environment a chance to disable the loader(8)
	 * environment first.  This is done with loader_env.disabled=1.
	 *
	 * static_env and static_hints may both be disabled, but in slightly
	 * different ways.  For static_env, we just don't setup kern_envp and
	 * it's as if a static env wasn't even provided.  For static_hints,
	 * we effectively zero out the buffer to stop the rest of the kernel
	 * from being able to use it.
	 *
	 * We're intentionally setting this up so that static_hints.disabled may
	 * be specified in either the MD env or the static env. This keeps us
	 * consistent in our new world view.
	 *
	 * As a warning, the static environment may not be disabled in any way
	 * if the static environment has disabled the loader environment.
	 */
	kern_envp = static_env;
	eval = kern_getenv("loader_env.disabled");
	if (eval == NULL || strcmp(eval, "1") != 0) {
		md_envp = buf;
		md_env_len = len;
		md_env_pos = 0;

		eval = kern_getenv("static_env.disabled");
		if (eval != NULL && strcmp(eval, "1") == 0) {
			kern_envp[0] = '\0';
			kern_envp[1] = '\0';
		}
	}
	eval = kern_getenv("static_hints.disabled");
	if (eval != NULL && strcmp(eval, "1") == 0) {
		static_hints[0] = '\0';
		static_hints[1] = '\0';
	}
}

static void
init_dynamic_kenv_from(char *init_env, int *curpos)
{
	char *cp, *cpnext, *eqpos, *found;
	size_t len;
	int i;

	if (init_env && *init_env != '\0') {
		found = NULL;
		i = *curpos;
		for (cp = init_env; cp != NULL; cp = cpnext) {
			cpnext = kernenv_next(cp);
			len = strlen(cp) + 1;
			if (len > KENV_MNAMELEN + 1 + KENV_MVALLEN + 1) {
				printf(
				"WARNING: too long kenv string, ignoring %s\n",
				    cp);
				goto sanitize;
			}
			eqpos = strchr(cp, '=');
			if (eqpos == NULL) {
				printf(
				"WARNING: malformed static env value, ignoring %s\n",
				    cp);
				goto sanitize;
			}
			*eqpos = 0;
			/*
			 * De-dupe the environment as we go.  We don't add the
			 * duplicated assignments because config(8) will flip
			 * the order of the static environment around to make
			 * kernel processing match the order of specification
			 * in the kernel config.
			 */
			found = _getenv_dynamic_locked(cp, NULL);
			*eqpos = '=';
			if (found != NULL)
				goto sanitize;
			if (i > KENV_SIZE) {
				printf(
				"WARNING: too many kenv strings, ignoring %s\n",
				    cp);
				goto sanitize;
			}

			kenvp[i] = malloc(len, M_KENV, M_WAITOK);
			strcpy(kenvp[i++], cp);
sanitize:
			explicit_bzero(cp, len - 1);
		}
		*curpos = i;
	}
}

/*
 * Setup the dynamic kernel environment.
 */
static void
init_dynamic_kenv(void *data __unused)
{
	int dynamic_envpos;

	kenvp = malloc((KENV_SIZE + 1) * sizeof(char *), M_KENV,
		M_WAITOK | M_ZERO);

	dynamic_envpos = 0;
	init_dynamic_kenv_from(md_envp, &dynamic_envpos);
	init_dynamic_kenv_from(kern_envp, &dynamic_envpos);
	kenvp[dynamic_envpos] = NULL;

	mtx_init(&kenv_lock, "kernel environment", NULL, MTX_DEF);
	dynamic_kenv = true;
}
SYSINIT(kenv, SI_SUB_KMEM + 1, SI_ORDER_FIRST, init_dynamic_kenv, NULL);

void
freeenv(char *env)
{

	if (dynamic_kenv && env != NULL) {
		explicit_bzero(env, strlen(env));
		free(env, M_KENV);
	}
}

/*
 * Internal functions for string lookup.
 */
static char *
_getenv_dynamic_locked(const char *name, int *idx)
{
	char *cp;
	int len, i;

	len = strlen(name);
	for (cp = kenvp[0], i = 0; cp != NULL; cp = kenvp[++i]) {
		if ((strncmp(cp, name, len) == 0) &&
		    (cp[len] == '=')) {
			if (idx != NULL)
				*idx = i;
			return (cp + len + 1);
		}
	}
	return (NULL);
}

static char *
_getenv_dynamic(const char *name, int *idx)
{

	mtx_assert(&kenv_lock, MA_OWNED);
	return (_getenv_dynamic_locked(name, idx));
}

static char *
_getenv_static_from(char *chkenv, const char *name)
{
	char *cp, *ep;
	int len;

	for (cp = chkenv; cp != NULL; cp = kernenv_next(cp)) {
		for (ep = cp; (*ep != '=') && (*ep != 0); ep++)
			;
		if (*ep != '=')
			continue;
		len = ep - cp;
		ep++;
		if (!strncmp(name, cp, len) && name[len] == 0)
			return (ep);
	}
	return (NULL);
}

static char *
_getenv_static(const char *name)
{
	char *val;

	val = _getenv_static_from(md_envp, name);
	if (val != NULL)
		return (val);
	val = _getenv_static_from(kern_envp, name);
	if (val != NULL)
		return (val);
	return (NULL);
}

/*
 * Look up an environment variable by name.
 * Return a pointer to the string if found.
 * The pointer has to be freed with freeenv()
 * after use.
 */
char *
kern_getenv(const char *name)
{
	char buf[KENV_MNAMELEN + 1 + KENV_MVALLEN + 1];
	char *ret;

	if (dynamic_kenv) {
		if (getenv_string(name, buf, sizeof(buf))) {
			ret = strdup(buf, M_KENV);
		} else {
			ret = NULL;
			WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
			    "getenv");
		}
	} else
		ret = _getenv_static(name);
	return (ret);
}

/*
 * Test if an environment variable is defined.
 */
int
testenv(const char *name)
{
	char *cp;

	if (dynamic_kenv) {
		mtx_lock(&kenv_lock);
		cp = _getenv_dynamic(name, NULL);
		mtx_unlock(&kenv_lock);
	} else
		cp = _getenv_static(name);
	if (cp != NULL)
		return (1);
	return (0);
}

/*
 * Set an environment variable in the MD-static environment.  This cannot
 * feasibly be done on config(8)-generated static environments as they don't
 * generally include space for extra variables.
 */
static int
setenv_static(const char *name, const char *value)
{
	int len;

	if (md_env_pos >= md_env_len)
		return (-1);

	/* Check space for x=y and two nuls */
	len = strlen(name) + strlen(value);
	if (len + 3 < md_env_len - md_env_pos) {
		len = sprintf(&md_envp[md_env_pos], "%s=%s", name, value);
		md_env_pos += len+1;
		md_envp[md_env_pos] = '\0';
		return (0);
	} else
		return (-1);

}

/*
 * Set an environment variable by name.
 */
int
kern_setenv(const char *name, const char *value)
{
	char *buf, *cp, *oldenv;
	int namelen, vallen, i;

	if (!dynamic_kenv && md_env_len > 0)
		return (setenv_static(name, value));

	KENV_CHECK;

	namelen = strlen(name) + 1;
	if (namelen > KENV_MNAMELEN + 1)
		return (-1);
	vallen = strlen(value) + 1;
	if (vallen > KENV_MVALLEN + 1)
		return (-1);
	buf = malloc(namelen + vallen, M_KENV, M_WAITOK);
	sprintf(buf, "%s=%s", name, value);

	mtx_lock(&kenv_lock);
	cp = _getenv_dynamic(name, &i);
	if (cp != NULL) {
		oldenv = kenvp[i];
		kenvp[i] = buf;
		mtx_unlock(&kenv_lock);
		free(oldenv, M_KENV);
	} else {
		/* We add the option if it wasn't found */
		for (i = 0; (cp = kenvp[i]) != NULL; i++)
			;

		/* Bounds checking */
		if (i < 0 || i >= KENV_SIZE) {
			free(buf, M_KENV);
			mtx_unlock(&kenv_lock);
			return (-1);
		}

		kenvp[i] = buf;
		kenvp[i + 1] = NULL;
		mtx_unlock(&kenv_lock);
	}
	return (0);
}

/*
 * Unset an environment variable string.
 */
int
kern_unsetenv(const char *name)
{
	char *cp, *oldenv;
	int i, j;

	KENV_CHECK;

	mtx_lock(&kenv_lock);
	cp = _getenv_dynamic(name, &i);
	if (cp != NULL) {
		oldenv = kenvp[i];
		for (j = i + 1; kenvp[j] != NULL; j++)
			kenvp[i++] = kenvp[j];
		kenvp[i] = NULL;
		mtx_unlock(&kenv_lock);
		explicit_bzero(oldenv, strlen(oldenv));
		free(oldenv, M_KENV);
		return (0);
	}
	mtx_unlock(&kenv_lock);
	return (-1);
}

/*
 * Return a string value from an environment variable.
 */
int
getenv_string(const char *name, char *data, int size)
{
	char *cp;

	if (dynamic_kenv) {
		mtx_lock(&kenv_lock);
		cp = _getenv_dynamic(name, NULL);
		if (cp != NULL)
			strlcpy(data, cp, size);
		mtx_unlock(&kenv_lock);
	} else {
		cp = _getenv_static(name);
		if (cp != NULL)
			strlcpy(data, cp, size);
	}
	return (cp != NULL);
}

/*
 * Return an array of integers at the given type size and signedness.
 */
int
getenv_array(const char *name, void *pdata, int size, int *psize,
    int type_size, bool allow_signed)
{
	char buf[KENV_MNAMELEN + 1 + KENV_MVALLEN + 1];
	uint8_t shift;
	int64_t value;
	int64_t old;
	char *end;
	char *ptr;
	int n;

	if (getenv_string(name, buf, sizeof(buf)) == 0)
		return (0);

	/* get maximum number of elements */
	size /= type_size;

	n = 0;

	for (ptr = buf; *ptr != 0; ) {

		value = strtoq(ptr, &end, 0);

		/* check if signed numbers are allowed */
		if (value < 0 && !allow_signed)
			goto error;

		/* check for invalid value */
		if (ptr == end)
			goto error;
		
		/* check for valid suffix */
		switch (*end) {
		case 't':
		case 'T':
			shift = 40;
			end++;
			break;
		case 'g':
		case 'G':
			shift = 30;
			end++;
			break;
		case 'm':
		case 'M':
			shift = 20;
			end++;
			break;
		case 'k':
		case 'K':
			shift = 10;
			end++;
			break;
		case ' ':
		case '\t':
		case ',':
		case 0:
			shift = 0;
			break;
		default:
			/* garbage after numeric value */
			goto error;
		}

		/* skip till next value, if any */
		while (*end == '\t' || *end == ',' || *end == ' ')
			end++;

		/* update pointer */
		ptr = end;

		/* apply shift */
		old = value;
		value <<= shift;

		/* overflow check */
		if ((value >> shift) != old)
			goto error;

		/* check for buffer overflow */
		if (n >= size)
			goto error;

		/* store value according to type size */
		switch (type_size) {
		case 1:
			if (allow_signed) {
				if (value < SCHAR_MIN || value > SCHAR_MAX)
					goto error;
			} else {
				if (value < 0 || value > UCHAR_MAX)
					goto error;
			}
			((uint8_t *)pdata)[n] = (uint8_t)value;
			break;
		case 2:
			if (allow_signed) {
				if (value < SHRT_MIN || value > SHRT_MAX)
					goto error;
			} else {
				if (value < 0 || value > USHRT_MAX)
					goto error;
			}
			((uint16_t *)pdata)[n] = (uint16_t)value;
			break;
		case 4:
			if (allow_signed) {
				if (value < INT_MIN || value > INT_MAX)
					goto error;
			} else {
				if (value > UINT_MAX)
					goto error;
			}
			((uint32_t *)pdata)[n] = (uint32_t)value;
			break;
		case 8:
			((uint64_t *)pdata)[n] = (uint64_t)value;
			break;
		default:
			goto error;
		}
		n++;
	}
	*psize = n * type_size;

	if (n != 0)
		return (1);	/* success */
error:
	return (0);	/* failure */
}

/*
 * Return an integer value from an environment variable.
 */
int
getenv_int(const char *name, int *data)
{
	quad_t tmp;
	int rval;

	rval = getenv_quad(name, &tmp);
	if (rval)
		*data = (int) tmp;
	return (rval);
}

/*
 * Return an unsigned integer value from an environment variable.
 */
int
getenv_uint(const char *name, unsigned int *data)
{
	quad_t tmp;
	int rval;

	rval = getenv_quad(name, &tmp);
	if (rval)
		*data = (unsigned int) tmp;
	return (rval);
}

/*
 * Return an int64_t value from an environment variable.
 */
int
getenv_int64(const char *name, int64_t *data)
{
	quad_t tmp;
	int64_t rval;

	rval = getenv_quad(name, &tmp);
	if (rval)
		*data = (int64_t) tmp;
	return (rval);
}

/*
 * Return an uint64_t value from an environment variable.
 */
int
getenv_uint64(const char *name, uint64_t *data)
{
	quad_t tmp;
	uint64_t rval;

	rval = getenv_quad(name, &tmp);
	if (rval)
		*data = (uint64_t) tmp;
	return (rval);
}

/*
 * Return a long value from an environment variable.
 */
int
getenv_long(const char *name, long *data)
{
	quad_t tmp;
	int rval;

	rval = getenv_quad(name, &tmp);
	if (rval)
		*data = (long) tmp;
	return (rval);
}

/*
 * Return an unsigned long value from an environment variable.
 */
int
getenv_ulong(const char *name, unsigned long *data)
{
	quad_t tmp;
	int rval;

	rval = getenv_quad(name, &tmp);
	if (rval)
		*data = (unsigned long) tmp;
	return (rval);
}

/*
 * Return a quad_t value from an environment variable.
 */
int
getenv_quad(const char *name, quad_t *data)
{
	char	value[KENV_MNAMELEN + 1 + KENV_MVALLEN + 1];
	char	*vtp;
	quad_t	iv;

	if (!getenv_string(name, value, sizeof(value)))
		return (0);
	iv = strtoq(value, &vtp, 0);
	if (vtp == value || (vtp[0] != '\0' && vtp[1] != '\0'))
		return (0);
	switch (vtp[0]) {
	case 't': case 'T':
		iv *= 1024;
		/* FALLTHROUGH */
	case 'g': case 'G':
		iv *= 1024;
		/* FALLTHROUGH */
	case 'm': case 'M':
		iv *= 1024;
		/* FALLTHROUGH */
	case 'k': case 'K':
		iv *= 1024;
	case '\0':
		break;
	default:
		return (0);
	}
	*data = iv;
	return (1);
}

/*
 * Find the next entry after the one which (cp) falls within, return a
 * pointer to its start or NULL if there are no more.
 */
static char *
kernenv_next(char *cp)
{

	if (cp != NULL) {
		while (*cp != 0)
			cp++;
		cp++;
		if (*cp == 0)
			cp = NULL;
	}
	return (cp);
}

void
tunable_int_init(void *data)
{
	struct tunable_int *d = (struct tunable_int *)data;

	TUNABLE_INT_FETCH(d->path, d->var);
}

void
tunable_long_init(void *data)
{
	struct tunable_long *d = (struct tunable_long *)data;

	TUNABLE_LONG_FETCH(d->path, d->var);
}

void
tunable_ulong_init(void *data)
{
	struct tunable_ulong *d = (struct tunable_ulong *)data;

	TUNABLE_ULONG_FETCH(d->path, d->var);
}

void
tunable_int64_init(void *data)
{
	struct tunable_int64 *d = (struct tunable_int64 *)data;

	TUNABLE_INT64_FETCH(d->path, d->var);
}

void
tunable_uint64_init(void *data)
{
	struct tunable_uint64 *d = (struct tunable_uint64 *)data;

	TUNABLE_UINT64_FETCH(d->path, d->var);
}

void
tunable_quad_init(void *data)
{
	struct tunable_quad *d = (struct tunable_quad *)data;

	TUNABLE_QUAD_FETCH(d->path, d->var);
}

void
tunable_str_init(void *data)
{
	struct tunable_str *d = (struct tunable_str *)data;

	TUNABLE_STR_FETCH(d->path, d->var, d->size);
}
