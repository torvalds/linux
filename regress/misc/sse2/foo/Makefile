.include <bsd.obj.mk>

BAR_DIR=	${.CURDIR}/../libbar

BAR_OBJDIR!=	if [ -d $(BAR_DIR)/${__objdir} ]; then \
			echo "$(BAR_DIR)/${__objdir}"; \
		else \
			echo "$(BAR_DIR)"; \
		fi

PROG=		foo
SRCS=		foo.c
LDADD=		-Wl,-rpath,$(BAR_OBJDIR) -L$(BAR_OBJDIR) -lbar -lpthread
CFLAGS+=	-msse2

.include <bsd.regress.mk>
