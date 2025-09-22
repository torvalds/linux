#	$OpenBSD: Makefile.inc,v 1.8 2016/05/16 16:20:58 guenther Exp $
#	$NetBSD: Makefile.inc,v 1.4 1996/10/17 02:58:18 cgd Exp $

# `source' files built from m4 source
SRCS+=	__divqu.S __divq.S __divlu.S __divl.S
SRCS+=	__remqu.S __remq.S __remlu.S __reml.S
CLEANFILES+=	__divqu.S __divq.S __divlu.S __divl.S
CLEANFILES+=	__remqu.S __remq.S __remlu.S __reml.S

__divqu.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__divqu')define(OP,\`div')define(S,\`false')"; \
	 echo "define(WORDSIZE,64)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__divq.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__divq')define(OP,\`div')define(S,\`true')"; \
	 echo "define(WORDSIZE,64)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__divlu.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__divlu')define(OP,\`div')define(S,\`false')"; \
	 echo "define(WORDSIZE,32)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__divl.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__divl')define(OP,\`div')define(S,\`true')"; \
	 echo "define(WORDSIZE,32)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__remqu.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__remqu')define(OP,\`rem')define(S,\`false')"; \
	 echo "define(WORDSIZE,64)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__remq.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__remq')define(OP,\`rem')define(S,\`true')"; \
	 echo "define(WORDSIZE,64)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__remlu.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__remlu')define(OP,\`rem')define(S,\`false')"; \
	 echo "define(WORDSIZE,32)"; cat ${.ALLSRC}) | m4 > ${.TARGET}

__reml.S: ${LIBCSRCDIR}/arch/alpha/gen/divrem.m4
	@echo 'building ${.TARGET} from ${.ALLSRC}'
	@(echo "define(NAME,\`__reml')define(OP,\`rem')define(S,\`true')"; \
	 echo "define(WORDSIZE,32)"; cat ${.ALLSRC}) | m4 > ${.TARGET}
