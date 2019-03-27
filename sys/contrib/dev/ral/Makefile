#	$FreeBSD$

FILES=	rt2561s.fw.uu rt2561.fw.uu rt2661.fw.uu rt2860.fw.uu

rt2561s.fw.uu: microcode.h LICENSE
	(echo '#include <sys/types.h>'; \
	 echo '#include <unistd.h>'; \
	 cat microcode.h; \
	 echo 'int main(void) { \
	     write(1, rt2561s, sizeof(rt2561s)); return 0; \
	 }') | ${CC} -o build -x c -
	(sed 's/^/# /' LICENSE; ./build | uuencode rt2561s.fw) > ${.TARGET}

rt2561.fw.uu: microcode.h LICENSE
	(echo '#include <sys/types.h>'; \
	 echo '#include <unistd.h>'; \
	 cat microcode.h; \
	 echo 'int main(void) { \
	     write(1, rt2561, sizeof(rt2561)); return 0; \
	 }') | ${CC} -o build -x c -
	(sed 's/^/# /' LICENSE; ./build | uuencode rt2561.fw) > ${.TARGET}

rt2661.fw.uu: microcode.h LICENSE
	(echo '#include <sys/types.h>'; \
	 echo '#include <unistd.h>'; \
	 cat microcode.h; \
	 echo 'int main(void) { \
	     write(1, rt2661, sizeof(rt2661)); return 0; \
	 }') | ${CC} -o build -x c -
	(sed 's/^/# /' LICENSE; ./build | uuencode rt2661.fw) > ${.TARGET}

rt2860.fw.uu: microcode.h LICENSE
	(echo '#include <sys/types.h>'; \
	 echo '#include <unistd.h>'; \
	 cat microcode.h; \
	 echo 'int main(void) { \
	     write(1, rt2860, sizeof(rt2860)); return 0; \
	 }') | ${CC} -o build -x c -
	(sed 's/^/# /' LICENSE; ./build | uuencode rt2860.fw) > ${.TARGET}

clean:
	rm -f build build.c ${FILES}

.include <bsd.prog.mk>
