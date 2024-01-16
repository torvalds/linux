include ../../../scripts/Makefile.include
include ../../../scripts/utilities.mak

# This Makefile and manpage XSL files were taken from tools/perf/Documentation
# and modified for libtraceevent.

MAN3_TXT= \
	$(wildcard libtraceevent-*.txt) \
	libtraceevent.txt

MAN_TXT = $(MAN3_TXT)
_MAN_XML=$(patsubst %.txt,%.xml,$(MAN_TXT))
_MAN_HTML=$(patsubst %.txt,%.html,$(MAN_TXT))
_DOC_MAN3=$(patsubst %.txt,%.3,$(MAN3_TXT))

MAN_XML=$(addprefix $(OUTPUT),$(_MAN_XML))
MAN_HTML=$(addprefix $(OUTPUT),$(_MAN_HTML))
DOC_MAN3=$(addprefix $(OUTPUT),$(_DOC_MAN3))

# Make the path relative to DESTDIR, not prefix
ifndef DESTDIR
prefix?=$(HOME)
endif
bindir?=$(prefix)/bin
htmldir?=$(prefix)/share/doc/libtraceevent-doc
pdfdir?=$(prefix)/share/doc/libtraceevent-doc
mandir?=$(prefix)/share/man
man3dir=$(mandir)/man3

ASCIIDOC=asciidoc
ASCIIDOC_EXTRA = --unsafe -f asciidoc.conf
ASCIIDOC_HTML = xhtml11
MANPAGE_XSL = manpage-normal.xsl
XMLTO_EXTRA =
INSTALL?=install
RM ?= rm -f

ifdef USE_ASCIIDOCTOR
ASCIIDOC = asciidoctor
ASCIIDOC_EXTRA = -a compat-mode
ASCIIDOC_EXTRA += -I. -rasciidoctor-extensions
ASCIIDOC_EXTRA += -a mansource="libtraceevent" -a manmanual="libtraceevent Manual"
ASCIIDOC_HTML = xhtml5
endif

XMLTO=xmlto

_tmp_tool_path := $(call get-executable,$(ASCIIDOC))
ifeq ($(_tmp_tool_path),)
	missing_tools = $(ASCIIDOC)
endif

ifndef USE_ASCIIDOCTOR
_tmp_tool_path := $(call get-executable,$(XMLTO))
ifeq ($(_tmp_tool_path),)
	missing_tools += $(XMLTO)
endif
endif

#
# For asciidoc ...
#	-7.1.2,	no extra settings are needed.
#	8.0-,	set ASCIIDOC8.
#

#
# For docbook-xsl ...
#	-1.68.1,	set ASCIIDOC_NO_ROFF? (based on changelog from 1.73.0)
#	1.69.0,		no extra settings are needed?
#	1.69.1-1.71.0,	set DOCBOOK_SUPPRESS_SP?
#	1.71.1,		no extra settings are needed?
#	1.72.0,		set DOCBOOK_XSL_172.
#	1.73.0-,	set ASCIIDOC_NO_ROFF
#

#
# If you had been using DOCBOOK_XSL_172 in an attempt to get rid
# of 'the ".ft C" problem' in your generated manpages, and you
# instead ended up with weird characters around callouts, try
# using ASCIIDOC_NO_ROFF instead (it works fine with ASCIIDOC8).
#

ifdef ASCIIDOC8
ASCIIDOC_EXTRA += -a asciidoc7compatible
endif
ifdef DOCBOOK_XSL_172
ASCIIDOC_EXTRA += -a libtraceevent-asciidoc-no-roff
MANPAGE_XSL = manpage-1.72.xsl
else
	ifdef ASCIIDOC_NO_ROFF
	# docbook-xsl after 1.72 needs the regular XSL, but will not
	# pass-thru raw roff codes from asciidoc.conf, so turn them off.
	ASCIIDOC_EXTRA += -a libtraceevent-asciidoc-no-roff
	endif
endif
ifdef MAN_BOLD_LITERAL
XMLTO_EXTRA += -m manpage-bold-literal.xsl
endif
ifdef DOCBOOK_SUPPRESS_SP
XMLTO_EXTRA += -m manpage-suppress-sp.xsl
endif

