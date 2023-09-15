/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

#include <linux/tty_buffer.h>
#include <linux/tty_port.h>

struct tty_ldisc;

int tty_buffer_set_limit(struct tty_port *port, int limit);
unsigned int tty_buffer_space_avail(struct tty_port *port);
int tty_buffer_request_room(struct tty_port *port, size_t size);
size_t __tty_insert_flip_string_flags(struct tty_port *port, const u8 *chars,
				      const u8 *flags, bool mutable_flags,
				      size_t size);
size_t tty_prepare_flip_string(struct tty_port *port, u8 **chars, size_t size);
void tty_flip_buffer_push(struct tty_port *port);

/**
 * tty_insert_flip_string_fixed_flag - add characters to the tty buffer
 * @port: tty port
 * @chars: characters
 * @flag: flag value for each character
 * @size: size
 *
 * Queue a series of bytes to the tty buffering. All the characters passed are
 * marked with the supplied flag.
 *
 * Returns: the number added.
 */
static inline size_t tty_insert_flip_string_fixed_flag(struct tty_port *port,
						       const u8 *chars, u8 flag,
						       size_t size)
{
	return __tty_insert_flip_string_flags(port, chars, &flag, false, size);
}

/**
 * tty_insert_flip_string_flags - add characters to the tty buffer
 * @port: tty port
 * @chars: characters
 * @flags: flag bytes
 * @size: size
 *
 * Queue a series of bytes to the tty buffering. For each character the flags
 * array indicates the status of the character.
 *
 * Returns: the number added.
 */
static inline size_t tty_insert_flip_string_flags(struct tty_port *port,
						  const u8 *chars,
						  const u8 *flags, size_t size)
{
	return __tty_insert_flip_string_flags(port, chars, flags, true, size);
}

/**
 * tty_insert_flip_char - add one character to the tty buffer
 * @port: tty port
 * @ch: character
 * @flag: flag byte
 *
 * Queue a single byte @ch to the tty buffering, with an optional flag.
 */
static inline size_t tty_insert_flip_char(struct tty_port *port, u8 ch, u8 flag)
{
	struct tty_buffer *tb = port->buf.tail;
	int change;

	change = !tb->flags && (flag != TTY_NORMAL);
	if (!change && tb->used < tb->size) {
		if (tb->flags)
			*flag_buf_ptr(tb, tb->used) = flag;
		*char_buf_ptr(tb, tb->used++) = ch;
		return 1;
	}
	return __tty_insert_flip_string_flags(port, &ch, &flag, false, 1);
}

static inline size_t tty_insert_flip_string(struct tty_port *port,
					    const u8 *chars, size_t size)
{
	return tty_insert_flip_string_fixed_flag(port, chars, TTY_NORMAL, size);
}

size_t tty_ldisc_receive_buf(struct tty_ldisc *ld, const u8 *p, const u8 *f,
			     size_t count);

void tty_buffer_lock_exclusive(struct tty_port *port);
void tty_buffer_unlock_exclusive(struct tty_port *port);

#endif /* _LINUX_TTY_FLIP_H */
