#!/bin/bash
#
#  findoptdiff
#
#      This script helps find the optimization difference between two llvm
#      builds. It is useful when you have a build that is known to work and
#      one that exhibits an optimization problem. Identifying the difference
#      between the two builds can lead to discovery of the source of a
#      mis-optimization.
#
#      The script takes two llvm build paths as arguments. These specify the
#      the two llvm builds to compare. It is generally expected that they
#      are "close cousins".  That is, they are the same except that the 
#      second build contains some experimental optimization features that
#      are suspected of producing a misoptimization.
#
#      The script takes two bitcode files, one from each build. They are
#      presumed to be a compilation of the same program or program fragment
#      with the only difference being the builds.
#
#      The script operates by iteratively applying the optimizations that gccas
#      and gccld run until there is a difference in the assembly resulting
#      from the optimization. The difference is then reported with the set of
#      optimization passes that produce the difference.  The processing 
#      continues until all optimization passes have been tried. The differences
#      for each pass, if they do differ, are placed in a diffs.# file.
#
#      To work around differences in the assembly language format, the script
#      can also take two filter arguments that post-process the assembly 
#      so they can be differenced without making false positives for known
#      differences in the two builds. These filters are optional.
#
#   Usage:
#      findoptdiff llvm1 llvm2 bc1 bc2 filter1 filter2
#
#   Where:
#      llvm1
#          is the path to the first llvm build dir
#      llvm2
#          is the path to the second llvm build dir
#      bc1
#          is the bitcode file for the first llvm environment
#      bc2
#          is the bitcode file for the second llvm environment
#      filter1
#          is an optional filter for filtering the llvm1 generated assembly
#      filter2
#          is an optional filter for filtering the llvm2 generated assembly
#       
llvm1=$1
llvm2=$2
bc1=$3
bc2=$4
filt1=$5
filt2=$6
if [ -z "$filt1" ] ; then
  filt1="cat"
fi
if [ -z "$filt2" ] ; then
  filt2="cat"
fi
opt1="${bc1}.opt"
opt2="${bc2}.opt" 
ll1="${bc1}.ll"
ll2="${bc2}.ll"
opt1ll="${bc1}.opt.ll"
opt2ll="${bc2}.opt.ll"
dis1="$llvm1/Debug/bin/llvm-dis"
dis2="$llvm2/Debug/bin/llvm-dis"
opt1="$llvm1/Debug/bin/opt"
opt2="$llvm2/Debug/bin/opt"

all_switches="-verify -lowersetjmp -simplifycfg -mem2reg -globalopt -globaldce -deadargelim -instcombine -simplifycfg -prune-eh -inline -simplify-libcalls -argpromotion -tailduplicate -simplifycfg -sroa -instcombine -predsimplify -condprop -tailcallelim -simplifycfg -reassociate -licm -loop-unswitch -instcombine -indvars -loop-unroll -instcombine -load-vn -gcse -sccp -instcombine -condprop -dse -dce -simplifycfg -deadtypeelim -constmerge -internalize -ipsccp -globalopt -constmerge -deadargelim -inline -prune-eh -globalopt -globaldce -argpromotion -instcombine -predsimplify -sroa -globalsmodref-aa -licm -load-vn -gcse -dse -instcombine -simplifycfg -verify"

#counter=0
function tryit {
  switches_to_use="$1"
  $opt1 $switches_to_use "$bc1" -o - | $dis1 | $filt1 > "$opt1ll"
  $opt2 $switches_to_use "$bc2" -o - | $dis2 | $filt2 > "$opt2ll"
  diffs="diffs."$((counter++))
  diff "$opt1ll" "$opt2ll" > $diffs
  if [ $? -ne 0 ] ; then
    echo
    echo "Diff fails with these switches:"
    echo $switches
    echo "Differences:"
    head $diffs
    echo 'Switches:' $switches_to_use >> $diffs
  else
    rm $diffs
  fi
  return 1
}

for sw in $all_switches ; do
  echo -n " $sw"
  switches="$switches $sw"
  if tryit "$switches" ; then
    break;
  fi
done
