# tdir that only exes the files.
args="../.."
if test "$1" = "-a"; then
	args=$2
	shift
	shift
fi

# This will keep the temporary directory around and return 1 when the test failed.
DEBUG=0
test -n "$DEBUG_TDIR" && DEBUG=1

quiet=0
if test "$1" = "-q"; then
	quiet=1
	shift
fi

if test "$1" = "clean"; then
	if test $quiet = 0; then
		echo "rm -f result.* .done* .skip* .tdir.var.master .tdir.var.test"
	fi
	rm -f result.* .done* .skip* .tdir.var.master .tdir.var.test
	exit 0
fi
if test "$1" = "fake"; then
	if test $quiet = 0; then
		echo "minitdir fake $2"
	fi
	echo "fake" > .done-`basename $2 .tdir`
	exit 0
fi
if test "$1" = "-f" && test "$2" = "report"; then
	echo "Minitdir Long Report"
	pass=0
	fail=0
	skip=0
	echo "   STATUS    ELAPSED TESTNAME TESTDESCRIPTION"
	for result in *.tdir; do
		name=`basename $result .tdir`
		timelen="     "
		desc=""
		if test -f "result.$name"; then
			timestart=`grep ^DateRunStart: "result.$name" | sed -e 's/DateRunStart: //'`
			timeend=`grep ^DateRunEnd: "result.$name" | sed -e 's/DateRunEnd: //'`
			timesec=`expr $timeend - $timestart`
			timelen=`printf %4ds $timesec`
			if test $? -ne 0; then
				timelen="$timesec""s"
			fi
			desc=`grep ^Description: "result.$name" | sed -e 's/Description: //'`
		fi
		if test -f ".done-$name"; then
			if test $quiet = 0; then
				echo "** PASSED ** $timelen $name: $desc"
				pass=`expr $pass + 1`
			fi
		elif test -f ".skip-$name"; then
			echo ".. SKIPPED.. $timelen $name: $desc"
			skip=`expr $skip + 1`
		else
			if test -f "result.$name"; then
				echo "!! FAILED !! $timelen $name: $desc"
				fail=`expr $fail + 1`
			else
				echo ".. SKIPPED.. $timelen $name: $desc"
				skip=`expr $skip + 1`
			fi
		fi
	done
	echo ""
	if test "$skip" = "0"; then
		echo "$pass pass, $fail fail"
	else
		echo "$pass pass, $fail fail, $skip skip"
	fi
	echo ""
	exit 0
fi
if test "$1" = "report" || test "$2" = "report"; then
	echo "Minitdir Report"
	for result in *.tdir; do
		name=`basename $result .tdir`
		if test -f ".done-$name"; then
			if test $quiet = 0; then
				echo "** PASSED ** : $name"
			fi
		elif test -f ".skip-$name"; then
			if test $quiet = 0; then
				echo ".. SKIPPED.. : $name"
			fi
		else
			if test -f "result.$name"; then
				echo "!! FAILED !! : $name"
			else
				if test $quiet = 0; then
					echo ".. SKIPPED.. : $name"
				fi
			fi
		fi
	done
	exit 0
fi

if test "$1" != 'exe'; then
	# usage
	echo "mini tdir. Reduced functionality for old shells."
	echo "	tdir [-q] exe <file>"
	echo "	tdir [-q] fake <file>"
	echo "	tdir [-q] clean"
	echo "	tdir [-q|-f] report"
	exit 1
fi
shift

# do not execute if the disk is too full
#DISKLIMIT=100000
# This check is not portable (to Solaris 10).
#avail=`df . | tail -1 | awk '{print $4}'`
#if test "$avail" -lt "$DISKLIMIT"; then
	#echo "minitdir: The disk is too full! Only $avail."
	#exit 1
#fi

name=`basename $1 .tdir`
dir=$name.$$
result=result.$name
done=.done-$name
skip=.skip-$name
asan_text="SUMMARY: AddressSanitizer"
success="no"
if test -x "`which bash`"; then
	shell="bash"
else
	shell="sh"
fi

# check already done
if test -f $done; then
	echo "minitdir $done exists. skip test."
	exit 0
fi

# Copy
if test $quiet = 0; then
	echo "minitdir copy $1 to $dir"
fi
mkdir $dir
if cp --help 2>&1 | grep -- "-a" >/dev/null; then
cp -a $name.tdir/* $dir/
else
cp -R $name.tdir/* $dir/
fi
cd $dir

# EXE
echo "minitdir exe $name" > $result
grep "Description:" $name.dsc >> $result 2>&1
echo "DateRunStart: "`date "+%s" 2>/dev/null` >> $result
if test -f $name.pre; then
	if test $quiet = 0; then
		echo "minitdir exe $name.pre"
	fi
	echo "minitdir exe $name.pre" >> $result
	$shell $name.pre $args >> $result
	exit_value=$?
	if test $exit_value -eq 3; then
		echo "$name: SKIPPED" >> $result
		echo "$name: SKIPPED" > ../$skip
		echo "$name: SKIPPED"
	elif test $exit_value -ne 0; then
		echo "Warning: $name.pre did not exit successfully"
	fi
fi
if test -f $name.test -a ! -f ../$skip; then
	if test $quiet = 0; then
		echo "minitdir exe $name.test"
	fi
	echo "minitdir exe $name.test" >> $result
	$shell $name.test $args >>$result 2>&1
	if test $? -ne 0; then
		echo "$name: FAILED" >> $result
		echo "$name: FAILED"
		success="no"
	else
		echo "$name: PASSED" >> $result
		echo "$name: PASSED" > ../$done
		if test $quiet = 0; then
			echo "$name: PASSED"
		fi
		success="yes"
	fi
fi
if test -f $name.post -a ! -f ../$skip; then
	if test $quiet = 0; then
		echo "minitdir exe $name.post"
	fi
	echo "minitdir exe $name.post" >> $result
	$shell $name.post $args >> $result
	if test $? -ne 0; then
		echo "Warning: $name.post did not exit successfully"
	fi
fi
# Check if there were any AddressSanitizer errors
# if compiled with -fsanitize=address
if grep "$asan_text" $result >/dev/null 2>&1; then
	if test -f ../$done; then
		rm ../$done
	fi
	echo "$name: FAILED (AddressSanitizer)" >> $result
	echo "$name: FAILED (AddressSanitizer)"
	success="no"
fi
echo "DateRunEnd: "`date "+%s" 2>/dev/null` >> $result

mv $result ..
cd ..
if test $DEBUG -eq 0; then
	rm -rf $dir
	# compat for windows where deletion may not succeed initially (files locked
	# by processes that still have to exit).
	if test $? -eq 1; then
		echo "minitdir waiting for processes to terminate"
		sleep 2 # some time to exit, and try again
		rm -rf $dir
	fi
else
	if test $success = "no"; then
		exit 1
	fi
	exit 0
fi
