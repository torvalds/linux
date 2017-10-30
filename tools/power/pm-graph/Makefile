PREFIX		?= /usr
DESTDIR		?=

all:
	@echo "Nothing to build"

install : uninstall
	install -d  $(DESTDIR)$(PREFIX)/lib/pm-graph
	install analyze_suspend.py $(DESTDIR)$(PREFIX)/lib/pm-graph
	install analyze_boot.py $(DESTDIR)$(PREFIX)/lib/pm-graph

	ln -s $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_boot.py $(DESTDIR)$(PREFIX)/bin/bootgraph
	ln -s $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_suspend.py $(DESTDIR)$(PREFIX)/bin/sleepgraph

	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install bootgraph.8 $(DESTDIR)$(PREFIX)/share/man/man8
	install sleepgraph.8 $(DESTDIR)$(PREFIX)/share/man/man8

uninstall :
	rm -f $(DESTDIR)$(PREFIX)/share/man/man8/bootgraph.8
	rm -f $(DESTDIR)$(PREFIX)/share/man/man8/sleepgraph.8

	rm -f $(DESTDIR)$(PREFIX)/bin/bootgraph
	rm -f $(DESTDIR)$(PREFIX)/bin/sleepgraph

	rm -f $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_boot.py
	rm -f $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_suspend.py
	rm -f $(DESTDIR)$(PREFIX)/lib/pm-graph/*.pyc
	if [ -d $(DESTDIR)$(PREFIX)/lib/pm-graph ] ; then \
		rmdir $(DESTDIR)$(PREFIX)/lib/pm-graph; \
	fi;
