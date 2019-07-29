#ifndef SRCCODE_H
#define SRCCODE_H 1

/* Result is not 0 terminated */
char *find_sourceline(char *fn, unsigned line, int *lenp);

#endif
