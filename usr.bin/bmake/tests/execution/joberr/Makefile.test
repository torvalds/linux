# $FreeBSD$

check-joberr:
.for _t in 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9
	@-${MAKE} -f ${MAKEFILE} -j2 do-$@
.endfor

do-check-joberr:
	@echo "Error: build failed"
	@exit 1
