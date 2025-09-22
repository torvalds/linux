#	$OpenBSD: bsd.obj.mk,v 1.19 2017/01/24 03:22:13 tb Exp $
#	$NetBSD: bsd.obj.mk,v 1.9 1996/04/10 21:08:05 thorpej Exp $

.if !target(obj)
.  if defined(NOOBJ)
obj:
.  else

.  if defined(MAKEOBJDIR)
__objdir=	${MAKEOBJDIR}
.  else
__objdir=	obj
.  endif

_SUBDIRUSE:

obj! _SUBDIRUSE
	@cd ${.CURDIR}; \
	umask ${WOBJUMASK}; \
	here=`/bin/pwd`; bsdsrcdir=`cd ${BSDSRCDIR}; /bin/pwd`; \
	subdir=$${here#$${bsdsrcdir}/}; \
	if [[ `id -u` -eq 0 && ${BUILDUSER} != root ]]; then \
		SETOWNER="chown -h ${BUILDUSER}:${WOBJGROUP}"; \
		if [[ $$here != $$subdir ]]; then \
			_mkdirs() { \
				su ${BUILDUSER} -c "mkdir -p $$1"; \
			}; \
			MKDIRS=_mkdirs; \
		fi; \
	elif [[ `id` == *'('${WOBJGROUP}')'* && $$here == $$subdir ]]; then \
		SETOWNER="chown :${WOBJGROUP}"; \
	else \
		SETOWNER=:; \
	fi; \
	[[ -z $$MKDIRS ]] && MKDIRS="mkdir -p"; \
	if [[ $$here != $$subdir ]]; then \
		dest=${BSDOBJDIR}/$$subdir ; \
		echo "$$here/${__objdir} -> $$dest"; \
		if [[ ! -L ${__objdir} || `readlink ${__objdir}` != $$dest ]]; \
		    then \
			[[ -e ${__objdir} ]] && rm -rf ${__objdir}; \
			ln -sf $$dest ${__objdir}; \
			$$SETOWNER ${__objdir}; \
		fi; \
		if [[ -d ${BSDOBJDIR} ]]; then \
			[[ -d $$dest ]] || $$MKDIRS $$dest; \
		else \
			if [[ -e ${BSDOBJDIR} ]]; then \
				echo "${BSDOBJDIR} is not a directory"; \
			else \
				echo "${BSDOBJDIR} does not exist"; \
			fi; \
		fi; \
	else \
		dest=$$here/${__objdir} ; \
		if [[ ! -d ${__objdir} ]]; then \
			echo "making $$dest" ; \
			$$MKDIRS $$dest; \
			$$SETOWNER $$dest; \
		fi ; \
	fi;
.  endif
.endif

.include <bsd.own.mk>
