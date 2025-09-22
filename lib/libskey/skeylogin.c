/* OpenBSD S/Key (skeylogin.c)
 *
 * Authors:
 *          Neil M. Haller <nmh@thumper.bellcore.com>
 *          Philip R. Karn <karn@chicago.qualcomm.com>
 *          John S. Walden <jsw@thumper.bellcore.com>
 *          Scott Chasin <chasin@crimelab.com>
 *          Todd C. Miller <millert@openbsd.org>
 *	    Angelos D. Keromytis <adk@adk.gr>
 *
 * S/Key verification check, lookups, and authentication.
 *
 * $OpenBSD: skeylogin.c,v 1.65 2024/03/23 16:30:01 guenther Exp $
 */

#ifdef	QUOTA
#include <sys/quota.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <sha1.h>

#include "skey.h"

static void skey_fakeprompt(char *, char *);
static char *tgetline(int, char *, size_t, int);
static int skeygetent(int, struct skey *, const char *);

/*
 * Return an skey challenge string for user 'name'. If successful,
 * fill in the caller's skey structure and return (0). If unsuccessful
 * (e.g., if name is unknown) return (-1).
 *
 * The file read/write pointer is left at the start of the record.
 */
int
skeychallenge2(int fd, struct skey *mp, char *name, char *ss)
{
	int rval;

	memset(mp, 0, sizeof(*mp));
	rval = skeygetent(fd, mp, name);

	switch (rval) {
	case 0:		/* Lookup succeeded, return challenge */
		(void)snprintf(ss, SKEY_MAX_CHALLENGE,
		    "otp-%.*s %d %.*s", SKEY_MAX_HASHNAME_LEN,
		    skey_get_algorithm(), mp->n - 1,
		    SKEY_MAX_SEED_LEN, mp->seed);
		return (0);

	case 1:		/* User not found */
		if (mp->keyfile) {
			(void)fclose(mp->keyfile);
			mp->keyfile = NULL;
		}
		/* FALLTHROUGH */

	default:	/* File error */
		skey_fakeprompt(name, ss);
		return (-1);
	}
}

int
skeychallenge(struct skey *mp, char *name, char *ss)
{
	return (skeychallenge2(-1, mp, name, ss));
}

/*
 * Get an entry in the One-time Password database and lock it.
 *
 * Return codes:
 * -1: error in opening database or unable to lock entry
 *  0: entry found, file R/W pointer positioned at beginning of record
 *  1: entry not found
 */
