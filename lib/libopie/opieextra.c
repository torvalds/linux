/*
 * This file contains routines modified from OpenBSD. Parts are contributed
 * by Todd Miller <millert@openbsd.org>, Theo De Raadt <deraadt@openbsd.org>
 * and possibly others.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <stdio.h>
#include <opie.h>

/*
 * opie_haopie()
 *
 * Returns: 1 user doesnt exist, -1 file error, 0 user exists.
 *
 */
int
opie_haskey(username)
char *username;
{
	struct opie opie;
 
	return opielookup(&opie, username);
}
 
/*
 * opie_keyinfo()
 *
 * Returns the current sequence number and
 * seed for the passed user.
 *
 */
char *
opie_keyinfo(username)
char *username;
{
	int i;
	static char str[OPIE_CHALLENGE_MAX];
	struct opie opie;

	i = opiechallenge(&opie, username, str);
	if (i == -1)
		return(0);

	return(str);
}
 
/*
 * opie_passverify()
 *
 * Check to see if answer is the correct one to the current
 * challenge.
 *
 * Returns: 0 success, -1 failure
 *
 */
int
opie_passverify(username, passwd)
char *username;
char *passwd;
{
	int i;
	struct opie opie;

	i = opielookup(&opie, username);
	if (i == -1 || i == 1)
		return(-1);

	if (opieverify(&opie, passwd) == 0)
		return(opie.opie_n);

	return(-1);
}

#define OPIE_HASH_DEFAULT	1

/* Current hash type (index into opie_hash_types array) */
static int opie_hash_type = OPIE_HASH_DEFAULT;

struct opie_algorithm_table {
	const char *name;
};

static struct opie_algorithm_table opie_algorithm_table[] = {
	"md4", "md5"
};

/* Get current hash type */
const char *
opie_get_algorithm()
{
	return(opie_algorithm_table[opie_hash_type].name);
}


