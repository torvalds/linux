#ifndef __PERF_STRBUF_H
#define __PERF_STRBUF_H

/*
 * Strbuf's can be use in many ways: as a byte array, or to store arbitrary
 * long, overflow safe strings.
 *
 * Strbufs has some invariants that are very important to keep in mind:
 *
 * 1. the ->buf member is always malloc-ed, hence strbuf's can be used to
 *    build complex strings/buffers whose final size isn't easily known.
 *
 *    It is NOT legal to copy the ->buf pointer away.
 *    `strbuf_detach' is the operation that detachs a buffer from its shell
 *    while keeping the shell valid wrt its invariants.
 *
 * 2. the ->buf member is a byte array that has at least ->len + 1 bytes
 *    allocated. The extra byte is used to store a '\0', allowing the ->buf
 *    member to be a valid C-string. Every strbuf function ensure this
 *    invariant is preserved.
 *
 *    Note that it is OK to "play" with the buffer directly if you work it
 *    that way:
 *
 *    strbuf_grow(sb, SOME_SIZE);
 *       ... Here, the memory array starting at sb->buf, and of length
 *       ... strbuf_avail(sb) is all yours, and you are sure that
 *       ... strbuf_avail(sb) is at least SOME_SIZE.
 *    strbuf_setlen(sb, sb->len + SOME_OTHER_SIZE);
 *
 *    Of course, SOME_OTHER_SIZE must be smaller or equal to strbuf_avail(sb).
 *
 *    Doing so is safe, though if it has to be done in many places, adding the
 *    missing API to the strbuf module is the way to go.
 *
 *    XXX: do _not_ assume that the area that is yours is of size ->alloc - 1
 *         even if it's true in the current implementation. Alloc is somehow a
 *         "private" member that should not be messed with.
 */

#include <assert.h>
#include <stdarg.h>

extern char strbuf_slopbuf[];
struct strbuf {
	size_t alloc;
	size_t len;
	char *buf;
};

#define STRBUF_INIT  { 0, 0, strbuf_slopbuf }

/*----- strbuf life cycle -----*/
void strbuf_init(struct strbuf *buf, ssize_t hint);
void strbuf_release(struct strbuf *buf);
char *strbuf_detach(struct strbuf *buf, size_t *);

/*----- strbuf size related -----*/
static inline ssize_t strbuf_avail(const struct strbuf *sb) {
	return sb->alloc ? sb->alloc - sb->len - 1 : 0;
}

void strbuf_grow(struct strbuf *buf, size_t);

static inline void strbuf_setlen(struct strbuf *sb, size_t len) {
	if (!sb->alloc)
		strbuf_grow(sb, 0);
	assert(len < sb->alloc);
	sb->len = len;
	sb->buf[len] = '\0';
}

/*----- add data in your buffer -----*/
void strbuf_addch(struct strbuf *sb, int c);

void strbuf_add(struct strbuf *buf, const void *, size_t);
static inline void strbuf_addstr(struct strbuf *sb, const char *s) {
	strbuf_add(sb, s, strlen(s));
}

__attribute__((format(printf,2,3)))
void strbuf_addf(struct strbuf *sb, const char *fmt, ...);

/* XXX: if read fails, any partial read is undone */
ssize_t strbuf_read(struct strbuf *, int fd, ssize_t hint);

#endif /* __PERF_STRBUF_H */