static int
skeygetent(int fd, struct skey *mp, const char *name)
{
	char *cp, filename[PATH_MAX], *last;
	struct stat statbuf;
	const char *errstr;
	size_t nread;
	FILE *keyfile;

	/* Check to see that /etc/skey has not been disabled. */
	if (stat(_PATH_SKEYDIR, &statbuf) != 0)
		return (-1);
	if ((statbuf.st_mode & ALLPERMS) == 0) {
		errno = EPERM;
		return (-1);
	}

	if (fd == -1) {
		/* Open the user's database entry, creating it as needed. */
		if (snprintf(filename, sizeof(filename), "%s/%s", _PATH_SKEYDIR,
		    name) >= sizeof(filename)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		if ((fd = open(filename, O_RDWR | O_NOFOLLOW | O_NONBLOCK,
		    S_IRUSR | S_IWUSR)) == -1) {
			if (errno == ENOENT)
				goto not_found;
			return (-1);
		}
	}

	/* Lock and stat the user's skey file. */
	if (flock(fd, LOCK_EX) != 0 || fstat(fd, &statbuf) != 0) {
		close(fd);
		return (-1);
	}
	if (statbuf.st_size == 0)
		goto not_found;

	/* Sanity checks. */
	if ((statbuf.st_mode & ALLPERMS) != (S_IRUSR | S_IWUSR) ||
	    !S_ISREG(statbuf.st_mode) || statbuf.st_nlink != 1 ||
	    (keyfile = fdopen(fd, "r+")) == NULL) {
		close(fd);
		return (-1);
	}

	/* At this point, we are committed. */
	mp->keyfile = keyfile;

	if ((nread = fread(mp->buf, 1, sizeof(mp->buf), keyfile)) == 0 ||
	    !isspace((unsigned char)mp->buf[nread - 1]))
		goto bad_keyfile;
	mp->buf[nread - 1] = '\0';

	if ((mp->logname = strtok_r(mp->buf, " \t\n\r", &last)) == NULL ||
	    strcmp(mp->logname, name) != 0)
		goto bad_keyfile;
	if ((cp = strtok_r(NULL, " \t\n\r", &last)) == NULL)
		goto bad_keyfile;
	if (skey_set_algorithm(cp) == NULL)
		goto bad_keyfile;
	if ((cp = strtok_r(NULL, " \t\n\r", &last)) == NULL)
		goto bad_keyfile;
	mp->n = strtonum(cp, 0, UINT_MAX, &errstr);
	if (errstr)
		goto bad_keyfile;
	if ((mp->seed = strtok_r(NULL, " \t\n\r", &last)) == NULL)
		goto bad_keyfile;
	if ((mp->val = strtok_r(NULL, " \t\n\r", &last)) == NULL)
		goto bad_keyfile;

	(void)fseek(keyfile, 0L, SEEK_SET);
	return (0);

    bad_keyfile:
	fclose(keyfile);
	return (-1);

    not_found:
	/* No existing entry, fill in what we can and return */
	memset(mp, 0, sizeof(*mp));
	strlcpy(mp->buf, name, sizeof(mp->buf));
	mp->logname = mp->buf;
	if (fd != -1)
		close(fd);
	return (1);
}

/*
 * Look up an entry in the One-time Password database and lock it.
 * Zeroes out the passed in struct skey before using it.
 *
 * Return codes:
 * -1: error in opening database or unable to lock entry
 *  0: entry found, file R/W pointer positioned at beginning of record
 *  1: entry not found
 */
int
skeylookup(struct skey *mp, char *name)
{
	memset(mp, 0, sizeof(*mp));
	return (skeygetent(-1, mp, name));
}

/*
 * Get the next entry in the One-time Password database.
 *
 * Return codes:
 * -1: error in opening database
 *  0: next entry found and stored in mp
 *  1: no more entries, keydir is closed.
 */
int
skeygetnext(struct skey *mp)
{
	struct dirent *dp;
	int rval;

	if (mp->keyfile != NULL) {
		fclose(mp->keyfile);
		mp->keyfile = NULL;
	}

	/* Open _PATH_SKEYDIR if it exists, else return an error */
	if (mp->keydir == NULL && (mp->keydir = opendir(_PATH_SKEYDIR)) == NULL)
		return (-1);

	rval = 1;
	while ((dp = readdir(mp->keydir)) != NULL) {
		/* Skip dot files and zero-length files. */
		if (dp->d_name[0] != '.' &&
		    (rval = skeygetent(-1, mp, dp->d_name)) != 1)
			break;
	}

	if (dp == NULL) {
		closedir(mp->keydir);
		mp->keydir = NULL;
	}

	return (rval);
}

/*
 * Verify response to a S/Key challenge.
 *
 * Return codes:
 * -1: Error of some sort; database unchanged
 *  0:  Verify successful, database updated
 *  1:  Verify failed, database unchanged
 *
 * The database file is always closed by this call.
 */
int
skeyverify(struct skey *mp, char *response)
{
	char key[SKEY_BINKEY_SIZE], fkey[SKEY_BINKEY_SIZE];
	char filekey[SKEY_BINKEY_SIZE], *cp, *last;
	size_t nread;

	if (response == NULL)
		goto verify_failure;

	/*
	 * The record should already be locked but lock it again
	 * just to be safe.  We don't wait for the lock to become
	 * available since we should already have it...
	 */
	if (flock(fileno(mp->keyfile), LOCK_EX | LOCK_NB) != 0)
		goto verify_failure;

	/* Convert response to binary */
	rip(response);
	if (etob(key, response) != 1 && atob8(key, response) != 0)
		goto verify_failure; /* Neither english words nor ascii hex */

	/* Compute fkey = f(key) */
	(void)memcpy(fkey, key, sizeof(key));
	f(fkey);

	/*
	 * Reread the file record NOW in case it has been modified.
	 * The only field we really need to worry about is mp->val.
	 */
	(void)fseek(mp->keyfile, 0L, SEEK_SET);
	if ((nread = fread(mp->buf, 1, sizeof(mp->buf), mp->keyfile)) == 0 ||
	    !isspace((unsigned char)mp->buf[nread - 1]))
		goto verify_failure;
	if ((mp->logname = strtok_r(mp->buf, " \t\r\n", &last)) == NULL)
		goto verify_failure;
	if ((cp = strtok_r(NULL, " \t\r\n", &last)) == NULL)
		goto verify_failure;
	if ((cp = strtok_r(NULL, " \t\r\n", &last)) == NULL)
		goto verify_failure;
	if ((mp->seed = strtok_r(NULL, " \t\r\n", &last)) == NULL)
		goto verify_failure;
	if ((mp->val = strtok_r(NULL, " \t\r\n", &last)) == NULL)
		goto verify_failure;

	/* Convert file value to hex and compare. */
	atob8(filekey, mp->val);
	if (memcmp(filekey, fkey, SKEY_BINKEY_SIZE) != 0)
		goto verify_failure;	/* Wrong response */

	/*
	 * Update key in database.
	 * XXX - check return values of things that write to disk.
	 */
	btoa8(mp->val,key);
	mp->n--;
	(void)fseek(mp->keyfile, 0L, SEEK_SET);
	(void)fprintf(mp->keyfile, "%s\n%s\n%d\n%s\n%s\n", mp->logname,
	    skey_get_algorithm(), mp->n, mp->seed, mp->val);
	(void)fflush(mp->keyfile);
	(void)ftruncate(fileno(mp->keyfile), ftello(mp->keyfile));
	(void)fclose(mp->keyfile);
	mp->keyfile = NULL;
	return (0);

    verify_failure:
	(void)fclose(mp->keyfile);
	mp->keyfile = NULL;
	return (-1);
}

/*
 * skey_haskey()
 *
 * Returns: 1 user doesn't exist, -1 file error, 0 user exists.
 *
 */
int
skey_haskey(char *username)
{
	struct skey skey;
	int i;

	i = skeylookup(&skey, username);
	if (skey.keyfile != NULL) {
		fclose(skey.keyfile);
		skey.keyfile = NULL;
	}
	return (i);
}

/*
 * skey_keyinfo()
 *
 * Returns the current sequence number and
 * seed for the passed user.
 *
 */
char *
skey_keyinfo(char *username)
{
	static char str[SKEY_MAX_CHALLENGE];
	struct skey skey;
	int i;

	i = skeychallenge(&skey, username, str);
	if (i == -1)
		return (0);

	if (skey.keyfile != NULL) {
		fclose(skey.keyfile);
		skey.keyfile = NULL;
	}
	return (str);
}

/*
 * skey_passcheck()
 *
 * Check to see if answer is the correct one to the current
 * challenge.
 *
 * Returns: 0 success, -1 failure
 *
 */
int
skey_passcheck(char *username, char *passwd)
{
	struct skey skey;
	int i;

	i = skeylookup(&skey, username);
	if (i == -1 || i == 1)
		return (-1);

	if (skeyverify(&skey, passwd) == 0)
		return (skey.n);

	return (-1);
}

#define ROUND(x)   (((x)[0] << 24) + (((x)[1]) << 16) + (((x)[2]) << 8) + \
		    ((x)[3]))

