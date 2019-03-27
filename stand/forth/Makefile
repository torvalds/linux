# $FreeBSD$

.include <bsd.init.mk>

MAN+=	beastie.4th.8 \
	brand.4th.8 \
	check-password.4th.8 \
	color.4th.8 \
	delay.4th.8 \
	loader.4th.8 \
	menu.4th.8 \
	menusets.4th.8 \
	version.4th.8

FILES+=	beastie.4th
FILES+=	brand.4th
FILES+=	brand-fbsd.4th
FILES+=	check-password.4th
FILES+=	color.4th
FILES+=	delay.4th
FILES+=	efi.4th
FILES+=	frames.4th
FILES+=	loader.4th
FILES+=	logo-beastie.4th
FILES+=	logo-beastiebw.4th
FILES+=	logo-fbsdbw.4th
FILES+=	logo-orb.4th
FILES+=	logo-orbbw.4th
FILES+=	menu.4th
FILES+=	menu-commands.4th
FILES+=	menusets.4th
FILES+=	screen.4th
FILES+=	shortcuts.4th
FILES+=	support.4th
FILES+=	version.4th

# Allow machine specific loader.rc to be installed.
.for f in loader.rc menu.rc
.if exists(${BOOTSRC}/${MACHINE:C/amd64/i386/}/loader/${f})
FILES+= ${BOOTSRC}/${MACHINE:C/amd64/i386/}/loader/${f}
.else
FILES+= ${f}
.endif
.endfor

.include <bsd.prog.mk>
