#	$OpenBSD: bsd.subdir.mk,v 1.22 2016/10/08 09:43:46 schwarze Exp $
#	$NetBSD: bsd.subdir.mk,v 1.11 1996/04/04 02:05:06 jtc Exp $
#	@(#)bsd.subdir.mk	5.9 (Berkeley) 2/1/91

.if !target(.MAIN)
.MAIN: all
.endif

# Make sure this is defined
SKIPDIR?=

_SUBDIRUSE: .USE
.if defined(SUBDIR)
	@for entry in ${SUBDIR}; do \
		set -e; if test -d ${.CURDIR}/$${entry}.${MACHINE}; then \
			_newdir_="$${entry}.${MACHINE}"; \
		else \
			_newdir_="$${entry}"; \
		fi; \
		if test X"${_THISDIR_}" = X""; then \
			_nextdir_="$${_newdir_}"; \
		else \
			_nextdir_="$${_THISDIR_}/$${_newdir_}"; \
		fi; \
		_makefile_spec_=""; \
		if [ -e ${.CURDIR}/$${_newdir_}/Makefile.bsd-wrapper ]; then \
			_makefile_spec_="-f Makefile.bsd-wrapper"; \
		fi; \
		subskipdir=''; \
		for skipdir in ${SKIPDIR}; do \
			subentry=$${skipdir#$${entry}}; \
			if [ X$${subentry} != X$${skipdir} ]; then \
				if [ X$${subentry} = X ]; then \
					echo "($${_nextdir_} skipped)"; \
					break; \
				fi; \
				subskipdir="$${subskipdir} $${subentry#/}"; \
			fi; \
		done; \
		if [ X$${skipdir} = X -o X$${subentry} != X ]; then \
			echo "===> $${_nextdir_}"; \
			${MAKE} -C ${.CURDIR}/$${_newdir_} \
			    SKIPDIR="$${subskipdir}" \
			    $${_makefile_spec_} _THISDIR_="$${_nextdir_}" \
			    ${MAKE_FLAGS} \
			    ${.TARGET:S/^real//}; \
		fi; \
	done

${SUBDIR}::
	@set -e; if test -d ${.CURDIR}/${.TARGET}.${MACHINE}; then \
		_newdir_=${.TARGET}.${MACHINE}; \
	else \
		_newdir_=${.TARGET}; \
	fi; \
	_makefile_spec_=""; \
	if [ -f ${.CURDIR}/$${_newdir_}/Makefile.bsd-wrapper ]; then \
		_makefile_spec_="-f Makefile.bsd-wrapper"; \
	fi; \
	echo "===> $${_newdir_}"; \
	exec ${MAKE} -C ${.CURDIR}/$${_newdir_} ${MAKE_FLAGS} \
	    $${_makefile_spec_} _THISDIR_="$${_newdir_}" all
.endif

.if !target(install)
.  if !target(beforeinstall)
beforeinstall:
.  endif
.  if !target(afterinstall)
afterinstall:
.  endif
install: maninstall
maninstall: afterinstall
afterinstall: realinstall
realinstall: beforeinstall _SUBDIRUSE
.endif


.for t in all cleandir includes depend obj tags manlint
.  if !target($t)
$t: _SUBDIRUSE
.  endif
.endfor
.if !target(regress) && empty(.TARGETS:Mall)
regress: _SUBDIRUSE
.endif
.if !target(clean) && empty(.TARGETS:Mcleandir)
clean: _SUBDIRUSE
.endif

.if !defined(BSD_OWN_MK)
.  include <bsd.own.mk>
.endif
