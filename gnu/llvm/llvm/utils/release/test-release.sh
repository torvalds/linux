#!/usr/bin/env bash
#===-- test-release.sh - Test the LLVM release candidates ------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#
#
# Download, build, and test the release candidate for an LLVM release.
#
#===------------------------------------------------------------------------===#

System=`uname -s`
Machine=`uname -m`
if [ "$System" = "FreeBSD" ]; then
    MAKE=gmake
else
    MAKE=make
fi
generator="Unix Makefiles"

Release=""
Release_no_dot=""
RC=""
Triple=""
use_gzip="no"
do_checkout="yes"
do_debug="no"
do_asserts="no"
do_compare="yes"
do_rt="yes"
do_clang_tools="yes"
do_libs="yes"
do_libcxxabi="yes"
do_libunwind="yes"
do_test_suite="yes"
do_openmp="yes"
do_lld="yes"
do_lldb="yes"
do_polly="yes"
do_mlir="yes"
do_flang="yes"
do_silent_log="no"
BuildDir="`pwd`"
ExtraConfigureFlags=""
ExportBranch=""
git_ref=""
do_cmake_cache="no"
do_bolt="no"
if [ "$System" = "Linux" ]; then
    case $Machine in
        x86_64 | arm64 | aarch64 )
            do_bolt="yes"
            ;;
    esac
fi

function usage() {
    echo "usage: `basename $0` -release X.Y.Z -rc NUM [OPTIONS]"
    echo ""
    echo " -release X.Y.Z       The release version to test."
    echo " -rc NUM              The pre-release candidate number."
    echo " -final               The final release candidate."
    echo " -triple TRIPLE       The target triple for this machine."
    echo " -j NUM               Number of compile jobs to run. [default: 3]"
    echo " -build-dir DIR       Directory to perform testing in. [default: pwd]"
    echo " -no-checkout         Don't checkout the sources from SVN."
    echo " -test-debug          Test the debug build. [default: no]"
    echo " -test-asserts        Test with asserts on. [default: no]"
    echo " -no-compare-files    Don't test that phase 2 and 3 files are identical."
    echo " -use-gzip            Use gzip instead of xz."
    echo " -use-ninja           Use ninja instead of make/gmake."
    echo " -configure-flags FLAGS  Extra flags to pass to the configure step."
    echo " -git-ref sha         Use the specified git ref for testing instead of a release."
    echo " -no-rt               Disable check-out & build Compiler-RT"
    echo " -no-clang-tools      Disable check-out & build clang-tools-extra"
    echo " -no-libs             Disable check-out & build libcxx/libcxxabi/libunwind"
    echo " -no-libcxxabi        Disable check-out & build libcxxabi"
    echo " -no-libunwind        Disable check-out & build libunwind"
    echo " -no-test-suite       Disable check-out & build test-suite"
    echo " -no-openmp           Disable check-out & build libomp"
    echo " -no-lld              Disable check-out & build lld"
    echo " -lldb                Enable check-out & build lldb"
    echo " -no-lldb             Disable check-out & build lldb (default)"
    echo " -no-polly            Disable check-out & build Polly"
    echo " -no-mlir             Disable check-out & build MLIR"
    echo " -no-flang            Disable check-out & build Flang"
    echo " -silent-log          Don't output build logs to stdout"
    echo " -use-cmake-cache     Build using a CMake cache file"
}

