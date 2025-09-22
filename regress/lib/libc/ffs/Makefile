PROG=		ffs_test

# prevent constant folding and inlining of __builtin_ffs()
CFLAGS+=	-ffreestanding

.include <bsd.regress.mk>