/*
 * hash_collapse()
 */
static u_int32_t
hash_collapse(u_char *s)
{
	int len, target;
	u_int32_t i;

	if ((strlen(s) % sizeof(u_int32_t)) == 0)
		target = strlen(s);    /* Multiple of 4 */
	else
		target = strlen(s) - (strlen(s) % sizeof(u_int32_t));

	for (i = 0, len = 0; len < target; len += 4)
		i ^= ROUND(s + len);

	return i;
}

/*
 * skey_fakeprompt()
 *
 * Generate a fake prompt for the specified user.
 *
 */
static void
skey_fakeprompt(char *username, char *skeyprompt)
{
	char secret[SKEY_MAX_SEED_LEN], pbuf[SKEY_MAX_PW_LEN+1], *p, *u;
	u_char *up;
	SHA1_CTX ctx;
	u_int ptr;
	int i;

	/*
	 * Base first 4 chars of seed on hostname.
	 * Add some filler for short hostnames if necessary.
	 */
	if (gethostname(pbuf, sizeof(pbuf)) == -1)
		*(p = pbuf) = '.';
	else
		for (p = pbuf; isalnum((unsigned char)*p); p++)
			if (isalpha((unsigned char)*p) &&
			    isupper((unsigned char)*p))
				*p = (char)tolower((unsigned char)*p);
	if (*p && p - pbuf < 4)
		(void)strncpy(p, "asjd", 4 - (p - pbuf));
	pbuf[4] = '\0';

	/* Hash the username if possible */
	if ((up = SHA1Data(username, strlen(username), NULL)) != NULL) {
		/* Collapse the hash */
		ptr = hash_collapse(up);
		explicit_bzero(up, strlen(up));

		/* Put that in your pipe and smoke it */
		arc4random_buf(secret, sizeof(secret));

		/* Hash secret value with username */
		SHA1Init(&ctx);
		SHA1Update(&ctx, secret, sizeof(secret));
		SHA1Update(&ctx, username, strlen(username));
		SHA1End(&ctx, up);

		/* Zero out */
		explicit_bzero(secret, sizeof(secret));

		/* Now hash the hash */
		SHA1Init(&ctx);
		SHA1Update(&ctx, up, strlen(up));
		SHA1End(&ctx, up);

		ptr = hash_collapse(up + 4);

		for (i = 4; i < 9; i++) {
			pbuf[i] = (ptr % 10) + '0';
			ptr /= 10;
		}
		pbuf[i] = '\0';

		/* Sequence number */
		ptr = ((up[2] + up[3]) % 99) + 1;

		freezero(up, 20); /* SHA1 specific */

		(void)snprintf(skeyprompt, SKEY_MAX_CHALLENGE,
		    "otp-%.*s %d %.*s", SKEY_MAX_HASHNAME_LEN,
		    skey_get_algorithm(), ptr, SKEY_MAX_SEED_LEN, pbuf);
	} else {
		/* Base last 8 chars of seed on username */
		u = username;
		i = 8;
		p = &pbuf[4];
		do {
			if (*u == 0) {
				/* Pad remainder with zeros */
				while (--i >= 0)
					*p++ = '0';
				break;
			}

			*p++ = (*u++ % 10) + '0';
		} while (--i != 0);
		pbuf[12] = '\0';

		(void)snprintf(skeyprompt, SKEY_MAX_CHALLENGE,
		    "otp-%.*s %d %.*s", SKEY_MAX_HASHNAME_LEN,
		    skey_get_algorithm(), 99, SKEY_MAX_SEED_LEN, pbuf);
	}
}