SHELL_PATH ?= $(SHELL)
# Shell quote;
SHELL_PATH_SQ = $(subst ','\'',$(SHELL_PATH))

DESTDIR ?=
DESTDIR_SQ = '$(subst ','\'',$(DESTDIR))'

export DESTDIR DESTDIR_SQ

#
# Please note that there is a minor bug in asciidoc.
# The version after 6.0.3 _will_ include the patch found here:
#   http://marc.theaimsgroup.com/?l=libtraceevent&m=111558757202243&w=2
#
# Until that version is released you may have to apply the patch
# yourself - yes, all 6 characters of it!
#
QUIET_SUBDIR0  = +$(MAKE) -C # space to separate -C and subdir
QUIET_SUBDIR1  =

ifneq ($(findstring $(MAKEFLAGS),w),w)
PRINT_DIR = --no-print-directory
else # "make -w"
NO_SUBDIR = :
endif

ifneq ($(findstring $(MAKEFLAGS),s),s)
ifneq ($(V),1)
	QUIET_ASCIIDOC	= @echo '  ASCIIDOC '$@;
	QUIET_XMLTO	= @echo '  XMLTO    '$@;
	QUIET_SUBDIR0	= +@subdir=
	QUIET_SUBDIR1	= ;$(NO_SUBDIR) \
			   echo '  SUBDIR   ' $$subdir; \
			  $(MAKE) $(PRINT_DIR) -C $$subdir
	export V
endif
endif

all: html man

man: man3
man3: $(DOC_MAN3)

html: $(MAN_HTML)

$(MAN_HTML) $(DOC_MAN3): asciidoc.conf

install: install-man

check-man-tools:
ifdef missing_tools
	$(error "You need to install $(missing_tools) for man pages")
endif

do-install-man: man
	$(call QUIET_INSTALL, Documentation-man) \
		$(INSTALL) -d -m 755 $(DESTDIR)$(man3dir); \
		$(INSTALL) -m 644 $(DOC_MAN3) $(DESTDIR)$(man3dir);

install-man: check-man-tools man do-install-man

uninstall: uninstall-man

uninstall-man:
	$(call QUIET_UNINST, Documentation-man) \
		$(Q)$(RM) $(addprefix $(DESTDIR)$(man3dir)/,$(DOC_MAN3))


ifdef missing_tools
  DO_INSTALL_MAN = $(warning Please install $(missing_tools) to have the man pages installed)
else
  DO_INSTALL_MAN = do-install-man
endif

CLEAN_FILES =					\
	$(MAN_XML) $(addsuffix +,$(MAN_XML))	\
	$(MAN_HTML) $(addsuffix +,$(MAN_HTML))	\
	$(DOC_MAN3) *.3

clean:
	$(call QUIET_CLEAN, Documentation) $(RM) $(CLEAN_FILES)

ifdef USE_ASCIIDOCTOR
$(OUTPUT)%.3 : $(OUTPUT)%.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(ASCIIDOC) -b manpage -d manpage \
		$(ASCIIDOC_EXTRA) -alibtraceevent_version=$(EVENT_PARSE_VERSION) -o $@+ $< && \
	mv $@+ $@
endif

$(OUTPUT)%.3 : $(OUTPUT)%.xml
	$(QUIET_XMLTO)$(RM) $@ && \
	$(XMLTO) -o $(OUTPUT). -m $(MANPAGE_XSL) $(XMLTO_EXTRA) man $<

$(OUTPUT)%.xml : %.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(ASCIIDOC) -b docbook -d manpage \
		$(ASCIIDOC_EXTRA) -alibtraceevent_version=$(EVENT_PARSE_VERSION) -o $@+ $< && \
	mv $@+ $@

$(MAN_HTML): $(OUTPUT)%.html : %.txt
	$(QUIET_ASCIIDOC)$(RM) $@+ $@ && \
	$(ASCIIDOC) -b $(ASCIIDOC_HTML) -d manpage \
		$(ASCIIDOC_EXTRA) -aperf_version=$(EVENT_PARSE_VERSION) -o $@+ $< && \
	mv $@+ $@
