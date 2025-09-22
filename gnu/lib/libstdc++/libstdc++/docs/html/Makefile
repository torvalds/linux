
PWD_COMMAND=$${PWDCMD-pwd}
MAKEINFO=makeinfo
INC=../../../gcc/doc/include

all: documentation.html                  \
     faq/index.txt                       \
     17_intro/porting.html               \
     17_intro/porting-howto.html

# chock full of GNUism, probably
documentation.html: $(wildcard */howto.html)
	sed -n '1,/beginlist/p' $@ > tmp.top
	sed -n '/endlist/,$$p' $@ > tmp.bottom
	echo '  <ul>' > tmp.middle
	for i in [0-9]*/howto.html; do \
	  title=`grep 'h1 ' $$i |\
	  sed 's=.*\(Chapter [[:digit:]]*\):[[:space:]]*\(.*\)</a>.*=\2 (\1)='` ;\
	  awk -v file=$$i -v "title=$$title" -f makedoc.awk $$i >> tmp.middle ;\
	done
	awk -v file=ext/howto.html -v "title=Extensions to the Standard Library"\
	  -f makedoc.awk ext/howto.html >> tmp.middle ;\
	echo '  </ul>' >> tmp.middle
	cat tmp.top tmp.middle tmp.bottom > $@
	rm tmp.top tmp.middle tmp.bottom

faq/index.txt: faq/index.html
	lynx -dump $< | sed "s%file://localhost`${PWD_COMMAND}`%..%" > $@

17_intro/porting.html: 17_intro/porting.texi
	${MAKEINFO} -I ${INC} --html --no-split $< -o $@

# known to work under RH; this can be cleaned up later if needed
17_intro/porting-howto.html: 17_intro/porting-howto.xml
	xltproc -o $@ /usr/share/xml/docbook/xsl-stylesheets-1.48-2/html/docbook.xsl $<

# vim:noet ts=4
