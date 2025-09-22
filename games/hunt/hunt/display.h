/*	$OpenBSD: display.h,v 1.4 2024/05/21 05:00:47 jsg Exp $	*/
/*	David Leonard <d@openbsd.org>, 1999.  Public domain.	*/

void display_open(void);
void display_beep(void);
void display_refresh(void);
void display_clear_eol(void);
void display_put_ch(char);
void display_put_str(char *);
void display_clear_the_screen(void);
void display_move(int, int);
void display_getyx(int *, int *);
void display_end(void);
char display_atyx(int, int);
void display_redraw_screen(void);
int  display_iskillchar(char);
int  display_iserasechar(char);
