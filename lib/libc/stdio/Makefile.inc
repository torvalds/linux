#	$OpenBSD: Makefile.inc,v 1.33 2025/07/25 18:27:56 tedu Exp $

# stdio sources
.PATH: ${LIBCSRCDIR}/stdio

CFLAGS+=-DFLOATING_POINT -DPRINTF_WIDE_CHAR -DSCANF_WIDE_CHAR

SRCS+=	asprintf.c clrerr.c fclose.c fdclose.c fdopen.c feof.c ferror.c fflush.c fgetc.c \
	fgetln.c fgetpos.c fgets.c fileno.c findfp.c flags.c fmemopen.c \
	fopen.c fprintf.c fpurge.c fputc.c fputs.c fread.c freopen.c fscanf.c \
	fseek.c fsetpos.c ftell.c funopen.c fvwrite.c fwalk.c fwrite.c \
	getc.c getchar.c getw.c makebuf.c open_memstream.c \
	open_wmemstream.c perror.c printf.c putc.c putchar.c puts.c putw.c \
	refill.c remove.c rewind.c rget.c scanf.c setbuf.c setbuffer.c \
	setvbuf.c snprintf.c sprintf.c sscanf.c stdio.c tempnam.c tmpfile.c \
	tmpnam.c ungetc.c vasprintf.c vfprintf.c vfscanf.c vprintf.c vscanf.c \
	vsnprintf.c vsprintf.c vsscanf.c wbuf.c wsetup.c flockfile.c \
	fgetwc.c fgetwln.c fgetws.c fputwc.c fputws.c fwide.c getwc.c \
	getwchar.c putwc.c putwchar.c ungetwc.c \
	fwprintf.c swprintf.c vfwprintf.c vswprintf.c vwprintf.c wprintf.c \
	fwscanf.c swscanf.c vfwscanf.c vswscanf.c vwscanf.c wscanf.c \
	getdelim.c getline.c dprintf.c vdprintf.c \
	fpending.c freadahead.c freading.c freadptr.c fseterr.c fwriting.c

MAN+=	fclose.3 ferror.3 fflush.3 fgetln.3 fgets.3 fgetwln.3 \
	flockfile.3 fmemopen.3 \
	fopen.3 __fpending.3 fputs.3 fread.3 fseek.3 funopen.3 getc.3 \
	open_memstream.3 perror.3 printf.3 putc.3 remove.3 scanf.3 setbuf.3 \
	setvbuf.3 stdio.3 tmpnam.3 ungetc.3 fgetws.3 fputws.3 fwide.3 getwc.3 \
	putwc.3 ungetwc.3 wprintf.3 wscanf.3 getdelim.3
