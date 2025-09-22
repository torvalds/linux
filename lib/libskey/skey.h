/*
 * OpenBSD S/Key (skey.h)
 *
 * Authors:
 *          Neil M. Haller <nmh@thumper.bellcore.com>
 *          Philip R. Karn <karn@chicago.qualcomm.com>
 *          John S. Walden <jsw@thumper.bellcore.com>
 *          Scott Chasin <chasin@crimelab.com>
 *          Todd C. Miller <millert@openbsd.org>
 *
 * Main client header
 *
 * $OpenBSD: skey.h,v 1.23 2024/05/21 11:13:08 jsg Exp $
 */

#ifndef _SKEY_H_
#define _SKEY_H_ 1

#include <dirent.h>

/* Server-side data structure for reading keys file during login */
struct skey {
	FILE *keyfile;
	DIR  *keydir;
	char *logname;
	char *seed;
	char *val;
	unsigned int n;
	char buf[256];
};

/* Client-side structure for scanning data stream for challenge */
struct mc {
	int skip;
	int cnt;
	char buf[256];
};

/* Maximum sequence number we allow */
#define SKEY_MAX_SEQ		10000

/* Minimum secret password length (rfc2289) */
#define SKEY_MIN_PW_LEN		10

/* Max secret password length (rfc2289 says 63 but allows more) */
#define SKEY_MAX_PW_LEN		255

/* Max length of an S/Key seed (rfc2289) */
#define SKEY_MAX_SEED_LEN	16

/* Max length of S/Key challenge (otp-???? 9999 seed) */
#define SKEY_MAX_CHALLENGE	(11 + SKEY_MAX_HASHNAME_LEN + SKEY_MAX_SEED_LEN)

/* Max length of hash algorithm name (md5/sha1/rmd160) */
#define SKEY_MAX_HASHNAME_LEN	6

/* Size of a binary key (not NULL-terminated) */
#define SKEY_BINKEY_SIZE	8

/* Directory for S/Key per-user files */
#define _PATH_SKEYDIR		"/etc/skey"

__BEGIN_DECLS
void f(char *);
int keycrunch(char *, char *, char *);
char *btoe(char *, char *);
char *put8(char *, char *);
int etob(char *, char *);
void rip(char *);
int skeychallenge(struct skey *, char *, char *);
int skeychallenge2(int, struct skey *, char *, char *);
int skeylookup(struct skey *, char *);
int skeyverify(struct skey *, char *);
void sevenbit(char *);
void backspace(char *);
char *skipspace(char *);
char *readpass(char *, int);
char *readskey(char *, int);
int skey_authenticate(char *);
int skey_passcheck(char *, char *);
char *skey_keyinfo(char *);
int skey_haskey(char *);
int atob8(char *, char *);
int btoa8(char *, char *);
int htoi(int);
const char *skey_get_algorithm(void);
char *skey_set_algorithm(char *);
int skeygetnext(struct skey *);
int skey_unlock(struct skey *);
__END_DECLS

#endif /* _SKEY_H_ */
