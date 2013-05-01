# ===========================================================================
# Kernel configuration targets
# These targets are used from top-level makefile

PHONY += oldconfig xconfig gconfig menuconfig config silentoldconfig update-po-config \
	localmodconfig localyesconfig

ifdef KBUILD_KCONFIG
Kconfig := $(KBUILD_KCONFIG)
else
Kconfig := Kconfig
endif

# We need this, in case the user has it in its environment
unexport CONFIG_

xconfig: $(obj)/qconf
	$< $(Kconfig)

gconfig: $(obj)/gconf
	$< $(Kconfig)

menuconfig: $(obj)/mconf
	$< $(Kconfig)

config: $(obj)/conf
	$< --oldaskconfig $(Kconfig)

nconfig: $(obj)/nconf
	$< $(Kconfig)

oldconfig: $(obj)/conf
	$< --$@ $(Kconfig)

silentoldconfig: $(obj)/conf
	$(Q)mkdir -p include/generated
	$< --$@ $(Kconfig)

localyesconfig localmodconfig: $(obj)/streamline_config.pl $(obj)/conf
	$(Q)mkdir -p include/generated
	$(Q)perl $< --$@ $(srctree) $(Kconfig) > .tmp.config
	$(Q)if [ -f .config ]; then 					\
			cmp -s .tmp.config .config ||			\
			(mv -f .config .config.old.1;			\
			 mv -f .tmp.config .config;			\
			 $(obj)/conf --silentoldconfig $(Kconfig);	\
			 mv -f .config.old.1 .config.old)		\
	else								\
			mv -f .tmp.config .config;			\
			$(obj)/conf --silentoldconfig $(Kconfig);	\
	fi
	$(Q)rm -f .tmp.config

# Create new linux.pot file
# Adjust charset to UTF-8 in .po file to accept UTF-8 in Kconfig files
update-po-config: $(obj)/kxgettext $(obj)/gconf.glade.h
	$(Q)echo "  GEN     config.pot"
	$(Q)xgettext --default-domain=linux                         \
	    --add-comments --keyword=_ --keyword=N_                 \
	    --from-code=UTF-8                                       \
	    --files-from=$(srctree)/scripts/kconfig/POTFILES.in     \
	    --directory=$(srctree) --directory=$(objtree)           \
	    --output $(obj)/config.pot
	$(Q)sed -i s/CHARSET/UTF-8/ $(obj)/config.pot
	$(Q)(for i in `ls $(srctree)/arch/*/Kconfig      \
	    $(srctree)/arch/*/um/Kconfig`;               \
	    do                                           \
		echo "  GEN     $$i";                    \
		$(obj)/kxgettext $$i                     \
		     >> $(obj)/config.pot;               \
	    done )
	$(Q)echo "  GEN     linux.pot"
	$(Q)msguniq --sort-by-file --to-code=UTF-8 $(obj)/config.pot \
	    --output $(obj)/linux.pot
	$(Q)rm -f $(obj)/config.pot

PHONY += allnoconfig allyesconfig allmodconfig alldefconfig randconfig

allnoconfig allyesconfig allmodconfig alldefconfig randconfig: $(obj)/conf
	$< --$@ $(Kconfig)

PHONY += listnewconfig olddefconfig oldnoconfig savedefconfig defconfig

listnewconfig olddefconfig: $(obj)/conf
	$< --$@ $(Kconfig)

# oldnoconfig is an alias of olddefconfig, because people already are dependent
# on its behavior(sets new symbols to their default value but not 'n') with the
# counter-intuitive name.
oldnoconfig: $(obj)/conf
	$< --olddefconfig $(Kconfig)

savedefconfig: $(obj)/conf
	$< --$@=defconfig $(Kconfig)

defconfig: $(obj)/conf
ifeq ($(KBUILD_DEFCONFIG),)
	$< --defconfig $(Kconfig)
else
	@echo "*** Default configuration is based on '$(KBUILD_DEFCONFIG)'"
	$(Q)$< --defconfig=arch/$(SRCARCH)/configs/$(KBUILD_DEFCONFIG) $(Kconfig)
endif

%_defconfig: $(obj)/conf
	$(Q)$< --defconfig=arch/$(SRCARCH)/configs/$@ $(Kconfig)

# Help text used by make help
help:
	@echo  '  config	  - Update current config utilising a line-oriented program'
	@echo  '  nconfig         - Update current config utilising a ncurses menu based program'
	@echo  '  menuconfig	  - Update current config utilising a menu based program'
	@echo  '  xconfig	  - Update current config utilising a QT based front-end'
	@echo  '  gconfig	  - Update current config utilising a GTK based front-end'
	@echo  '  oldconfig	  - Update current config utilising a provided .config as base'
	@echo  '  localmodconfig  - Update current config disabling modules not loaded'
	@echo  '  localyesconfig  - Update current config converting local mods to core'
	@echo  '  silentoldconfig - Same as oldconfig, but quietly, additionally update deps'
	@echo  '  defconfig	  - New config with default from ARCH supplied defconfig'
	@echo  '  savedefconfig   - Save current config as ./defconfig (minimal config)'
	@echo  '  allnoconfig	  - New config where all options are answered with no'
	@echo  '  allyesconfig	  - New config where all options are accepted with yes'
	@echo  '  allmodconfig	  - New config selecting modules when possible'
	@echo  '  alldefconfig    - New config with all symbols set to default'
	@echo  '  randconfig	  - New config with random answer to all options'
	@echo  '  listnewconfig   - List new options'
	@echo  '  olddefconfig	  - Same as silentoldconfig but sets new symbols to their default value'

