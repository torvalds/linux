#	$FreeBSD$

FILES=	mw88W8363.fw.uu mwlboot.fw.uu

mw88W8363.fw.uu: mv88W8363fw.h LICENSE
	(cat mv88W8363fw.h; \
	 echo 'int main(void) { \
	     write(1, fmimage, sizeof(fmimage)); return 0; \
	 }') | ${CC} -o build -x c -
	(sed 's/^/# /' LICENSE; ./build | uuencode mw88W8363.fw) > ${.TARGET}

mwlboot.fw.uu: mvbootfw.h LICENSE
	(cat mvbootfw.h; \
	 echo 'int main(void) { \
	     write(1, hlpimage, sizeof(hlpimage)); return 0; \
	 }') | ${CC} -o build -x c -
	(sed 's/^/# /' LICENSE; ./build | uuencode mwlboot.fw) > ${.TARGET}

clean:
	rm -f build build.c ${FILES}

.include <bsd.prog.mk>
