#!/bin/bash
#
#  findmisopt
#
#      This is a quick and dirty hack to potentially find a misoptimization
#      problem. Mostly its to work around problems in bugpoint that prevent
#      it from finding a problem unless the set of failing optimizations are
#      known and given to it on the command line.
#
#      Given a bitcode file that produces correct output (or return code), 
#      this script will run through all the optimizations passes that gccas
#      uses (in the same order) and will narrow down which optimizations
#      cause the program either generate different output or return a 
#      different result code. When the passes have been narrowed down, 
#      bugpoint is invoked to further refine the problem to its origin. If a
#      release version of bugpoint is available it will be used, otherwise 
#      debug.
#
#   Usage:
#      findmisopt bcfile outdir progargs [match]
#
#   Where:
#      bcfile 
#          is the bitcode file input (the unoptimized working case)
#      outdir
#          is a directory into which intermediate results are placed
#      progargs
#          is a single argument containing all the arguments the program needs
#      proginput
#          is a file name from which stdin should be directed
#      match
#          if specified to any value causes the result code of the program to
#          be used to determine success/fail. If not specified success/fail is
#          determined by diffing the program's output with the non-optimized
#          output.
#       
if [ "$#" -lt 3 ] ; then
  echo "usage: findmisopt bcfile outdir progargs [match]"
  exit 1
fi

dir="${0%%/utils/findmisopt}"
if [ -x "$dir/Release/bin/bugpoint" ] ; then
  bugpoint="$dir/Release/bin/bugpoint"
elif [ -x "$dir/Debug/bin/bugpoint" ] ; then
  bugpoint="$dir/Debug/bin/bugpoint"
else
  echo "findmisopt: bugpoint not found"
  exit 1
fi

bcfile="$1"
outdir="$2"
args="$3"
input="$4"
if [ ! -f "$input" ] ; then
  input="/dev/null"
fi
match="$5"
name=`basename $bcfile .bc`
ll="$outdir/${name}.ll"
s="$outdir/${name}.s"
prog="$outdir/${name}"
out="$outdir/${name}.out"
optbc="$outdir/${name}.opt.bc"
optll="$outdir/${name}.opt.ll"
opts="$outdir/${name}.opt.s"
optprog="$outdir/${name}.opt"
optout="$outdir/${name}.opt.out"
ldflags="-lstdc++ -lm -ldl -lc"

echo "Test Name: $name"
echo "Unoptimized program: $prog"
echo "  Optimized program: $optprog"

# Define the list of optimizations to run. This comprises the same set of 
# optimizations that opt -O3 runs, in the same order.
opt_switches=`llvm-as < /dev/null -o - | opt -O3 -disable-output -debug-pass=Arguments 2>&1 | sed 's/Pass Arguments: //'`
all_switches="$opt_switches"
echo "Passes : $all_switches"

# Create output directory if it doesn't exist
if [ -f "$outdir" ] ; then
  echo "$outdir is not a directory"
  exit 1
fi

if [ ! -d "$outdir" ] ; then
  mkdir "$outdir" || exit 1
fi

# Generate the disassembly
llvm-dis "$bcfile" -o "$ll" -f || exit 1

# Generate the non-optimized program and its output
llc "$bcfile" -o "$s" -f || exit 1
gcc "$s" -o "$prog" $ldflags || exit 1
"$prog" $args > "$out" 2>&1 <$input
ex1=$?

# Current set of switches is empty
function tryit {
  switches_to_use="$1"
  opt $switches_to_use "$bcfile" -o "$optbc" -f || exit
  llvm-dis "$optbc" -o "$optll" -f || exit
  llc "$optbc" -o "$opts" -f || exit
  gcc "$opts" -o "$optprog" $ldflags || exit
  "$optprog" $args > "$optout" 2>&1 <"$input"
  ex2=$?

  if [ -n "$match" ] ; then
    if [ "$ex1" -ne "$ex2" ] ; then
      echo "Return code not the same with these switches:"
      echo $switches
      echo "Unoptimized returned: $ex1"
      echo "Optimized   returned: $ex2"
      return 0
    fi
  else
    diff "$out" "$optout" > /dev/null
    if [ $? -ne 0 ] ; then
      echo "Diff fails with these switches:"
      echo $switches
      echo "Differences:"
      diff "$out" "$optout" | head
      return 0;
    fi
  fi
  return 1
}

echo "Trying to find optimization that breaks program:"
for sw in $all_switches ; do
  echo -n " $sw"
  switches="$switches $sw"
  if tryit "$switches" ; then
    break;
  fi
done

# Terminate the previous output with a newline
echo ""

# Determine if we're done because none of the optimizations broke the program
if [ "$switches" == " $all_switches" ] ; then
  echo "The program did not miscompile"
  exit 0
fi

final=""
while [ ! -z "$switches" ] ; do
  trimmed=`echo "$switches" | sed -e 's/^ *\(-[^ ]*\).*/\1/'`
  switches=`echo "$switches" | sed -e 's/^ *-[^ ]* *//'`
  echo "Trimmed $trimmed from left"
  tryit "$final $switches"
  if [ "$?" -eq "0" ] ; then
    echo "Still Failing .. continuing ..."
    continue
  else
    echo "Found required early pass: $trimmed"
    final="$final $trimmed"
    continue
  fi
  echo "Next Loop"
done

if [ "$final" == " $all_switches" ] ; then
  echo "findmisopt: All optimizations pass. Perhaps this isn't a misopt?"
  exit 0
fi
echo "Smallest Optimization list=$final"

bpcmd="$bugpoint -run-llc -disable-loop-extraction --output "$out" --input /dev/null $bcfile $final --args $args"

echo "Running: $bpcmd"
$bpcmd
echo "findmisopt finished."
