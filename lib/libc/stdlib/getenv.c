/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sean C. Farley <scf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include "namespace.h"
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"


static const char CorruptEnvFindMsg[] = "environment corrupt; unable to find ";
static const char CorruptEnvValueMsg[] =
    "environment corrupt; missing value for ";


/*
 * Standard environ.  environ variable is exposed to entire process.
 *
 *	origEnviron:	Upon cleanup on unloading of library or failure, this
 *			allows environ to return to as it was before.
 *	environSize:	Number of variables environ can hold.  Can only
 *			increase.
 *	intEnviron:	Internally-built environ.  Exposed via environ during
 *			(re)builds of the environment.
 */
extern char **environ;
static char **origEnviron;
static char **intEnviron = NULL;
static int environSize = 0;

/*
 * Array of environment variables built from environ.  Each element records:
 *	name:		Pointer to name=value string
 *	name length:	Length of name not counting '=' character
 *	value:		Pointer to value within same string as name
 *	value size:	Size (not length) of space for value not counting the
 *			nul character
 *	active state:	true/false value to signify whether variable is active.
 *			Useful since multiple variables with the same name can
 *			co-exist.  At most, one variable can be active at any
 *			one time.
 *	putenv:		Created from putenv() call.  This memory must not be
 *			reused.
 */
static struct envVars {
	size_t nameLen;
	size_t valueSize;
	char *name;
	char *value;
	bool active;
	bool putenv;
} *envVars = NULL;

/*
 * Environment array information.
 *
 *	envActive:	Number of active variables in array.
 *	envVarsSize:	Size of array.
 *	envVarsTotal:	Number of total variables in array (active or not).
 */
static int envActive = 0;
static int envVarsSize = 0;
static int envVarsTotal = 0;


/* Deinitialization of new environment. */
static void __attribute__ ((destructor)) __clean_env_destructor(void);


/*
 * A simple version of warnx() to avoid the bloat of including stdio in static
 * binaries.
 */
static void
__env_warnx(const char *msg, const char *name, size_t nameLen)
{
	static const char nl[] = "\n";
	static const char progSep[] = ": ";

	_write(STDERR_FILENO, _getprogname(), strlen(_getprogname()));
	_write(STDERR_FILENO, progSep, sizeof(progSep) - 1);
	_write(STDERR_FILENO, msg, strlen(msg));
	_write(STDERR_FILENO, name, nameLen);
	_write(STDERR_FILENO, nl, sizeof(nl) - 1);

	return;
}


/*
 * Inline strlen() for performance.  Also, perform check for an equals sign.
 * Cheaper here than peforming a strchr() later.
 */
static inline size_t
__strleneq(const char *str)
{
	const char *s;

	for (s = str; *s != '\0'; ++s)
		if (*s == '=')
			return (0);

	return (s - str);
}


/*
 * Comparison of an environment name=value to a name.
 */
static inline bool
strncmpeq(const char *nameValue, const char *name, size_t nameLen)
{
	if (strncmp(nameValue, name, nameLen) == 0 && nameValue[nameLen] == '=')
		return (true);

	return (false);
}


/*
 * Using environment, returns pointer to value associated with name, if any,
 * else NULL.  If the onlyActive flag is set to true, only variables that are
 * active are returned else all are.
 */
static inline char *
__findenv(const char *name, size_t nameLen, int *envNdx, bool onlyActive)
{
	int ndx;

	/*
	 * Find environment variable from end of array (more likely to be
	 * active).  A variable created by putenv is always active, or it is not
	 * tracked in the array.
	 */
	for (ndx = *envNdx; ndx >= 0; ndx--)
		if (envVars[ndx].putenv) {
			if (strncmpeq(envVars[ndx].name, name, nameLen)) {
				*envNdx = ndx;
				return (envVars[ndx].name + nameLen +
				    sizeof ("=") - 1);
			}
		} else if ((!onlyActive || envVars[ndx].active) &&
		    (envVars[ndx].nameLen == nameLen &&
		    strncmpeq(envVars[ndx].name, name, nameLen))) {
			*envNdx = ndx;
			return (envVars[ndx].value);
		}

	return (NULL);
}


/*
 * Using environ, returns pointer to value associated with name, if any, else
 * NULL.  Used on the original environ passed into the program.
 */
static char *
__findenv_environ(const char *name, size_t nameLen)
{
	int envNdx;

	/* Find variable within environ. */
	for (envNdx = 0; environ[envNdx] != NULL; envNdx++)
		if (strncmpeq(environ[envNdx], name, nameLen))
			return (&(environ[envNdx][nameLen + sizeof("=") - 1]));

	return (NULL);
}


