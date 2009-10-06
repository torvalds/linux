/*
 * Some non-inline ceph helpers
 */
#include "types.h"

int ceph_flags_to_mode(int flags)
{
#ifdef O_DIRECTORY  /* fixme */
	if ((flags & O_DIRECTORY) == O_DIRECTORY)
		return CEPH_FILE_MODE_PIN;
#endif
#ifdef O_LAZY
	if (flags & O_LAZY)
		return CEPH_FILE_MODE_LAZY;
#endif
	if ((flags & O_APPEND) == O_APPEND)
		flags |= O_WRONLY;

	flags &= O_ACCMODE;
	if ((flags & O_RDWR) == O_RDWR)
		return CEPH_FILE_MODE_RDWR;
	if ((flags & O_WRONLY) == O_WRONLY)
		return CEPH_FILE_MODE_WR;
	return CEPH_FILE_MODE_RD;
}

int ceph_caps_for_mode(int mode)
{
	switch (mode) {
	case CEPH_FILE_MODE_PIN:
		return CEPH_CAP_PIN;
	case CEPH_FILE_MODE_RD:
		return CEPH_CAP_PIN | CEPH_CAP_FILE_SHARED |
			CEPH_CAP_FILE_RD | CEPH_CAP_FILE_CACHE;
	case CEPH_FILE_MODE_RDWR:
		return CEPH_CAP_PIN | CEPH_CAP_FILE_SHARED |
			CEPH_CAP_FILE_EXCL |
			CEPH_CAP_FILE_RD | CEPH_CAP_FILE_CACHE |
			CEPH_CAP_FILE_WR | CEPH_CAP_FILE_BUFFER |
			CEPH_CAP_AUTH_SHARED | CEPH_CAP_AUTH_EXCL |
			CEPH_CAP_XATTR_SHARED | CEPH_CAP_XATTR_EXCL;
	case CEPH_FILE_MODE_WR:
		return CEPH_CAP_PIN | CEPH_CAP_FILE_SHARED |
			CEPH_CAP_FILE_EXCL |
			CEPH_CAP_FILE_WR | CEPH_CAP_FILE_BUFFER |
			CEPH_CAP_AUTH_SHARED | CEPH_CAP_AUTH_EXCL |
			CEPH_CAP_XATTR_SHARED | CEPH_CAP_XATTR_EXCL;
	}
	return 0;
}

/* Name hashing routines. Initial hash value */
/* Hash courtesy of the R5 hash in reiserfs modulo sign bits */
#define ceph_init_name_hash()		0

/* partial hash update function. Assume roughly 4 bits per character */
static unsigned long ceph_partial_name_hash(unsigned long c,
					    unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/*
 * Finally: cut down the number of bits to a int value (and try to avoid
 * losing bits)
 */
static unsigned long ceph_end_name_hash(unsigned long hash)
{
	return hash & 0xffffffff;
}

/* Compute the hash for a name string. */
unsigned int ceph_full_name_hash(const char *name, unsigned int len)
{
	unsigned long hash = ceph_init_name_hash();
	while (len--)
		hash = ceph_partial_name_hash(*name++, hash);
	return ceph_end_name_hash(hash);
}

