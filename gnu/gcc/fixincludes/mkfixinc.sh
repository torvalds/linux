#! /bin/sh

if [ $# -ne 1 ]
then
  echo "Usage: $0 <target-mach-triplet>"
  exit 1
fi

machine=$1
target=fixinc.sh

# Check for special fix rules for particular targets
case $machine in
    alpha*-dec-*vms* | \
    arm-semi-aof | \
    hppa1.1-*-osf* | \
    hppa1.1-*-bsd* | \
    i370-*-openedition | \
    i?86-moss-msdos* | \
    i?86-*-moss* | \
    i?86-*-pe | \
    i?86-*-cygwin* | \
    i?86-*-mingw32* | \
    i?86-*-uwin* | \
    i?86-*-interix* | \
    powerpc-*-eabiaix* | \
    powerpc-*-eabisim* | \
    powerpc-*-eabi*    | \
    powerpc-*-rtems*   | \
    powerpcle-*-eabisim* | \
    powerpcle-*-eabi* )
	#  IF there is no include fixing,
	#  THEN create a no-op fixer and exit
	(echo "#! /bin/sh" ; echo "exit 0" ) > ${target}
        ;;

    *)
	cat < ${srcdir}/fixinc.in > ${target} || exit 1
	;;
esac
chmod 755 ${target}
