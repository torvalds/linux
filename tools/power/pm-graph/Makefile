PREFIX		?= /usr
DESTDIR		?=

all:
	@echo "Nothing to build"

install :
	install -d  $(DESTDIR)$(PREFIX)/lib/pm-graph
	install analyze_suspend.py $(DESTDIR)$(PREFIX)/lib/pm-graph
	install analyze_boot.py $(DESTDIR)$(PREFIX)/lib/pm-graph

	ln -s $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_boot.py $(DESTDIR)$(PREFIX)/bin/bootgraph
	ln -s $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_suspend.py $(DESTDIR)$(PREFIX)/bin/sleepgraph

	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install bootgraph.8 $(DESTDIR)$(PREFIX)/share/man/man8
	install sleepgraph.8 $(DESTDIR)$(PREFIX)/share/man/man8

uninstall :
	rm $(DESTDIR)$(PREFIX)/share/man/man8/bootgraph.8
	rm $(DESTDIR)$(PREFIX)/share/man/man8/sleepgraph.8

	rm $(DESTDIR)$(PREFIX)/bin/bootgraph
	rm $(DESTDIR)$(PREFIX)/bin/sleepgraph

	rm $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_boot.py
	rm $(DESTDIR)$(PREFIX)/lib/pm-graph/analyze_suspend.py
	rmdir $(DESTDIR)$(PREFIX)/lib/pm-graph