while [ $# -gt 0 ]; do
    case $1 in
        -release | --release )
            shift
            Release="$1"
            Release_no_dot="`echo $1 | sed -e 's,\.,,g'`"
            ;;
        -rc | --rc | -RC | --RC )
            shift
            RC="rc$1"
            ;;
        -final | --final )
            RC=final
            ;;
        -git-ref | --git-ref )
            shift
            Release="test"
            Release_no_dot="test"
            ExportBranch="$1"
            RC="`echo $ExportBranch | sed -e 's,/,_,g'`"
            git_ref="$1"
            echo "WARNING: Using the ref $git_ref instead of a release tag"
            echo "         This is intended to aid new packagers in trialing "
            echo "         builds without requiring a tag to be created first"
            ;;
        -triple | --triple )
            shift
            Triple="$1"
            ;;
        -configure-flags | --configure-flags )
            shift
            ExtraConfigureFlags="$1"
            ;;
        -j* )
            NumJobs="`echo $1 | sed -e 's,-j\([0-9]*\),\1,g'`"
            if [ -z "$NumJobs" ]; then
                shift
                NumJobs="$1"
            fi
            ;;
        -use-ninja )
            MAKE=ninja
            generator=Ninja
            ;;
        -build-dir | --build-dir | -builddir | --builddir )
            shift
            BuildDir="$1"
            ;;
        -no-checkout | --no-checkout )
            do_checkout="no"
            ;;
        -test-debug | --test-debug )
            do_debug="yes"
            ;;
        -test-asserts | --test-asserts )
            do_asserts="yes"
            ;;
        -no-compare-files | --no-compare-files )
            do_compare="no"
            ;;
        -use-gzip | --use-gzip )
            use_gzip="yes"
            ;;
        -no-rt )
            do_rt="no"
            ;;
        -no-libs )
            do_libs="no"
            ;;
        -no-clang-tools )
            do_clang_tools="no"
            ;;
        -no-libcxxabi )
            do_libcxxabi="no"
            ;;
        -no-libunwind )
            do_libunwind="no"
            ;;
        -no-test-suite )
            do_test_suite="no"
            ;;
        -no-openmp )
            do_openmp="no"
            ;;
        -bolt )
            do_bolt="yes"
            ;;
        -no-bolt )
            do_bolt="no"
            ;;
        -no-lld )
            do_lld="no"
            ;;
        -lldb )
            do_lldb="yes"
            ;;
        -no-lldb )
            do_lldb="no"
            ;;
        -no-polly )
            do_polly="no"
            ;;
        -no-mlir )
            do_mlir="no"
            ;;
        -no-flang )
            do_flang="no"
            ;;
        -silent-log )
            do_silent_log="yes"
            ;;
        -use-cmake-cache | --use-cmake-cache )
            do_cmake_cache="yes"
            ;;
        -help | --help | -h | --h | -\? )
            usage
            exit 0
            ;;
        * )
            echo "unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

if [ $do_mlir = "no" ] && [ $do_flang = "yes" ]; then
  echo "error: cannot build Flang without MLIR"
  exit 1
fi

# Check required arguments.
if [ -z "$Release" ]; then
    echo "error: no release number specified"
    exit 1
fi
if [ -z "$RC" ]; then
    echo "error: no release candidate number specified"
    exit 1
fi
if [ -z "$ExportBranch" ]; then
    ExportBranch="tags/RELEASE_$Release_no_dot/$RC"
fi
if [ -z "$Triple" ]; then
    echo "error: no target triple specified"
    exit 1
fi

if [ "$Release" != "test" ]; then
  if [ -n "$git_ref" ]; then
    echo "error: can't specify both -release and -git-ref"
    exit 1
  fi
  git_ref=llvmorg-$Release
  if [ "$RC" != "final" ]; then
    git_ref="$git_ref-$RC"
  fi
fi

UserNumJobs="$NumJobs"

# Figure out how many make processes to run.
if [ -z "$NumJobs" ]; then
    NumJobs=`sysctl -n hw.activecpu 2> /dev/null || true`
fi
if [ -z "$NumJobs" ]; then
    NumJobs=`sysctl -n hw.ncpu 2> /dev/null || true`
fi
if [ -z "$NumJobs" ]; then
    NumJobs=`grep -c processor /proc/cpuinfo 2> /dev/null || true`
fi
if [ -z "$NumJobs" ]; then
    NumJobs=3
fi

if [ "$MAKE" = "ninja" ] && [ -z "$UserNumJobs" ]; then
  # Rely on default ninja job numbers
  J_ARG=""
