# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Most of this file is copied from tools/perf/Documentation/Makefile

include ../../../scripts/Makefile.include
include ../../../scripts/utilities.mak

MAN3_TXT  = libperf.txt
MAN7_TXT  = libperf-counting.txt libperf-sampling.txt
MAN_EX    = examples/*.c

MAN_TXT   = $(MAN3_TXT) $(MAN7_TXT)

_MAN_XML  = $(patsubst %.txt,%.xml,$(MAN_TXT))
_MAN_HTML = $(patsubst %.txt,%.html,$(MAN_TXT))
_MAN_3    = $(patsubst %.txt,%.3,$(MAN3_TXT))
_MAN_7    = $(patsubst %.txt,%.7,$(MAN7_TXT))

MAN_XML   = $(addprefix $(OUTPUT),$(_MAN_XML))
MAN_HTML  = $(addprefix $(OUTPUT),$(_MAN_HTML))
MAN_3     = $(addprefix $(OUTPUT),$(_MAN_3))
MAN_7     = $(addprefix $(OUTPUT),$(_MAN_7))
MAN_X     = $(MAN_3) $(MAN_7)

# Make the path relative to DESTDIR, not prefix
ifndef DESTDIR
  prefix ?=$(HOME)
endif

mandir  ?= $(prefix)/share/man
man3dir  = $(mandir)/man3
man7dir  = $(mandir)/man7

docdir  ?= $(prefix)/share/doc/libperf
htmldir  = $(docdir)/html
exdir    = $(docdir)/examples

ASCIIDOC        = asciidoc
ASCIIDOC_EXTRA  = --unsafe -f asciidoc.conf
ASCIIDOC_HTML   = xhtml11
MANPAGE_XSL     = manpage-normal.xsl
XMLTO_EXTRA     =
XMLTO           =xmlto

INSTALL ?= install
RM      ?= rm -f

# For asciidoc ...
#	-7.1.2,	no extra settings are needed.
#	8.0-,	set ASCIIDOC8.
#

# For docbook-xsl ...
#	-1.68.1,	set ASCIIDOC_NO_ROFF? (based on changelog from 1.73.0)
#	1.69.0,		no extra settings are needed?
#	1.69.1-1.71.0,	set DOCBOOK_SUPPRESS_SP?
#	1.71.1,		no extra settings are needed?
#	1.72.0,		set DOCBOOK_XSL_172.
#	1.73.0-,	set ASCIIDOC_NO_ROFF

# If you had been using DOCBOOK_XSL_172 in an attempt to get rid
# of 'the ".ft C" problem' in your generated manpages, and you
# instead ended up with weird characters around callouts, try
# using ASCIIDOC_NO_ROFF instead (it works fine with ASCIIDOC8).

ifdef ASCIIDOC8
  ASCIIDOC_EXTRA += -a asciidoc7compatible
endif
ifdef DOCBOOK_XSL_172
  ASCIIDOC_EXTRA += -a libperf-asciidoc-no-roff
  MANPAGE_XSL = manpage-1.72.xsl
else
  ifdef ASCIIDOC_NO_ROFF
    # docbook-xsl after 1.72 needs the regular XSL, but will not
    # pass-thru raw roff codes from asciidoc.conf, so turn them off.
    ASCIIDOC_EXTRA += -a libperf-asciidoc-no-roff
  endif
endif
ifdef MAN_BOLD_LITERAL
  XMLTO_EXTRA += -m manpage-bold-literal.xsl
endif
ifdef DOCBOOK_SUPPRESS_SP
  XMLTO_EXTRA += -m manpage-suppress-sp.xsl
endif

DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

export DESTDIR DESTDIR_SQ

# Please note that there is a minor bug in asciidoc.
# The version after 6.0.3 _will_ include the patch found here:
#   http://marc.theaimsgroup.com/?l=libtraceevent&m=111558757202243&w=2
#
# Until that version is released you may have to apply the patch
# yourself - yes, all 6 characters of it!

QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring $(MAKEFLAGS),w),w)
  PRINT_DIR = --no-print-directory
else # "make -w"
  NO_SUBDIR = :
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
  ifneq ($(V),1)
    QUIET_ASCIIDOC = @echo '  ASCIIDOC '$@;
    QUIET_XMLTO    = @echo '  XMLTO    '$@;
  endif
endif

all: $(MAN_X) $(MAN_HTML)

$(MAN_HTML) $(MAN_X): asciidoc.conf

install-man: all
	$(call QUIET_INSTALL, man) \
		$(INSTALL) -d -m 755 $(DESTDIR)$(man3dir); \
		$(INSTALL) -m 644 $(MAN_3) $(DESTDIR)$(man3dir); \
		$(INSTALL) -d -m 755 $(DESTDIR)$(man7dir); \
		$(INSTALL) -m 644 $(MAN_7) $(DESTDIR)$(man7dir);

install-html: $(MAN_HTML)
	$(call QUIET_INSTALL, html) \
		$(INSTALL) -d -m 755 $(DESTDIR)$(htmldir); \
		$(INSTALL) -m 644 $(MAN_HTML) $(DESTDIR)$(htmldir); \

install-examples:
	$(call QUIET_INSTALL, examples) \
		$(INSTALL) -d -m 755 $(DESTDIR)$(exdir); \
		$(INSTALL) -m 644 $(MAN_EX) $(DESTDIR)$(exdir); \

CLEAN_FILES =					\
	$(MAN_XML) $(addsuffix +,$(MAN_XML))	\
	$(MAN_HTML) $(addsuffix +,$(MAN_HTML))	\
	$(MAN_X)

clean:
	$(call QUIET_CLEAN, Documentation) $(RM) $(CLEAN_FILES)

$(MAN_3): $(OUTPUT)%.3: %.xml
	$(QUIET_XMLTO)$(XMLTO) -o $(OUTPUT). -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $<

$(MAN_7): $(OUTPUT)%.7: %.xml
	$(QUIET_XMLTO)$(XMLTO) -o $(OUTPUT). -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $<

$(MAN_XML): $(OUTPUT)%.xml: %.txt
	$(QUIET_ASCIIDOC)$(ASCIIDOC) -b docbook -d manpage \
		$(ASCIIDOC_EXTRA) -alibperf_version=$(EVENT_PARSE_VERSION) -o $@+ $< && \
	mv $@+ $@

$(MAN_HTML): $(OUTPUT)%.html: %.txt
	$(QUIET_ASCIIDOC)$(ASCIIDOC) -b $(ASCIIDOC_HTML) -d manpage \
	$(ASCIIDOC_EXTRA) -aperf_version=$(EVENT_PARSE_VERSION) -o $@+ $< && \
	mv $@+ $@
