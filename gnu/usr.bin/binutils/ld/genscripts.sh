#!/bin/sh
# genscripts.sh - generate the ld-emulation-target specific files
#
# Usage: genscripts.sh srcdir libdir host target target_alias \
# default_emulation native_lib_dirs this_emulation
#
# Sample usage:
# genscripts.sh /djm/ld-devo/devo/ld /usr/local/lib sparc-sun-sunos4.1.3 \
# sparc-sun-sunos4.1.3 sparc-sun-sunos4.1.3 sun4 "" sun3 sparc-sun-sunos4.1.3
# produces sun3.x sun3.xbn sun3.xn sun3.xr sun3.xu em_sun3.c
#
# $FreeBSD$
#
# This is a cut-down version of the GNU script. Instead of jumping through
# hoops for all possible combinations of paths, just use the libdir
# argument in place of LIB_PATH.
#
# The exec_prefix, target_alias, use_sysroot, NATIVE_LIB_DIRS, TOOL_LIB, CUSTOMIZER_SCRIPT
# arguments are not used in this version.
#

srcdir=$1
libdir=$2
exec_prefix=$3
host=$4
target=$5
target_alias=$6
EMULATION_LIBPATH=$7
NATIVE_LIB_DIRS=$8
use_sysroot=$9
shift 9
EMULATION_NAME=$1
TOOL_LIB=$2
CUSTOMIZER_SCRIPT=$3

# Create the 'CUSTOMIZER_SCRIPT' knob to better sync this script with
# FSF BU ver 2.15 which allows for a more generic emulparams processing.
# To reduce the diff, I also include the ${EMULATION_NAME} parameter in uses
# of 'CUSTOMIZER_SCRIPT'.

# XXX: arm hack : until those file are merged back into the FSF repo, just
# use the version in this directory.
if !(test -f ${CUSTOMIZER_SCRIPT}"";) then
CUSTOMIZER_SCRIPT="${srcdir}/emulparams/${EMULATION_NAME}.sh"
fi

# Include the emulation-specific parameters:
. ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}

if test -d ldscripts; then
  true
else
  mkdir -p ldscripts
fi

# Set some flags for the emultempl scripts.  USE_LIBPATH will
# be set for any libpath-using emulation; NATIVE will be set for a
# emulation to enable 'LD_LIBRARY_PATH=/foo:/bar ld -lfooz'
    if [ "x${host}" = "x${target}" ] ; then
      NATIVE=yes
    fi
  USE_LIBPATH=yes

# Set the library search path, for libraries named by -lfoo.
# If LIB_PATH is defined (e.g., by Makefile) and non-empty, it is used.
# Otherwise, the default is set here.
#
# The format is the usual list of colon-separated directories.
# To force a logically empty LIB_PATH, do LIBPATH=":".

LIB_SEARCH_DIRS=`echo ${libdir} | sed -e 's/:/ /g' -e 's/\([^ ][^ ]*\)/SEARCH_DIR(\1);/g'`

# Generate 5 or 6 script files from a master script template in
# ${srcdir}/scripttempl/${SCRIPT_NAME}.sh.  Which one of the 5 or 6
# script files is actually used depends on command line options given
# to ld.  (SCRIPT_NAME was set in the emulparams_file.)
#
# A .x script file is the default script.
# A .xr script is for linking without relocation (-r flag).
# A .xu script is like .xr, but *do* create constructors (-Ur flag).
# A .xn script is for linking with -n flag (mix text and data on same page).
# A .xbn script is for linking with -N flag (mix text and data on same page).
# A .xs script is for generating a shared library with the --shared
#   flag; it is only generated if $GENERATE_SHLIB_SCRIPT is set by the
#   emulation parameters.
# A .xc script is for linking with -z combreloc; it is only generated if
#   $GENERATE_COMBRELOC_SCRIPT is set by the emulation parameters or
#   $SCRIPT_NAME is "elf".
# A .xsc script is for linking with --shared -z combreloc; it is generated
#   if $GENERATE_COMBRELOC_SCRIPT is set by the emulation parameters or
#   $SCRIPT_NAME is "elf" and $GENERATE_SHLIB_SCRIPT is set by the emulation
#   parameters too.

if [ "x$SCRIPT_NAME" = "xelf" ]; then
  GENERATE_COMBRELOC_SCRIPT=yes
fi

SEGMENT_SIZE=${SEGMENT_SIZE-${MAXPAGESIZE-${TARGET_PAGE_SIZE}}}

# Determine DATA_ALIGNMENT for the 5 variants, using
# values specified in the emulparams/<script_to_run>.sh file or default.

