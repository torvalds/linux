#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

extern int tty_buffer_request_room(struct tty_port *port, size_t size);
extern int tty_insert_flip_string_flags(struct tty_port *port,
		const unsigned char *chars, const char *flags, size_t size);
extern int tty_insert_flip_string_fixed_flag(struct tty_port *port,
		const unsigned char *chars, char flag, size_t size);
extern int tty_prepare_flip_string(struct tty_port *port,
		unsigned char **chars, size_t size);
extern int tty_prepare_flip_string_flags(struct tty_port *port,
		unsigned char **chars, char **flags, size_t size);
extern void tty_flip_buffer_push(struct tty_port *port);
void tty_schedule_flip(struct tty_port *port);

static inline int tty_insert_flip_char(struct tty_port *port,
					unsigned char ch, char flag)
{
	struct tty_buffer *tb = port->buf.tail;
	if (tb && tb->used < tb->size) {
		tb->flag_buf_ptr[tb->used] = flag;
		tb->char_buf_ptr[tb->used++] = ch;
		return 1;
	}
	return tty_insert_flip_string_flags(port, &ch, &flag, 1);
}

static inline int tty_insert_flip_string(struct tty_port *port,
		const unsigned char *chars, size_t size)
{
	return tty_insert_flip_string_fixed_flag(port, chars, TTY_NORMAL, size);
}

#endif /* _LINUX_TTY_FLIP_H */