/*
 * Remove variable added by putenv() from variable tracking array.
 */
static void
__remove_putenv(int envNdx)
{
	envVarsTotal--;
	if (envVarsTotal > envNdx)
		memmove(&(envVars[envNdx]), &(envVars[envNdx + 1]),
		    (envVarsTotal - envNdx) * sizeof (*envVars));
	memset(&(envVars[envVarsTotal]), 0, sizeof (*envVars));

	return;
}


/*
 * Deallocate the environment built from environ as well as environ then set
 * both to NULL.  Eases debugging of memory leaks.
 */
static void
__clean_env(bool freeVars)
{
	int envNdx;

	/* Deallocate environment and environ if created by *env(). */
	if (envVars != NULL) {
		for (envNdx = envVarsTotal - 1; envNdx >= 0; envNdx--)
			/* Free variables or deactivate them. */
			if (envVars[envNdx].putenv) {
				if (!freeVars)
					__remove_putenv(envNdx);
			} else {
				if (freeVars)
					free(envVars[envNdx].name);
				else
					envVars[envNdx].active = false;
			}
		if (freeVars) {
			free(envVars);
			envVars = NULL;
		} else
			envActive = 0;

		/* Restore original environ if it has not updated by program. */
		if (origEnviron != NULL) {
			if (environ == intEnviron)
				environ = origEnviron;
			free(intEnviron);
			intEnviron = NULL;
			environSize = 0;
		}
	}

	return;
}


/*
 * Using the environment, rebuild the environ array for use by other C library
 * calls that depend upon it.
 */
static int
__rebuild_environ(int newEnvironSize)
{
	char **tmpEnviron;
	int envNdx;
	int environNdx;
	int tmpEnvironSize;

	/* Resize environ. */
	if (newEnvironSize > environSize) {
		tmpEnvironSize = newEnvironSize * 2;
		tmpEnviron = reallocarray(intEnviron, tmpEnvironSize + 1,
		    sizeof(*intEnviron));
		if (tmpEnviron == NULL)
			return (-1);
		environSize = tmpEnvironSize;
		intEnviron = tmpEnviron;
	}
	envActive = newEnvironSize;

	/* Assign active variables to environ. */
	for (envNdx = envVarsTotal - 1, environNdx = 0; envNdx >= 0; envNdx--)
		if (envVars[envNdx].active)
			intEnviron[environNdx++] = envVars[envNdx].name;
	intEnviron[environNdx] = NULL;

	/* Always set environ which may have been replaced by program. */
	environ = intEnviron;

	return (0);
}


/*
 * Enlarge new environment.
 */
static inline bool
__enlarge_env(void)
{
	int newEnvVarsSize;
	struct envVars *tmpEnvVars;

	envVarsTotal++;
	if (envVarsTotal > envVarsSize) {
		newEnvVarsSize = envVarsTotal * 2;
		tmpEnvVars = reallocarray(envVars, newEnvVarsSize,
		    sizeof(*envVars));
		if (tmpEnvVars == NULL) {
			envVarsTotal--;
			return (false);
		}
		envVarsSize = newEnvVarsSize;
		envVars = tmpEnvVars;
	}

	return (true);
}


/*
 * Using environ, build an environment for use by standard C library calls.
 */