/*
 * skey_authenticate()
 *
 * Used when calling program will allow input of the user's
 * response to the challenge.
 *
 * Returns: 0 success, -1 failure
 *
 */
int
skey_authenticate(char *username)
{
	char pbuf[SKEY_MAX_PW_LEN+1], skeyprompt[SKEY_MAX_CHALLENGE+1];
	struct skey skey;
	int i;

	/* Get the S/Key challenge (may be fake) */
	i = skeychallenge(&skey, username, skeyprompt);
	(void)fprintf(stderr, "%s\nResponse: ", skeyprompt);
	(void)fflush(stderr);

	/* Time out on user input after 2 minutes */
	tgetline(fileno(stdin), pbuf, sizeof(pbuf), 120);
	sevenbit(pbuf);
	(void)rewind(stdin);

	/* Is it a valid response? */
	if (i == 0 && skeyverify(&skey, pbuf) == 0) {
		if (skey.n < 5) {
			(void)fprintf(stderr,
			    "\nWarning! Key initialization needed soon.  (%d logins left)\n",
			    skey.n);
		}
		return (0);
	}
	return (-1);
}

/*
 * Unlock current entry in the One-time Password database.
 *
 * Return codes:
 * -1: unable to lock the record
 *  0: record was successfully unlocked
 */
int
skey_unlock(struct skey *mp)
{
	if (mp->logname == NULL || mp->keyfile == NULL)
		return (-1);

	return (flock(fileno(mp->keyfile), LOCK_UN));
}

/*
 * Get a line of input (optionally timing out) and place it in buf.
 */
static char *
tgetline(int fd, char *buf, size_t bufsiz, int timeout)
{
	struct pollfd pfd[1];
	size_t left;
	char c, *cp;
	ssize_t ss;
	int n;

	if (bufsiz == 0)
		return (NULL);			/* sanity */

	cp = buf;
	left = bufsiz;

	/*
	 * Timeout of <= 0 means no timeout.
	 */
	if (timeout > 0) {
		timeout *= 1000;		/* convert to milliseconds */

		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		while (--left) {
			/* Poll until we are ready or we time out */
			while ((n = poll(pfd, 1, timeout)) == -1 &&
			    (errno == EINTR || errno == EAGAIN))
				;
			if (n <= 0 ||
			    (pfd[0].revents & (POLLERR|POLLHUP|POLLNVAL)))
				break;		/* timeout or error */

			/* Read a character, exit loop on error, EOF or EOL */
			ss = read(fd, &c, 1);
			if (ss != 1 || c == '\n' || c == '\r')
				break;
			*cp++ = c;
		}
	} else {
		/* Keep reading until out of space, EOF, error, or newline */
		while (--left && read(fd, &c, 1) == 1 && c != '\n' && c != '\r')
			*cp++ = c;
	}
	*cp = '\0';

	return (cp == buf ? NULL : buf);
}
