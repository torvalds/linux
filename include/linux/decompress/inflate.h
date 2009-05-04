#ifndef INFLATE_H
#define INFLATE_H

/* Other housekeeping constants */
#define INBUFSIZ 4096

int gunzip(unsigned char *inbuf, int len,
	   int(*fill)(void*, unsigned int),
	   int(*flush)(void*, unsigned int),
	   unsigned char *output,
	   int *pos,
	   void(*error_fn)(char *x));
#endif
