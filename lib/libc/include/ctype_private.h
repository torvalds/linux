/* $OpenBSD: ctype_private.h,v 1.2 2015/08/27 04:37:09 guenther Exp $ */
/* Written by Marc Espie, public domain */
#define CTYPE_NUM_CHARS       256

__BEGIN_HIDDEN_DECLS
extern const char _C_ctype_[];
extern const short _C_toupper_[];
extern const short _C_tolower_[];
__END_HIDDEN_DECLS