else
  J_ARG="-j $NumJobs"
fi

# Projects list
projects="llvm;clang"
if [ $do_clang_tools = "yes" ]; then
  projects="${projects:+$projects;}clang-tools-extra"
fi
runtimes=""
if [ $do_rt = "yes" ]; then
  runtimes="${runtimes:+$runtimes;}compiler-rt"
fi
if [ $do_libs = "yes" ]; then
  runtimes="${runtimes:+$runtimes;}libcxx"
  if [ $do_libcxxabi = "yes" ]; then
    runtimes="${runtimes:+$runtimes;}libcxxabi"
  fi
  if [ $do_libunwind = "yes" ]; then
    runtimes="${runtimes:+$runtimes;}libunwind"
  fi
fi
if [ $do_openmp = "yes" ]; then
  projects="${projects:+$projects;}openmp"
fi
if [ $do_bolt = "yes" ]; then
  projects="${projects:+$projects;}bolt"
fi
if [ $do_lld = "yes" ]; then
  projects="${projects:+$projects;}lld"
fi
if [ $do_lldb = "yes" ]; then
  projects="${projects:+$projects;}lldb"
fi
if [ $do_polly = "yes" ]; then
  projects="${projects:+$projects;}polly"
fi
if [ $do_mlir = "yes" ]; then
  projects="${projects:+$projects;}mlir"
fi
if [ $do_flang = "yes" ]; then
  projects="${projects:+$projects;}flang"
fi

# Go to the build directory (may be different from CWD)
BuildDir=$BuildDir/$RC
mkdir -p $BuildDir
cd $BuildDir

# Location of log files.
LogDir=$BuildDir/logs
mkdir -p $LogDir

# Final package name.
Package=clang+llvm-$Release
if [ $RC != "final" ]; then
  Package=$Package-$RC
fi
Package=$Package-$Triple

# Errors to be highlighted at the end are written to this file.
echo -n > $LogDir/deferred_errors.log

redir="/dev/stdout"
if [ $do_silent_log == "yes" ]; then
  echo "# Silencing build logs because of -silent-log flag..."
  redir="/dev/null"
fi


function build_with_cmake_cache() {
(
  CMakeBuildDir=$BuildDir/build
  SrcDir=$BuildDir/llvm-project/
  InstallDir=$BuildDir/install

  rm -rf $CMakeBuildDir

  # FIXME: Would be nice if the commands were echoed to the log file too.
  set -x

  env CC="$c_compiler" CXX="$cxx_compiler" \
  cmake -G "$generator" -B $CMakeBuildDir -S $SrcDir/llvm \
        -C $SrcDir/clang/cmake/caches/Release.cmake \
	-DCLANG_BOOTSTRAP_PASSTHROUGH="LLVM_LIT_ARGS" \
        -DLLVM_LIT_ARGS="-j $NumJobs $LitVerbose" \
        $ExtraConfigureFlags
        2>&1 | tee $LogDir/llvm.configure-$Flavor.log

  ${MAKE} $J_ARG $Verbose -C $CMakeBuildDir stage2-check-all \
          2>&1 | tee $LogDir/llvm.make-$Flavor.log > $redir

  DESTDIR="${InstallDir}" \
  ${MAKE} -C $CMakeBuildDir stage2-install \
          2>&1 | tee $LogDir/llvm.install-$Flavor.log > $redir

 mkdir -p $BuildDir/Release
 pushd $BuildDir/Release
 mv $InstallDir/usr/local $Package
 if [ "$use_gzip" = "yes" ]; then
    tar cf - $Package | gzip -9c > $BuildDir/$Package.tar.gz
  else
    tar cf - $Package | xz -9ce -T $NumJobs > $BuildDir/$Package.tar.xz
  fi
  mv $Package $InstallDir/usr/local
  popd
) 2>&1 | tee $LogDir/testing.$Release-$RC.log

  exit 0
}

