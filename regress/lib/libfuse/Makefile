#       $OpenBSD: Makefile,v 1.4 2021/09/12 07:06:59 anton Exp $

REGRESS_TARGETS=  run-fuse-opt-add-opt
REGRESS_TARGETS+= run-fuse-opt-add-opt-escaped
REGRESS_TARGETS+= run-fuse-opt-add-arg
REGRESS_TARGETS+= run-fuse-opt-insert-arg
REGRESS_TARGETS+= run-fuse-opt-match
REGRESS_TARGETS+= run-fuse-opt-parse
REGRESS_TARGETS+= run-fuse-parse-cmdline
REGRESS_TARGETS+= run-fuse-null-check
REGRESS_TARGETS+= run-fuse-mount

REGRESS_ROOT_TARGETS+= run-fuse-mount

LDFLAGS+= -lfuse
CLEANFILES= fuse-opt-add-opt
CLEANFILES+=fuse-opt-add-opt-escaped
CLEANFILES+=fuse-opt-add-arg
CLEANFILES+=fuse-opt-insert-arg
CLEANFILES+=fuse-opt-match
CLEANFILES+=fuse-opt-parse
CLEANFILES+=fuse-parse-cmdline
CLEANFILES+=fuse-null-check
CLEANFILES+=fuse-mount

.PHONY: ${REGRESS_TARGETS}

run-fuse-opt-add-opt: fuse-opt-add-opt
	./fuse-opt-add-opt
run-fuse-opt-add-opt-escaped: fuse-opt-add-opt-escaped
	./fuse-opt-add-opt-escaped
run-fuse-opt-add-arg: fuse-opt-add-arg
	./fuse-opt-add-arg
run-fuse-opt-insert-arg: fuse-opt-insert-arg
	./fuse-opt-insert-arg
run-fuse-opt-match: fuse-opt-match
	./fuse-opt-match
run-fuse-opt-parse: fuse-opt-parse
	./fuse-opt-parse
run-fuse-parse-cmdline: fuse-parse-cmdline
	./fuse-parse-cmdline
run-fuse-null-check: fuse-null-check
	./fuse-null-check
run-fuse-mount: fuse-mount
	${SUDO} ./fuse-mount

.include <bsd.regress.mk>