static int
__build_env(void)
{
	char **env;
	int activeNdx;
	int envNdx;
	int savedErrno;
	size_t nameLen;

	/* Check for non-existant environment. */
	if (environ == NULL || environ[0] == NULL)
		return (0);

	/* Count environment variables. */
	for (env = environ, envVarsTotal = 0; *env != NULL; env++)
		envVarsTotal++;
	envVarsSize = envVarsTotal * 2;

	/* Create new environment. */
	envVars = calloc(envVarsSize, sizeof(*envVars));
	if (envVars == NULL)
		goto Failure;

	/* Copy environ values and keep track of them. */
	for (envNdx = envVarsTotal - 1; envNdx >= 0; envNdx--) {
		envVars[envNdx].putenv = false;
		envVars[envNdx].name =
		    strdup(environ[envVarsTotal - envNdx - 1]);
		if (envVars[envNdx].name == NULL)
			goto Failure;
		envVars[envNdx].value = strchr(envVars[envNdx].name, '=');
		if (envVars[envNdx].value != NULL) {
			envVars[envNdx].value++;
			envVars[envNdx].valueSize =
			    strlen(envVars[envNdx].value);
		} else {
			__env_warnx(CorruptEnvValueMsg, envVars[envNdx].name,
			    strlen(envVars[envNdx].name));
			errno = EFAULT;
			goto Failure;
		}

		/*
		 * Find most current version of variable to make active.  This
		 * will prevent multiple active variables from being created
		 * during this initialization phase.
		 */
		nameLen = envVars[envNdx].value - envVars[envNdx].name - 1;
		envVars[envNdx].nameLen = nameLen;
		activeNdx = envVarsTotal - 1;
		if (__findenv(envVars[envNdx].name, nameLen, &activeNdx,
		    false) == NULL) {
			__env_warnx(CorruptEnvFindMsg, envVars[envNdx].name,
			    nameLen);
			errno = EFAULT;
			goto Failure;
		}
		envVars[activeNdx].active = true;
	}

	/* Create a new environ. */
	origEnviron = environ;
	environ = NULL;
	if (__rebuild_environ(envVarsTotal) == 0)
		return (0);

Failure:
	savedErrno = errno;
	__clean_env(true);
	errno = savedErrno;

	return (-1);
}


/*
 * Destructor function with default argument to __clean_env().
 */
static void
__clean_env_destructor(void)
{
	__clean_env(true);

	return;
}


/*
 * Returns the value of a variable or NULL if none are found.
 */
char *
getenv(const char *name)
{
	int envNdx;
	size_t nameLen;

	/* Check for malformed name. */
	if (name == NULL || (nameLen = __strleneq(name)) == 0) {
		errno = EINVAL;
		return (NULL);
	}

	/*
	 * Variable search order:
	 * 1. Check for an empty environ.  This allows an application to clear
	 *    the environment.
	 * 2. Search the external environ array.
	 * 3. Search the internal environment.
	 *
	 * Since malloc() depends upon getenv(), getenv() must never cause the
	 * internal environment storage to be generated.
	 */
	if (environ == NULL || environ[0] == NULL)
		return (NULL);
	else if (envVars == NULL || environ != intEnviron)
		return (__findenv_environ(name, nameLen));
	else {
		envNdx = envVarsTotal - 1;
		return (__findenv(name, nameLen, &envNdx, true));
	}
}


/*
 * Set the value of a variable.  Older settings are labeled as inactive.  If an
 * older setting has enough room to store the new value, it will be reused.  No
 * previous variables are ever freed here to avoid causing a segmentation fault
 * in a user's code.
 *
 * The variables nameLen and valueLen are passed into here to allow the caller
 * to calculate the length by means besides just strlen().
 */
static int
__setenv(const char *name, size_t nameLen, const char *value, int overwrite)
{
	bool reuse;
	char *env;
	int envNdx;
	int newEnvActive;
	size_t valueLen;

	/* Find existing environment variable large enough to use. */
	envNdx = envVarsTotal - 1;
	newEnvActive = envActive;
	valueLen = strlen(value);
	reuse = false;
	if (__findenv(name, nameLen, &envNdx, false) != NULL) {
		/* Deactivate entry if overwrite is allowed. */
		if (envVars[envNdx].active) {
			if (overwrite == 0)
				return (0);
			envVars[envNdx].active = false;
			newEnvActive--;
		}

		/* putenv() created variable cannot be reused. */
		if (envVars[envNdx].putenv)
			__remove_putenv(envNdx);

		/* Entry is large enough to reuse. */
		else if (envVars[envNdx].valueSize >= valueLen)
			reuse = true;
	}

	/* Create new variable if none was found of sufficient size. */
	if (! reuse) {
		/* Enlarge environment. */
		envNdx = envVarsTotal;
		if (!__enlarge_env())
			return (-1);

		/* Create environment entry. */
		envVars[envNdx].name = malloc(nameLen + sizeof ("=") +
		    valueLen);
		if (envVars[envNdx].name == NULL) {
			envVarsTotal--;
			return (-1);
		}
		envVars[envNdx].nameLen = nameLen;
		envVars[envNdx].valueSize = valueLen;

		/* Save name of name/value pair. */
		env = stpncpy(envVars[envNdx].name, name, nameLen);
		*env++ = '=';
	}
	else
		env = envVars[envNdx].value;

	/* Save value of name/value pair. */
	strcpy(env, value);
	envVars[envNdx].value = env;
	envVars[envNdx].active = true;
	newEnvActive++;

	/* No need to rebuild environ if an active variable was reused. */
	if (reuse && newEnvActive == envActive)
		return (0);
	else
		return (__rebuild_environ(newEnvActive));
}