function deferred_error() {
  Phase="$1"
  Flavor="$2"
  Msg="$3"
  echo "[${Flavor} Phase${Phase}] ${Msg}" | tee -a $LogDir/deferred_errors.log
}

# Make sure that a required program is available
function check_program_exists() {
  local program="$1"
  if ! type -P $program > /dev/null 2>&1 ; then
    echo "program '$1' not found !"
    exit 1
  fi
}

if [ "$System" != "Darwin" ] && [ "$System" != "SunOS" ] && [ "$System" != "AIX" ]; then
  check_program_exists 'chrpath'
fi

if [ "$System" != "Darwin" ]; then
  check_program_exists 'file'
  check_program_exists 'objdump'
fi

check_program_exists ${MAKE}

# Export sources to the build directory.
function export_sources() {
  SrcDir=$BuildDir/llvm-project
  mkdir -p $SrcDir
  echo "# Using git ref: $git_ref"

  # GitHub allows you to download a tarball of any commit using the URL:
  # https://github.com/$organization/$repo/archive/$ref.tar.gz
  curl -L https://github.com/llvm/llvm-project/archive/$git_ref.tar.gz | \
    tar -C $SrcDir --strip-components=1 -xzf -

  if [ "$do_test_suite" = "yes" ]; then
    TestSuiteSrcDir=$BuildDir/llvm-test-suite
    mkdir -p $TestSuiteSrcDir

    # We can only use named refs, like branches and tags, that exist in
    # both the llvm-project and test-suite repos if we want to run the
    # test suite.
    # If the test-suite fails to download assume we are using a ref that
    # doesn't exist in the test suite and disable it.
    set +e
    curl -L https://github.com/llvm/test-suite/archive/$git_ref.tar.gz | \
      tar -C $TestSuiteSrcDir --strip-components=1 -xzf -
    if [ $? -ne -0 ]; then
      echo "$git_ref not found in test-suite repo, test-suite disabled."
      do_test_suite="no"
    fi
    set -e
  fi

  cd $BuildDir
}

function configure_llvmCore() {
    Phase="$1"
    Flavor="$2"
    ObjDir="$3"

    case $Flavor in
        Release )
            BuildType="Release"
            Assertions="OFF"
            ;;
        Release+Asserts )
            BuildType="Release"
            Assertions="ON"
            ;;
        Debug )
            BuildType="Debug"
            Assertions="ON"
            ;;
        * )
            echo "# Invalid flavor '$Flavor'"
            echo ""
            return
            ;;
    esac

    # During the first two phases, there is no need to build any of the projects
    # except clang, since these phases are only meant to produce a bootstrapped
    # clang compiler, capable of building the third phase.
    if [ "$Phase" -lt "3" ]; then
      project_list="clang"
    else
      project_list="$projects"
    fi
    # During the first phase, there is no need to build any of the runtimes,
    # since this phase is only meant to get a clang compiler, capable of
    # building itself and any selected runtimes in the second phase.
    if [ "$Phase" -lt "2" ]; then
      runtime_list=""
      # compiler-rt builtins is needed on AIX to have a functional Phase 1 clang.
      if [ "$System" = "AIX" ]; then
        runtime_list="compiler-rt"
      fi  
    else
      runtime_list="$runtimes"
    fi

    echo "# Using C compiler: $c_compiler"
    echo "# Using C++ compiler: $cxx_compiler"

    cd $ObjDir
    echo "# Configuring llvm $Release-$RC $Flavor"

    echo "#" env CC="$c_compiler" CXX="$cxx_compiler" \
        cmake -G "$generator" \
        -DCMAKE_BUILD_TYPE=$BuildType -DLLVM_ENABLE_ASSERTIONS=$Assertions \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DLLVM_ENABLE_PROJECTS="$project_list" \
        -DLLVM_LIT_ARGS="-j $NumJobs $LitVerbose" \
        -DLLVM_ENABLE_RUNTIMES="$runtime_list" \
        $ExtraConfigureFlags $BuildDir/llvm-project/llvm \
        2>&1 | tee $LogDir/llvm.configure-Phase$Phase-$Flavor.log
    env CC="$c_compiler" CXX="$cxx_compiler" \
        cmake -G "$generator" \
        -DCMAKE_BUILD_TYPE=$BuildType -DLLVM_ENABLE_ASSERTIONS=$Assertions \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DLLVM_ENABLE_PROJECTS="$project_list" \
        -DLLVM_LIT_ARGS="-j $NumJobs $LitVerbose" \
        -DLLVM_ENABLE_RUNTIMES="$runtime_list" \
        $ExtraConfigureFlags $BuildDir/llvm-project/llvm \
        2>&1 | tee $LogDir/llvm.configure-Phase$Phase-$Flavor.log

    cd $BuildDir
}

