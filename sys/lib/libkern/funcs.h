size_t strlcpy(char *, const char *, int)
	__attribute__ ((__bounded__(__string__,1,3)));
size_t strlcat(char *, const char *, int)
	__attribute__ ((__bounded__(__string__,1,3)));
