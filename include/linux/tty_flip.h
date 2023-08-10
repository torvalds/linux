/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

#include <linux/tty_buffer.h>
#include <linux/tty_port.h>

struct tty_ldisc;

int tty_buffer_set_limit(struct tty_port *port, int limit);
unsigned int tty_buffer_space_avail(struct tty_port *port);
int tty_buffer_request_room(struct tty_port *port, size_t size);
int tty_insert_flip_string_flags(struct tty_port *port, const u8 *chars,
				 const u8 *flags, size_t size);
int tty_insert_flip_string_fixed_flag(struct tty_port *port, const u8 *chars,
				      u8 flag, size_t size);
int tty_prepare_flip_string(struct tty_port *port, u8 **chars, size_t size);
void tty_flip_buffer_push(struct tty_port *port);
int __tty_insert_flip_char(struct tty_port *port, u8 ch, u8 flag);

static inline int tty_insert_flip_char(struct tty_port *port, u8 ch, u8 flag)
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
	return __tty_insert_flip_char(port, ch, flag);
}

static inline int tty_insert_flip_string(struct tty_port *port,
		const u8 *chars, size_t size)
{
	return tty_insert_flip_string_fixed_flag(port, chars, TTY_NORMAL, size);
}

size_t tty_ldisc_receive_buf(struct tty_ldisc *ld, const u8 *p, const u8 *f,
			     size_t count);

void tty_buffer_lock_exclusive(struct tty_port *port);
void tty_buffer_unlock_exclusive(struct tty_port *port);

#endif /* _LINUX_TTY_FLIP_H */
