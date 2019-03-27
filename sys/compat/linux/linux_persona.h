/*
 * $FreeBSD$
 */

#ifndef LINUX_PERSONALITY_H
#define LINUX_PERSONALITY_H

/*
 * Flags for bug emulation.
 *
 * These occupy the top three bytes.
 */
enum {
	LINUX_UNAME26 =			0x0020000,
	LINUX_ADDR_NO_RANDOMIZE =	0x0040000,	/* disable randomization
							 * of VA space
							 */
	LINUX_FDPIC_FUNCPTRS =		0x0080000,	/* userspace function
							 * ptrs point to descriptors
							 * (signal handling)
							 */
	LINUX_MMAP_PAGE_ZERO =		0x0100000,
	LINUX_ADDR_COMPAT_LAYOUT =	0x0200000,
	LINUX_READ_IMPLIES_EXEC =	0x0400000,
	LINUX_ADDR_LIMIT_32BIT =	0x0800000,
	LINUX_SHORT_INODE =		0x1000000,
	LINUX_WHOLE_SECONDS =		0x2000000,
	LINUX_STICKY_TIMEOUTS =		0x4000000,
	LINUX_ADDR_LIMIT_3GB =		0x8000000,
};

/*
 * Security-relevant compatibility flags that must be
 * cleared upon setuid or setgid exec:
 */
#define LINUX_PER_CLEAR_ON_SETID	(LINUX_READ_IMPLIES_EXEC  | \
					LINUX_ADDR_NO_RANDOMIZE  | \
					LINUX_ADDR_COMPAT_LAYOUT | \
					LINUX_MMAP_PAGE_ZERO)

/*
 * Personality types.
 *
 * These go in the low byte.  Avoid using the top bit, it will
 * conflict with error returns.
 */
enum {
	LINUX_PER_LINUX =	0x0000,
	LINUX_PER_LINUX_32BIT =	0x0000 | LINUX_ADDR_LIMIT_32BIT,
	LINUX_PER_LINUX_FDPIC =	0x0000 | LINUX_FDPIC_FUNCPTRS,
	LINUX_PER_LINUX32 =	0x0008,
	LINUX_PER_LINUX32_3GB =	0x0008 | LINUX_ADDR_LIMIT_3GB,
	LINUX_PER_MASK =	0x00ff,
};

#endif /* LINUX_PERSONALITY_H */