DATA_ALIGNMENT_="${DATA_ALIGNMENT_-${DATA_ALIGNMENT-ALIGN(${SEGMENT_SIZE})}}"
DATA_ALIGNMENT_n="${DATA_ALIGNMENT_n-${DATA_ALIGNMENT_}}"
DATA_ALIGNMENT_N="${DATA_ALIGNMENT_N-${DATA_ALIGNMENT-.}}"
DATA_ALIGNMENT_r="${DATA_ALIGNMENT_r-${DATA_ALIGNMENT-}}"
DATA_ALIGNMENT_u="${DATA_ALIGNMENT_u-${DATA_ALIGNMENT_r}}"

LD_FLAG=r
DATA_ALIGNMENT=${DATA_ALIGNMENT_r}
DEFAULT_DATA_ALIGNMENT="ALIGN(${SEGMENT_SIZE})"
( echo "/* Script for ld -r: link without relocation */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xr

LD_FLAG=u
DATA_ALIGNMENT=${DATA_ALIGNMENT_u}
CONSTRUCTING=" "
( echo "/* Script for ld -Ur: link w/out relocation, do create constructors */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xu

LD_FLAG=
DATA_ALIGNMENT=${DATA_ALIGNMENT_}
RELOCATING=" "
( echo "/* Default linker script, for normal executables */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.x

LD_FLAG=n
DATA_ALIGNMENT=${DATA_ALIGNMENT_n}
TEXT_START_ADDR=${NONPAGED_TEXT_START_ADDR-${TEXT_START_ADDR}}
( echo "/* Script for -n: mix text and data on same page */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xn

LD_FLAG=N
DATA_ALIGNMENT=${DATA_ALIGNMENT_N}
( echo "/* Script for -N: mix text and data on same page; don't align data */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xbn

if test -n "$GENERATE_COMBRELOC_SCRIPT"; then
  DATA_ALIGNMENT=${DATA_ALIGNMENT_c-${DATA_ALIGNMENT_}}
  LD_FLAG=c
  COMBRELOC=ldscripts/${EMULATION_NAME}.xc.tmp
  ( echo "/* Script for -z combreloc: combine and sort reloc sections */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xc
  rm -f ${COMBRELOC}
  LD_FLAG=w
  RELRO_NOW=" "
  COMBRELOC=ldscripts/${EMULATION_NAME}.xw.tmp
  ( echo "/* Script for -z combreloc -z now -z relro: combine and sort reloc sections */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xw
  rm -f ${COMBRELOC}
  COMBRELOC=
  unset RELRO_NOW
fi

if test -n "$GENERATE_SHLIB_SCRIPT"; then
  LD_FLAG=shared
  DATA_ALIGNMENT=${DATA_ALIGNMENT_s-${DATA_ALIGNMENT_}}
  CREATE_SHLIB=" "
  # Note that TEXT_START_ADDR is set to NONPAGED_TEXT_START_ADDR.
  (
    echo "/* Script for ld --shared: link shared library */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xs
  if test -n "$GENERATE_COMBRELOC_SCRIPT"; then
    LD_FLAG=cshared
    DATA_ALIGNMENT=${DATA_ALIGNMENT_sc-${DATA_ALIGNMENT}}
    COMBRELOC=ldscripts/${EMULATION_NAME}.xsc.tmp
    ( echo "/* Script for --shared -z combreloc: shared library, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xsc
    rm -f ${COMBRELOC}
    LD_FLAG=wshared
    RELRO_NOW=" "
    COMBRELOC=ldscripts/${EMULATION_NAME}.xsw.tmp
    ( echo "/* Script for --shared -z combreloc -z now -z relro: shared library, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xsw
    rm -f ${COMBRELOC}
    COMBRELOC=
    unset RELRO_NOW
  fi
  unset CREATE_SHLIB
fi

if test -n "$GENERATE_PIE_SCRIPT"; then
  LD_FLAG=pie
  DATA_ALIGNMENT=${DATA_ALIGNMENT_s-${DATA_ALIGNMENT_}}
  CREATE_PIE=" "
  # Note that TEXT_START_ADDR is set to NONPAGED_TEXT_START_ADDR.
  (
    echo "/* Script for ld -pie: link position independent executable */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xd
  if test -n "$GENERATE_COMBRELOC_SCRIPT"; then
    LD_FLAG=cpie
    DATA_ALIGNMENT=${DATA_ALIGNMENT_sc-${DATA_ALIGNMENT}}
    COMBRELOC=ldscripts/${EMULATION_NAME}.xdc.tmp
    ( echo "/* Script for -pie -z combreloc: position independent executable, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xdc
    rm -f ${COMBRELOC}
    LD_FLAG=wpie
    RELRO_NOW=" "
    COMBRELOC=ldscripts/${EMULATION_NAME}.xdw.tmp
    ( echo "/* Script for -pie -z combreloc -z now -z relro: position independent executable, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xdw
    rm -f ${COMBRELOC}
    COMBRELOC=
    unset RELRO_NOW
  fi
  unset CREATE_PIE
fi

case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*) COMPILE_IN=true;;
esac

# Generate e${EMULATION_NAME}.c.
. ${srcdir}/emultempl/${TEMPLATE_NAME-generic}.em
