#!/bin/sh
# $FreeBSD$
#
# Unpack an ACPI CA drop and restructure it to fit the FreeBSD layout
#

if [ ! $# -eq 1 ]; then
	echo "usage: $0 acpica_archive"
	exit
fi

src=$1
wrk="$(realpath .)/_acpi_ca_unpack"
dst="$(realpath .)/acpi_ca_destination"

# files that should keep their full directory path
fulldirs="common compiler components include os_specific"

# files to remove
stripdirs="generate libraries parsers preprocessor tests tools"
stripfiles="Makefile README accygwin.h acdragonfly.h acdragonflyex.h	\
	acefi.h acefiex.h achaiku.h acintel.h aclinux.h aclinuxex.h	\
	acmacosx.h acmsvc.h acmsvcex.h acnetbsd.h acos2.h acqnx.h	\
	acwin.h acwin64.h acwinex.h new_table.txt osbsdtbl.c osefitbl.c	\
	osefixf.c osfreebsdtbl.c oslinuxtbl.c osunixdir.c osunixmap.c	\
	oswindir.c oswintbl.c oswinxf.c readme.txt utclib.c utprint.c"

# include files to canonify
src_headers="acapps.h acbuffer.h acclib.h accommon.h acconfig.h		\
	acconvert.h acdebug.h acdisasm.h acdispat.h acevents.h		\
	acexcep.h acglobal.h achware.h acinterp.h aclocal.h acmacros.h	\
	acnames.h acnamesp.h acobject.h acopcode.h acoutput.h		\
	acparser.h acpi.h acpiosxf.h acpixf.h acpredef.h acresrc.h	\
	acrestyp.h acstruct.h actables.h actbinfo.h actbl.h actbl1.h	\
	actbl2.h actbl3.h actypes.h acutils.h acuuid.h amlcode.h	\
	amlresrc.h platform/acenv.h platform/acenvex.h			\
	platform/acfreebsd.h platform/acgcc.h"
comp_headers="aslcompiler.h asldefine.h aslglobal.h aslmessages.h	\
	aslsupport.l asltypes.h dtcompiler.h dttemplate.h preprocess.h"
platform_headers="acfreebsd.h acgcc.h"

# pre-clean
echo pre-clean
rm -rf ${wrk} ${dst}
mkdir -p ${wrk}
mkdir -p ${dst}

# unpack
echo unpack
tar -x -z -f ${src} -C ${wrk}

# strip files
echo strip
for i in ${stripdirs}; do
	find ${wrk} -name ${i} -type d -print | xargs rm -r
done
for i in ${stripfiles}; do
	find ${wrk} -name ${i} -type f -delete
done

# copy files
echo copying full dirs
for i in ${fulldirs}; do
	find ${wrk} -name ${i} -type d -print | xargs -J % mv % ${dst}
done
echo copying remaining files
find ${wrk} -type f -print | xargs -J % mv % ${dst}

# canonify include paths
for H in ${src_headers}; do
	find ${dst} -name "*.[chly]" -type f -print |	\
	xargs sed -i "" -e "s|[\"<]$H[\">]|\<contrib/dev/acpica/include/$H\>|g"
done
for H in ${comp_headers}; do
	find ${dst}/common ${dst}/compiler ${dst}/components \
	    -name "*.[chly]" -type f |	\
	xargs sed -i "" -e "s|[\"<]$H[\">]|\<contrib/dev/acpica/compiler/$H\>|g"
done
for H in ${platform_headers}; do
	find ${dst}/include/platform -name "*.h" -type f -print |	\
	xargs sed -i "" -e "s|[\"<]$H[\">]|\<contrib/dev/acpica/include/platform/$H\>|g"
done

# post-clean
echo post-clean
rm -rf ${wrk}

# assist the developer in generating a diff
echo "Directories you may want to 'svn diff':"
echo "    sys/contrib/dev/acpica sys/dev/acpica \\"
echo "    sys/amd64/acpica sys/arm64/acpica sys/i386/acpica sys/x86/acpica \\"
echo "    sys/amd64/include sys/arm64/include sys/i386/include include \\"
echo "    stand sys/conf sys/modules/acpi usr.sbin/acpi"