function build_llvmCore() {
    Phase="$1"
    Flavor="$2"
    ObjDir="$3"
    DestDir="$4"

    Verbose="VERBOSE=1"
    if [ ${MAKE} = 'ninja' ]; then
      Verbose="-v"
    fi
    LitVerbose="-v"

    InstallTarget="install"
    if [ "$Phase" -lt "3" ]; then
      BuildTarget="clang"
      InstallTarget="install-clang install-clang-resource-headers"
      # compiler-rt builtins is needed on AIX to have a functional Phase 1 clang.
      if [ "$System" = "AIX" ]; then
        BuildTarget="$BuildTarget runtimes"
        InstallTarget="$InstallTarget install-builtins"
      fi
    fi
    if [ "$Phase" -eq "3" ]; then
      # Build everything at once, with the proper parallelism and verbosity,
      # in Phase 3.
      BuildTarget=
    fi

    cd $ObjDir
    echo "# Compiling llvm $Release-$RC $Flavor"
    echo "# ${MAKE} $J_ARG $Verbose"
    ${MAKE} $J_ARG $Verbose $BuildTarget \
        2>&1 | tee $LogDir/llvm.make-Phase$Phase-$Flavor.log > $redir

    echo "# Installing llvm $Release-$RC $Flavor"
    echo "# ${MAKE} install"
    DESTDIR="${DestDir}" ${MAKE} $InstallTarget \
        2>&1 | tee $LogDir/llvm.install-Phase$Phase-$Flavor.log > $redir
    cd $BuildDir
}

function test_llvmCore() {
    Phase="$1"
    Flavor="$2"
    ObjDir="$3"

    KeepGoing="-k"
    if [ ${MAKE} = 'ninja' ]; then
      # Ninja doesn't have a documented "keep-going-forever" mode, we need to
      # set a limit on how many jobs can fail before we give up.
      KeepGoing="-k 100"
    fi

    cd $ObjDir
    if ! ( ${MAKE} $J_ARG $KeepGoing $Verbose check-all \
        2>&1 | tee $LogDir/llvm.check-Phase$Phase-$Flavor.log ) ; then
      deferred_error $Phase $Flavor "check-all failed"
    fi

    if [ $do_test_suite = 'yes' ]; then
      cd $TestSuiteBuildDir
      env CC="$c_compiler" CXX="$cxx_compiler" \
          cmake $TestSuiteSrcDir -G "$generator" -DTEST_SUITE_LIT=$Lit \
                -DTEST_SUITE_HOST_CC=$build_compiler

      if ! ( ${MAKE} $J_ARG $KeepGoing $Verbose check \
          2>&1 | tee $LogDir/llvm.check-Phase$Phase-$Flavor.log ) ; then
        deferred_error $Phase $Flavor "test suite failed"
      fi
    fi
    cd $BuildDir
}

