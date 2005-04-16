/*
 * linux/include/asm-arm/arch-shark/uncompress.h
 * by Alexander Schulz
 *
 * derived from:
 * linux/include/asm-arm/arch-ebsa285/uncompress.h
 * Copyright (C) 1996,1997,1998 Russell King
 */

#define SERIAL_BASE ((volatile unsigned char *)0x400003f8)

static __inline__ void putc(char c)
{
	int t;

	SERIAL_BASE[0] = c;
	t=0x10000;
	while (t--);
}

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

#ifdef DEBUG
static void putn(unsigned long z)
{
	int i;
	char x;

	putc('0');
	putc('x');
	for (i=0;i<8;i++) {
		x='0'+((z>>((7-i)*4))&0xf);
		if (x>'9') x=x-'0'+'A'-10;
		putc(x);
	}
}

static void putr()
{
	putc('\n');
	putc('\r');
}
#endif

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
