/* $OpenBSD: disasm.h,v 1.1 2024/11/19 05:50:41 anton Exp $ */

#include <sys/types.h>	/* vaddr_t */
#include <stddef.h>	/* size_t */
#include <stdint.h>

struct db_disasm_context {
	struct {
		const uint8_t	*buf;
		size_t		 len;
	} raw;

	struct {
		char	buf[128];
		size_t	siz;
		size_t	len;
	} act;
};

vaddr_t	db_disasm(vaddr_t, int);

extern struct db_disasm_context	*ctx;
