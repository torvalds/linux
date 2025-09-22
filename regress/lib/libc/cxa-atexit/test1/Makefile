# $OpenBSD: Makefile,v 1.2 2015/05/29 04:17:11 miod Exp $

.include <bsd.obj.mk>

GD1_DIR=${.CURDIR}/../libgd1
GD1_OBJDIR!=    if [ -d $(GD1_DIR)/${__objdir} ]; then \
                        echo "$(GD1_DIR)/${__objdir}"; \
                else \
                        echo "$(GD1_DIR)"; \
                fi

GD2_DIR=${.CURDIR}/../libgd2
GD2_OBJDIR!=    if [ -d $(GD2_DIR)/${__objdir} ]; then \
                        echo "$(GD2_DIR)/${__objdir}"; \
                else \
                        echo "$(GD2_DIR)"; \
                fi

PROG=		test1

SRCS=		test1.C

CXXFLAGS+=	-DLIBGD1="\"$(GD1_OBJDIR)/libgd1.so\""
CXXFLAGS+=	-DLIBGD2="\"$(GD2_OBJDIR)/libgd2.so\""
LDFLAGS+=	-Wl,-E

NOMAN=

.include <bsd.regress.mk>
