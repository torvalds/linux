# $FreeBSD$
# .gdbinit file for remote serial debugging.
#
# XXX Do not use this file directly.  It contains parameters which are
# XXX substituted by the kernel Makefile when you do a 'make gdbinit'.
# XXX This also removes lines starting with '# XXX'.
# XXX
# To debug kernels, do:
#
#  cd /usr/obj/usr/src/sys/GENERIC   (or kernel build directory)
#  make gdbinit
#  gdb kernel.debug
#
# Read gdb(4) for more details.

# The following lines (down to "***** End" comment) may need to be changed

# Bit rate for serial link.  Due to problems in the interface,
# this may not work well above 9600 bps.
set remotebaud 9600		

set output-radix 16
set height 70
set width 120
set remotetimeout 1
set complaints 1
set print pretty
dir ../../..

# ***** End of things you're likely to need to change.

# Connect to remote target via a serial port.
define tr
# Remote debugging port
target remote $arg0
end

document tr
Debug a remote system via serial or firewire interface.  For example, specify 'tr /dev/cuau0' to use first serial port, or 'tr localhost:5556' for default firewire port.  See also tr0, tr1 and trf commands.
end

# Convenience functions.  These call tr.
# debug via cuau0
define tr0
tr /dev/cuau0
end
define tr1
tr /dev/cuau1
end
# Firewire
define trf
tr localhost:5556
end

document tr0
Debug a remote system via serial interface /dev/cuau0.  See also tr, tr1 and trf commands.
end
document tr1
Debug a remote system via serial interface /dev/cuau1.  See also tr, tr0 and trf commands.
end
document trf
Debug a remote system via firewire interface at default port 5556.  See also tr, tr0 and tr1 commands.
end

# Get symbols from klds.  Unfortunately, there are a number of
# landmines involved here:
#
# When debugging the same machine (via /dev/mem), we can get the
# script to call kldstat and pass the info on to asf(8).  This won't
# work for crashes or remote debugging, of course, because we'd get
# the information for the wrong system.  Instead, we use the macro
# "kldstat", which extracts the information from the "dump".  The
# trouble here is that it's a pain to use, since gdb doesn't have the
# capability to pass data to scripts, so we have to mark it and paste
# it into the script.  This makes it silly to use this method for
# debugging the local system.  Instead, we have two scripts:
#
# getsyms uses the information in the "dump", and you have to paste it.
# kldsyms uses the local kld information.
# 
# Improvements in gdb should make this go away some day.
#
define kldsyms
# This will be replaced by the path of the real modules directory.
shell asf -f -k MODPATH
source .asf
end
document kldsyms
Read in the symbol tables for the debugging machine.  This only makes sense when debugging /dev/mem; use the 'getsyms' macro for remote debugging.
end

# Remote system
define getsyms
kldstat
echo Select the list above with the mouse, paste into the screen\n
echo and then press ^D.  Yes, this is annoying.\n
# This will be replaced by the path of the real modules directory.
shell asf -f MODPATH
source .asf
end

document getsyms
Display kldstat information for the target machine and invite user to paste it back in.  This causes the symbols for the KLDs to be loaded.  When doing memory debugging, use the command kldsyms instead.
end

source gdbinit.kernel
source gdbinit.machine

echo Ready to go.  Enter 'tr' to connect to the remote target\n
echo with /dev/cuau0, 'tr /dev/cuau1' to connect to a different port\n
echo or 'trf portno' to connect to the remote target with the firewire\n
echo interface.  portno defaults to 5556.\n
echo \n
echo Type 'getsyms' after connection to load kld symbols.\n
echo \n
echo If you're debugging a local system, you can use 'kldsyms' instead\n
echo to load the kld symbols.  That's a less obnoxious interface.\n