/*
 * If the program attempts to replace the array of environment variables
 * (environ) environ or sets the first varible to NULL, then deactivate all
 * variables and merge in the new list from environ.
 */
static int
__merge_environ(void)
{
	char **env;
	char *equals;

	/*
	 * Internally-built environ has been replaced or cleared (detected by
	 * using the count of active variables against a NULL as the first value
	 * in environ).  Clean up everything.
	 */
	if (intEnviron != NULL && (environ != intEnviron || (envActive > 0 &&
	    environ[0] == NULL))) {
		/* Deactivate all environment variables. */
		if (envActive > 0) {
			origEnviron = NULL;
			__clean_env(false);
		}

		/*
		 * Insert new environ into existing, yet deactivated,
		 * environment array.
		 */
		origEnviron = environ;
		if (origEnviron != NULL)
			for (env = origEnviron; *env != NULL; env++) {
				if ((equals = strchr(*env, '=')) == NULL) {
					__env_warnx(CorruptEnvValueMsg, *env,
					    strlen(*env));
					errno = EFAULT;
					return (-1);
				}
				if (__setenv(*env, equals - *env, equals + 1,
				    1) == -1)
					return (-1);
			}
	}

	return (0);
}


/*
 * The exposed setenv() that peforms a few tests before calling the function
 * (__setenv()) that does the actual work of inserting a variable into the
 * environment.
 */
int
setenv(const char *name, const char *value, int overwrite)
{
	size_t nameLen;

	/* Check for malformed name. */
	if (name == NULL || (nameLen = __strleneq(name)) == 0) {
		errno = EINVAL;
		return (-1);
	}

	/* Initialize environment. */
	if (__merge_environ() == -1 || (envVars == NULL && __build_env() == -1))
		return (-1);

	return (__setenv(name, nameLen, value, overwrite));
}


/*
 * Insert a "name=value" string into the environment.  Special settings must be
 * made to keep setenv() from reusing this memory block and unsetenv() from
 * allowing it to be tracked.
 */
int
putenv(char *string)
{
	char *equals;
	int envNdx;
	int newEnvActive;
	size_t nameLen;

	/* Check for malformed argument. */
	if (string == NULL || (equals = strchr(string, '=')) == NULL ||
	    (nameLen = equals - string) == 0) {
		errno = EINVAL;
		return (-1);
	}

	/* Initialize environment. */
	if (__merge_environ() == -1 || (envVars == NULL && __build_env() == -1))
		return (-1);

	/* Deactivate previous environment variable. */
	envNdx = envVarsTotal - 1;
	newEnvActive = envActive;
	if (__findenv(string, nameLen, &envNdx, true) != NULL) {
		/* Reuse previous putenv slot. */
		if (envVars[envNdx].putenv) {
			envVars[envNdx].name = string;
			return (__rebuild_environ(envActive));
		} else {
			newEnvActive--;
			envVars[envNdx].active = false;
		}
	}

	/* Enlarge environment. */
	envNdx = envVarsTotal;
	if (!__enlarge_env())
		return (-1);

	/* Create environment entry. */
	envVars[envNdx].name = string;
	envVars[envNdx].nameLen = -1;
	envVars[envNdx].value = NULL;
	envVars[envNdx].valueSize = -1;
	envVars[envNdx].putenv = true;
	envVars[envNdx].active = true;
	newEnvActive++;

	return (__rebuild_environ(newEnvActive));
}


/*
 * Unset variable with the same name by flagging it as inactive.  No variable is
 * ever freed.
 */
int
unsetenv(const char *name)
{
	int envNdx;
	size_t nameLen;
	int newEnvActive;

	/* Check for malformed name. */
	if (name == NULL || (nameLen = __strleneq(name)) == 0) {
		errno = EINVAL;
		return (-1);
	}

	/* Initialize environment. */
	if (__merge_environ() == -1 || (envVars == NULL && __build_env() == -1))
		return (-1);

	/* Deactivate specified variable. */
	/* Remove all occurrences. */
	envNdx = envVarsTotal - 1;
	newEnvActive = envActive;
	while (__findenv(name, nameLen, &envNdx, true) != NULL) {
		envVars[envNdx].active = false;
		if (envVars[envNdx].putenv)
			__remove_putenv(envNdx);
		envNdx--;
		newEnvActive--;
	}
	if (newEnvActive != envActive)
		__rebuild_environ(newEnvActive);

	return (0);
}
