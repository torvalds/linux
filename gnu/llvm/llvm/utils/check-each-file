#!/bin/sh
# check-each-file
# Used to narrow down a miscompilation to one .o file from a list. Please read
# the usage procedure, below, for command-line syntax (or run it with --help).
# This script depends on the llvm-native-gcc script.

if [ x$1 = x--make-linker-script ]
then
	program=$2
	linker=./link-$program
	echo "Building $program with llvm-native-gcc"
	rm -f $program
	gmake -e $program CC=llvm-native-gcc CXX=llvm-native-gxx
	echo "Erasing $program and re-linking it" 
	rm -f $program
	echo "rm -f $program" > $linker
	gmake -n $program >> $linker
	chmod 755 $linker
	echo "Linker script created in $linker; testing it out"
	output=`./$linker 2>&1`
	case "$output" in
		*undefined*reference*__main*) 
			echo "$program appears to need a dummy __main function; adding one"
			echo "void __main () { }" > __main.c
			gcc -c __main.c
			echo "Done; rebuilding $linker"
			echo "rm -f $program" > $linker
			gmake -n $program 2>&1 | sed '/gcc/s/$/__main.o/' >> $linker
			./$linker > /dev/null 2>&1
			if [ ! -x $program ]
			then
				echo "WARNING: linker script didn't work"
			fi
			;;
		*)
			if [ ! -x $program ]
			then
				echo "WARNING: linker script didn't work"
			fi
			;;
	esac
	echo "Linker script created in $linker; please check it manually"
	exit 0
fi

checkfiles="$1"
program="$2"
linker="$3"
checker="$4"

usage () {
	myname=`basename $0`
	echo "$myname --make-linker-script PROGRAM"
	echo "$myname OBJECTS-FILE PROGRAM LINKER CHECKER"
	echo ""
	echo "OBJECTS-FILE is a text file containing the names of all the .o files"
	echo "PROGRAM is the name of the executable under test"
	echo "(there must also exist a Makefile in the current directory which"
	echo "has PROGRAM as a target)"
	echo "LINKER is the script that builds PROGRAM; try --make-linker-script" 
	echo "to automatically generate it"
	echo "CHECKER is the script that exits 0 if PROGRAM is ok, 1 if it is not OK"
	echo "(LINKER and CHECKER must be in your PATH, or you should specify ./)"
	echo ""
	echo "Bugs to <gaeke@uiuc.edu>."
	exit 1
}

if [ x$1 = x--help ]
then
	usage
fi

if [ -z "$checkfiles" ]
then
	echo "ERROR: Must specify name of file w/ list of objects as 1st arg."
	echo "(got \"$checkfiles\")"
	usage
fi
if [ ! -f "$checkfiles" ]
then
	echo "ERROR: $checkfiles not found"
	usage
fi
if [ -z "$program" ]
then
	echo "ERROR: Must specify name of program as 2nd arg."
	usage
fi
if [ -z "$linker" ]
then
	echo "ERROR: Must specify name of link script as 3rd arg."
	usage
fi
if [ ! -x "$linker" ]
then
	echo "ERROR: $linker not found or not executable"
	echo "You may wish to try: $0 --make-linker-script $program"
	usage
fi
if [ -z "$checker" ]
then
	echo "ERROR: Must specify name of $program check script as 3rd arg."
	usage
fi
if [ ! -x "$checker" ]
then
	echo "ERROR: $checker not found or not executable"
	usage
fi

files=`cat $checkfiles`
echo "Recompiling everything with llvm-native-gcc"
for f in $files
do
	rm -f $f
	gmake $f CC=llvm-native-gcc CXX=llvm-native-gxx
done
rm -f $program
$linker
if $checker
then
	echo "Sorry, I can't help you, $program is OK when compiled with llvm-native-gcc"
	exit 1
fi
for f in $files
do
	echo Trying to compile $f with native gcc and rebuild $program
	mv ${f} ${f}__OLD__
	gmake ${f} CC=gcc > /dev/null 2>&1
	$linker
	echo Checking validity of new $program
	if $checker
	then
		echo Program is OK
		okfiles="$okfiles $f"
	else
		echo Program is not OK
		notokfiles="$notokfiles $f"
	fi
	mv ${f}__OLD__ ${f}
done
echo ""
echo "Program is OK when these files are recompiled with native gcc: "
echo "$okfiles"
echo ""
echo "Program is not OK when these files are recompiled with native gcc: "
echo "$notokfiles"
echo ""
exit 0