# lxdialog stuff
check-lxdialog  := $(srctree)/$(src)/lxdialog/check-lxdialog.sh

# Use recursively expanded variables so we do not call gcc unless
# we really need to do so. (Do not call gcc as part of make mrproper)
HOST_EXTRACFLAGS += $(shell $(CONFIG_SHELL) $(check-lxdialog) -ccflags) \
                    -DLOCALE

# ===========================================================================
# Shared Makefile for the various kconfig executables:
# conf:	  Used for defconfig, oldconfig and related targets
# nconf:  Used for the nconfig target.
#         Utilizes ncurses
# mconf:  Used for the menuconfig target
#         Utilizes the lxdialog package
# qconf:  Used for the xconfig target
#         Based on QT which needs to be installed to compile it
# gconf:  Used for the gconfig target
#         Based on GTK which needs to be installed to compile it
# object files used by all kconfig flavours

lxdialog := lxdialog/checklist.o lxdialog/util.o lxdialog/inputbox.o
lxdialog += lxdialog/textbox.o lxdialog/yesno.o lxdialog/menubox.o

conf-objs	:= conf.o  zconf.tab.o
mconf-objs     := mconf.o zconf.tab.o $(lxdialog)
nconf-objs     := nconf.o zconf.tab.o nconf.gui.o
kxgettext-objs	:= kxgettext.o zconf.tab.o
qconf-cxxobjs	:= qconf.o
qconf-objs	:= zconf.tab.o
gconf-objs	:= gconf.o zconf.tab.o

hostprogs-y := conf

ifeq ($(MAKECMDGOALS),nconfig)
	hostprogs-y += nconf
endif

ifeq ($(MAKECMDGOALS),menuconfig)
	hostprogs-y += mconf
endif

ifeq ($(MAKECMDGOALS),update-po-config)
	hostprogs-y += kxgettext
endif

ifeq ($(MAKECMDGOALS),xconfig)
	qconf-target := 1
endif
ifeq ($(MAKECMDGOALS),gconfig)
	gconf-target := 1
endif


ifeq ($(qconf-target),1)
	hostprogs-y += qconf
endif

ifeq ($(gconf-target),1)
	hostprogs-y += gconf
endif

clean-files	:= qconf.moc .tmp_qtcheck .tmp_gtkcheck
clean-files	+= zconf.tab.c zconf.lex.c zconf.hash.c gconf.glade.h
clean-files     += mconf qconf gconf nconf
clean-files     += config.pot linux.pot

# Check that we have the required ncurses stuff installed for lxdialog (menuconfig)
PHONY += $(obj)/dochecklxdialog
$(addprefix $(obj)/,$(lxdialog)): $(obj)/dochecklxdialog
$(obj)/dochecklxdialog:
	$(Q)$(CONFIG_SHELL) $(check-lxdialog) -check $(HOSTCC) $(HOST_EXTRACFLAGS) $(HOSTLOADLIBES_mconf)

always := dochecklxdialog

# Add environment specific flags
HOST_EXTRACFLAGS += $(shell $(CONFIG_SHELL) $(srctree)/$(src)/check.sh $(HOSTCC) $(HOSTCFLAGS))

# generated files seem to need this to find local include files
HOSTCFLAGS_zconf.lex.o	:= -I$(src)
HOSTCFLAGS_zconf.tab.o	:= -I$(src)

LEX_PREFIX_zconf	:= zconf
YACC_PREFIX_zconf	:= zconf

HOSTLOADLIBES_qconf	= $(KC_QT_LIBS)
HOSTCXXFLAGS_qconf.o	= $(KC_QT_CFLAGS)

HOSTLOADLIBES_gconf	= `pkg-config --libs gtk+-2.0 gmodule-2.0 libglade-2.0`
HOSTCFLAGS_gconf.o	= `pkg-config --cflags gtk+-2.0 gmodule-2.0 libglade-2.0` \
                          -Wno-missing-prototypes

HOSTLOADLIBES_mconf   = $(shell $(CONFIG_SHELL) $(check-lxdialog) -ldflags $(HOSTCC))

HOSTLOADLIBES_nconf	= -lmenu -lpanel -lncurses
$(obj)/qconf.o: $(obj)/.tmp_qtcheck