# Clean RPATH. Libtool adds the build directory to the search path, which is
# not necessary --- and even harmful --- for the binary packages we release.
function clean_RPATH() {
  if [ "$System" = "Darwin" ] || [ "$System" = "SunOS" ] || [ "$System" = "AIX" ]; then
    return
  fi
  local InstallPath="$1"
  for Candidate in `find $InstallPath/{bin,lib} -type f`; do
    if file $Candidate | grep ELF | egrep 'executable|shared object' > /dev/null 2>&1 ; then
      if rpath=`objdump -x $Candidate | grep 'RPATH'` ; then
        rpath=`echo $rpath | sed -e's/^ *RPATH *//'`
        if [ -n "$rpath" ]; then
          newrpath=`echo $rpath | sed -e's/.*\(\$ORIGIN[^:]*\).*/\1/'`
          chrpath -r $newrpath $Candidate 2>&1 > /dev/null 2>&1
        fi
      fi
    fi
  done
}

# Create a package of the release binaries.
function package_release() {
    cwd=`pwd`
    cd $BuildDir/Phase3/Release
    mv llvmCore-$Release-$RC.install/usr/local $Package
    if [ "$use_gzip" = "yes" ]; then
      tar cf - $Package | gzip -9c > $BuildDir/$Package.tar.gz
    else
      tar cf - $Package | xz -9ce -T $NumJobs > $BuildDir/$Package.tar.xz
    fi
    mv $Package llvmCore-$Release-$RC.install/usr/local
    cd $cwd
}

# Exit if any command fails
# Note: pipefail is necessary for running build commands through
# a pipe (i.e. it changes the output of ``false | tee /dev/null ; echo $?``)
set -e
set -o pipefail

# Turn off core dumps, as some test cases can easily fill up even the largest
# file systems.
ulimit -c 0

if [ "$do_checkout" = "yes" ]; then
    export_sources
fi

# Setup the test-suite.  Do this early so we can catch failures before
# we do the full 3 stage build.
if [ $do_test_suite = "yes" ]; then
  check_program_exists 'python3'
  venv="python3 -m venv"

  SandboxDir="$BuildDir/sandbox"
  Lit=$SandboxDir/bin/lit
  TestSuiteBuildDir="$BuildDir/test-suite-build"
  TestSuiteSrcDir="$BuildDir/llvm-test-suite"

  ${venv} $SandboxDir
  $SandboxDir/bin/python $BuildDir/llvm-project/llvm/utils/lit/setup.py install
  mkdir -p $TestSuiteBuildDir
fi

if [ "$do_cmake_cache" = "yes" ]; then
  build_with_cmake_cache
  exit 0
fi

