/*	$OpenBSD: main.c,v 1.1 2013/11/03 00:20:24 schwarze Exp $	*/

/*	Written by Ingo Schwarze, 2013,  Public domain. */

void shortseek(void);
void longseek(void);

int
main(void)
{
	shortseek();
	longseek();
	return(0);
}
