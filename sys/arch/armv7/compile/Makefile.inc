SYSDIR != cd ${.CURDIR}/../../../..; pwd
CONFDIR != cd ${.CURDIR}/../../conf; pwd

.if ${.CURDIR} == ${.OBJDIR}
.PHONY: config
config:
	@echo make obj required first >&2
	@false
.else
.PHONY: config clean
config:
	config ${.CURDIR:M*.PROF:C/.*/-p/} -b ${.OBJDIR} \
		-s ${SYSDIR} ${CONFDIR}/${.CURDIR:T:S/.PROF$//}
.endif

cleandir clean:

.include <bsd.obj.mk>