(

Flavors="Release"
if [ "$do_debug" = "yes" ]; then
    Flavors="Debug $Flavors"
fi
if [ "$do_asserts" = "yes" ]; then
    Flavors="$Flavors Release+Asserts"
fi

for Flavor in $Flavors ; do
    echo ""
    echo ""
    echo "********************************************************************************"
    echo "  Release:     $Release-$RC"
    echo "  Build:       $Flavor"
    echo "  System Info: "
    echo "    `uname -a`"
    echo "********************************************************************************"
    echo ""

    c_compiler="$CC"
    cxx_compiler="$CXX"
    build_compiler="$CC"
    [[ -z "$build_compiler" ]] && build_compiler="cc"
    llvmCore_phase1_objdir=$BuildDir/Phase1/$Flavor/llvmCore-$Release-$RC.obj
    llvmCore_phase1_destdir=$BuildDir/Phase1/$Flavor/llvmCore-$Release-$RC.install

    llvmCore_phase2_objdir=$BuildDir/Phase2/$Flavor/llvmCore-$Release-$RC.obj
    llvmCore_phase2_destdir=$BuildDir/Phase2/$Flavor/llvmCore-$Release-$RC.install

    llvmCore_phase3_objdir=$BuildDir/Phase3/$Flavor/llvmCore-$Release-$RC.obj
    llvmCore_phase3_destdir=$BuildDir/Phase3/$Flavor/llvmCore-$Release-$RC.install

    rm -rf $llvmCore_phase1_objdir
    rm -rf $llvmCore_phase1_destdir

    rm -rf $llvmCore_phase2_objdir
    rm -rf $llvmCore_phase2_destdir

    rm -rf $llvmCore_phase3_objdir
    rm -rf $llvmCore_phase3_destdir

    mkdir -p $llvmCore_phase1_objdir
    mkdir -p $llvmCore_phase1_destdir

    mkdir -p $llvmCore_phase2_objdir
    mkdir -p $llvmCore_phase2_destdir

    mkdir -p $llvmCore_phase3_objdir
    mkdir -p $llvmCore_phase3_destdir

    ############################################################################
    # Phase 1: Build llvmCore and clang
    echo "# Phase 1: Building llvmCore"
    configure_llvmCore 1 $Flavor $llvmCore_phase1_objdir
    build_llvmCore 1 $Flavor \
        $llvmCore_phase1_objdir $llvmCore_phase1_destdir
    clean_RPATH $llvmCore_phase1_destdir/usr/local

    ########################################################################
    # Phase 2: Build llvmCore with newly built clang from phase 1.
    c_compiler=$llvmCore_phase1_destdir/usr/local/bin/clang
    cxx_compiler=$llvmCore_phase1_destdir/usr/local/bin/clang++
    echo "# Phase 2: Building llvmCore"
    configure_llvmCore 2 $Flavor $llvmCore_phase2_objdir
    build_llvmCore 2 $Flavor \
        $llvmCore_phase2_objdir $llvmCore_phase2_destdir
    clean_RPATH $llvmCore_phase2_destdir/usr/local

    ########################################################################
    # Phase 3: Build llvmCore with newly built clang from phase 2.
    c_compiler=$llvmCore_phase2_destdir/usr/local/bin/clang
    cxx_compiler=$llvmCore_phase2_destdir/usr/local/bin/clang++
    echo "# Phase 3: Building llvmCore"
    configure_llvmCore 3 $Flavor $llvmCore_phase3_objdir
    build_llvmCore 3 $Flavor \
        $llvmCore_phase3_objdir $llvmCore_phase3_destdir
    clean_RPATH $llvmCore_phase3_destdir/usr/local

    ########################################################################
    # Testing: Test phase 3
    c_compiler=$llvmCore_phase3_destdir/usr/local/bin/clang
    cxx_compiler=$llvmCore_phase3_destdir/usr/local/bin/clang++
    echo "# Testing - built with clang"
    test_llvmCore 3 $Flavor $llvmCore_phase3_objdir

    ########################################################################
    # Compare .o files between Phase2 and Phase3 and report which ones
    # differ.
    if [ "$do_compare" = "yes" ]; then
        echo
        echo "# Comparing Phase 2 and Phase 3 files"
        for p2 in `find $llvmCore_phase2_objdir -name '*.o'` ; do
            p3=`echo $p2 | sed -e 's,Phase2,Phase3,'`
            # Substitute 'Phase2' for 'Phase3' in the Phase 2 object file in
            # case there are build paths in the debug info. Do the same sub-
            # stitution on both files in case the string occurrs naturally.
            if ! cmp -s \
                <(env LC_CTYPE=C sed -e 's,Phase1,Phase2,g' -e 's,Phase2,Phase3,g' $p2) \
                <(env LC_CTYPE=C sed -e 's,Phase1,Phase2,g' -e 's,Phase2,Phase3,g' $p3) \
                16 16; then
                echo "file `basename $p2` differs between phase 2 and phase 3"
            fi
        done
    fi
done

) 2>&1 | tee $LogDir/testing.$Release-$RC.log

if [ "$use_gzip" = "yes" ]; then
  echo "# Packaging the release as $Package.tar.gz"
else
  echo "# Packaging the release as $Package.tar.xz"
fi
package_release

set +e

# Woo hoo!
echo "### Testing Finished ###"
echo "### Logs: $LogDir"

echo "### Errors:"
if [ -s "$LogDir/deferred_errors.log" ]; then
  cat "$LogDir/deferred_errors.log"
  exit 1
else
  echo "None."
fi

exit 0
