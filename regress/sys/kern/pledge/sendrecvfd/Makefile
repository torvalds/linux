#	$OpenBSD: Makefile,v 1.2 2021/09/28 05:40:38 anton Exp $
CFLAGS+=	-Wall -Werror

testtype=	nopledge sendfd recvfd nosendfd norecvfd
vnodetype=	VREG VDIR VBLK VCHR VLNK VSOCK VFIFO

PASS_TARGETS=	test-nopledge-VREG \
		test-nopledge-VDIR \
		test-nopledge-VBLK \
		test-nopledge-VCHR \
		test-nopledge-VLNK \
		test-nopledge-VSOCK \
		test-nopledge-VFIFO \
		\
		test-sendfd-VREG \
		test-sendfd-VBLK \
		test-sendfd-VCHR \
		test-sendfd-VLNK \
		test-sendfd-VSOCK \
		test-sendfd-VFIFO \
		\
		test-recvfd-VREG \
		test-recvfd-VBLK \
		test-recvfd-VCHR \
		test-recvfd-VLNK \
		test-recvfd-VSOCK \
		test-recvfd-VFIFO

FAIL_TARGETS=	test-sendfd-VDIR \
		test-recvfd-VDIR \
		\
		test-nosendfd-VREG \
		test-nosendfd-VDIR \
		test-nosendfd-VBLK \
		test-nosendfd-VCHR \
		test-nosendfd-VLNK \
		test-nosendfd-VSOCK \
		test-nosendfd-VFIFO \
		\
		test-norecvfd-VREG \
		test-norecvfd-VDIR \
		test-norecvfd-VBLK \
		test-norecvfd-VCHR \
		test-norecvfd-VLNK \
		test-norecvfd-VSOCK \
		test-norecvfd-VFIFO

SUDO_TARGETS=	test-nopledge-VBLK \
		test-sendfd-VBLK \
		test-recvfd-VBLK \
		test-nosendfd-VBLK \
		test-norecvfd-VBLK

CLEANFILES+=	sendrecvfd

.for _test in ${testtype}
. for _vnode in ${vnodetype}
REGRESS_TARGETS+=	test-${_test}-${_vnode}

.  if ${SUDO_TARGETS:Mtest-${_test}-${_vnode}}
REGRESS_ROOT_TARGETS+=		test-${_test}-${_vnode}
_sudo-${_test}-${_vnode}=	${SUDO}
.  endif

.  if ${PASS_TARGETS:Mtest-${_test}-${_vnode}} 
test-${_test}-${_vnode}: sendrecvfd
	@echo test-${_test}-${_vnode}: expected PASS
	@${_sudo-${_test}-${_vnode}} ./sendrecvfd ${_test} ${_vnode}

.  elif ${FAIL_TARGETS:Mtest-${_test}-${_vnode}}
test-${_test}-${_vnode}: sendrecvfd
	@echo test-${_test}-${_vnode}: expected FAIL
	@sudo="${SUDO_TARGETS:Mtest-${_test}-${_vnode}:C/.+/${SUDO}/}"; \
	if ${_sudo-${_test}-${_vnode}} ./sendrecvfd ${_test} ${_vnode}; then false; else true; fi

.  else
test-${_test}-${_vnode}:
	@echo "ERROR: test-${_test}-${_vnode} is missing"
	@false
.  endif
. endfor
.endfor

.include <bsd.regress.mk>