ifeq ($(qconf-target),1)
$(obj)/.tmp_qtcheck: $(src)/Makefile
-include $(obj)/.tmp_qtcheck

# QT needs some extra effort...
$(obj)/.tmp_qtcheck:
	@set -e; echo "  CHECK   qt"; dir=""; pkg=""; \
	if ! pkg-config --exists QtCore 2> /dev/null; then \
	    echo "* Unable to find the QT4 tool qmake. Trying to use QT3"; \
	    pkg-config --exists qt 2> /dev/null && pkg=qt; \
	    pkg-config --exists qt-mt 2> /dev/null && pkg=qt-mt; \
	    if [ -n "$$pkg" ]; then \
	      cflags="\$$(shell pkg-config $$pkg --cflags)"; \
	      libs="\$$(shell pkg-config $$pkg --libs)"; \
	      moc="\$$(shell pkg-config $$pkg --variable=prefix)/bin/moc"; \
	      dir="$$(pkg-config $$pkg --variable=prefix)"; \
	    else \
	      for d in $$QTDIR /usr/share/qt* /usr/lib/qt*; do \
	        if [ -f $$d/include/qconfig.h ]; then dir=$$d; break; fi; \
	      done; \
	      if [ -z "$$dir" ]; then \
	        echo >&2 "*"; \
	        echo >&2 "* Unable to find any QT installation. Please make sure that"; \
	        echo >&2 "* the QT4 or QT3 development package is correctly installed and"; \
	        echo >&2 "* either qmake can be found or install pkg-config or set"; \
	        echo >&2 "* the QTDIR environment variable to the correct location."; \
	        echo >&2 "*"; \
	        false; \
	      fi; \
	      libpath=$$dir/lib; lib=qt; osdir=""; \
	      $(HOSTCXX) -print-multi-os-directory > /dev/null 2>&1 && \
	        osdir=x$$($(HOSTCXX) -print-multi-os-directory); \
	      test -d $$libpath/$$osdir && libpath=$$libpath/$$osdir; \
	      test -f $$libpath/libqt-mt.so && lib=qt-mt; \
	      cflags="-I$$dir/include"; \
	      libs="-L$$libpath -Wl,-rpath,$$libpath -l$$lib"; \
	      moc="$$dir/bin/moc"; \
	    fi; \
	    if [ ! -x $$dir/bin/moc -a -x /usr/bin/moc ]; then \
	      echo "*"; \
	      echo "* Unable to find $$dir/bin/moc, using /usr/bin/moc instead."; \
	      echo "*"; \
	      moc="/usr/bin/moc"; \
	    fi; \
	else \
	  cflags="\$$(shell pkg-config QtCore QtGui Qt3Support --cflags)"; \
	  libs="\$$(shell pkg-config QtCore QtGui Qt3Support --libs)"; \
	  moc="\$$(shell pkg-config QtCore --variable=moc_location)"; \
	  [ -n "$$moc" ] || moc="\$$(shell pkg-config QtCore --variable=prefix)/bin/moc"; \
	fi; \
	echo "KC_QT_CFLAGS=$$cflags" > $@; \
	echo "KC_QT_LIBS=$$libs" >> $@; \
	echo "KC_QT_MOC=$$moc" >> $@
endif

$(obj)/gconf.o: $(obj)/.tmp_gtkcheck

ifeq ($(gconf-target),1)
-include $(obj)/.tmp_gtkcheck

# GTK needs some extra effort, too...
$(obj)/.tmp_gtkcheck:
	@if `pkg-config --exists gtk+-2.0 gmodule-2.0 libglade-2.0`; then		\
		if `pkg-config --atleast-version=2.0.0 gtk+-2.0`; then			\
			touch $@;								\
		else									\
			echo >&2 "*"; 							\
			echo >&2 "* GTK+ is present but version >= 2.0.0 is required.";	\
			echo >&2 "*";							\
			false;								\
		fi									\
	else										\
		echo >&2 "*"; 								\
		echo >&2 "* Unable to find the GTK+ installation. Please make sure that"; 	\
		echo >&2 "* the GTK+ 2.0 development package is correctly installed..."; 	\
		echo >&2 "* You need gtk+-2.0, glib-2.0 and libglade-2.0."; 		\
		echo >&2 "*"; 								\
		false;									\
	fi
endif

$(obj)/zconf.tab.o: $(obj)/zconf.lex.c $(obj)/zconf.hash.c

$(obj)/qconf.o: $(obj)/qconf.moc

quiet_cmd_moc = MOC     $@
      cmd_moc = $(KC_QT_MOC) -i $< -o $@

$(obj)/%.moc: $(src)/%.h $(obj)/.tmp_qtcheck
	$(call cmd,moc)

# Extract gconf menu items for I18N support
$(obj)/gconf.glade.h: $(obj)/gconf.glade
	$(Q)intltool-extract --type=gettext/glade --srcdir=$(srctree) \
	$(obj)/gconf.glade

