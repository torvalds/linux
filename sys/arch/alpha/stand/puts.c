/*	$OpenBSD: puts.c,v 1.4 2023/01/16 07:29:32 deraadt Exp $	*/


void putchar(char);

void
puts(s)
	char *s;
{

	while (*s)
		putchar(*s++);
}
