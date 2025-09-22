#	$OpenBSD: Makefile,v 1.5 2024/06/23 13:53:21 tb Exp $

RUST_OPENSSL_TESTS =	/usr/local/share/rust-openssl-tests
CARGO =			/usr/local/bin/cargo

.if !exists(${RUST_OPENSSL_TESTS}) || !exists(${CARGO})
regress:
	@echo packages rust-openssl-tests and rust are required for this regress
	@echo SKIPPED
.else

REGRESS_TARGETS +=	rust-openssl-test

WORKSPACE_LINKS = openssl openssl-errors openssl-macros openssl-sys systest

_WORKSPACE_COOKIE =	.workspace

${_WORKSPACE_COOKIE}:
. if ${.CURDIR} != ${.OBJDIR}
	cp ${.CURDIR}/Cargo.toml ${.OBJDIR}/
. endif
	mkdir -p .cargo
	cp ${.CURDIR}/config.toml .cargo/
	cd ${.OBJDIR} && ln -sf ${WORKSPACE_LINKS:S,^,${RUST_OPENSSL_TESTS}/,} .
	touch $@

CLEANFILES += Cargo.lock

. if ${.CURDIR} != ${.OBJDIR}
CLEANFILES += Cargo.toml
. endif

# Force use of base-clang on sparc64 since the build with base-gcc fails with:
# error occurred: Command "cc" "-O0" "-ffunction-sections" "-fdata-sections" [...]
# did not execute successfully (status code exit status: 1).
. if "${MACHINE_ARCH}" == sparc64
CARGO_CC=/usr/bin/clang
. else
CARGO_CC=cc
. endif

rust-openssl-test: ${_WORKSPACE_COOKIE}
	cd ${.OBJDIR} && env CC=${CARGO_CC} \
		cargo test --offline --color=never -- --color=never

CLEANFILES +=	${_WORKSPACE_COOKIE} ${WORKSPACE_LINKS}

. if make(clean) || make(cleandir)
.  if exists(.cargo) || exists(target)
.BEGIN:
	rm -rf .cargo
	rm -rf target
.  endif
. endif

.endif

.include <bsd.regress.mk>
