#ifndef _LINUX_TTY_FLIP_H
#define _LINUX_TTY_FLIP_H

extern int tty_buffer_request_room(struct tty_struct *tty, size_t size);
extern int tty_insert_flip_string(struct tty_struct *tty, unsigned char *chars, size_t size);
extern int tty_insert_flip_string_flags(struct tty_struct *tty, unsigned char *chars, char *flags, size_t size);
extern int tty_prepare_flip_string(struct tty_struct *tty, unsigned char **chars, size_t size);
extern int tty_prepare_flip_string_flags(struct tty_struct *tty, unsigned char **chars, char **flags, size_t size);

#ifdef INCLUDE_INLINE_FUNCS
#define _INLINE_ extern
#else
#define _INLINE_ static __inline__
#endif

_INLINE_ int tty_insert_flip_char(struct tty_struct *tty,
				   unsigned char ch, char flag)
{
	struct tty_buffer *tb = tty->buf.tail;
	if (tb && tb->used < tb->size) {
		tb->flag_buf_ptr[tb->used] = flag;
		tb->char_buf_ptr[tb->used++] = ch;
		return 1;
	}
	return tty_insert_flip_string_flags(tty, &ch, &flag, 1);
}

_INLINE_ void tty_schedule_flip(struct tty_struct *tty)
{
	schedule_delayed_work(&tty->buf.work, 1);
}

#undef _INLINE_


#endif /* _LINUX_TTY_FLIP_H */







