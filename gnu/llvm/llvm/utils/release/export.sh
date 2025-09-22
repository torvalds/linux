#!/bin/bash
#===-- tag.sh - Tag the LLVM release candidates ----------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#
#
# Create branches and release candidates for the LLVM release.
#
#===------------------------------------------------------------------------===#

set -e

projects="llvm bolt clang cmake compiler-rt libcxx libcxxabi libclc clang-tools-extra polly lldb lld openmp libunwind mlir flang runtimes third-party"

release=""
rc=""
yyyymmdd=$(date +'%Y%m%d')
snapshot=""
template='${PROJECT}-${RELEASE}${RC}.src.tar.xz'

usage() {
cat <<EOF
Export the Git sources and build tarballs from them.

Usage: $(basename $0) [-release|--release <major>.<minor>.<patch>]
                      [-rc|--rc <num>]
                      [-final|--final]
                      [-git-ref|--git-ref <git-ref>]
                      [-template|--template <template>]

Flags:

  -release  | --release <major>.<minor>.<patch>    The version number of the release
  -rc       | --rc <num>                           The release candidate number
  -final    | --final                              When provided, this option will disable the rc flag
  -git-ref  | --git-ref <git-ref>                  (optional) Use <git-ref> to determine the release and don't export the test-suite files
  -template | --template <template>                (optional) Possible placeholders: \$PROJECT \$YYYYMMDD \$GIT_REF \$RELEASE \$RC.
                                                   Defaults to '${template}'.

The following list shows the filenames (with <placeholders>) for the artifacts
that are being generated (given that you don't touch --template).

$(echo "$projects "| sed 's/\([a-z-]\+\) /  * \1-<RELEASE><RC>.src.tar.xz \n/g')

Additional files being generated:

  * llvm-project-<RELEASE><RC>.src.tar.xz    (the complete LLVM source project)
  * test-suite-<RELEASE><RC>.src.tar.xz      (only when not using --git-ref)

To ease the creation of snapshot builds, we also provide these files

  * llvm-release-<YYYYMMDD>.txt        (contains the <RELEASE> as a text)
  * llvm-rc-<YYYYMMDD>.txt             (contains the rc version passed to the invocation of $(basename $0))
  * llvm-git-revision-<YYYYMMDD>.txt   (contains the current git revision sha1)

Example values for the placeholders:

  * <RELEASE>  -> 13.0.0
  * <YYYYMMDD> -> 20210414   (the date when executing this script)
  * <RC>       -> rc4        (will be empty when using --git-ref)

In order to generate snapshots of the upstream main branch you could do this for example:

  $(basename $0) --git-ref upstream/main --template '\${PROJECT}-\${YYYYMMDD}.src.tar.xz'

EOF
}

template_file() {
    export PROJECT=$1 YYYYMMDD=$yyyymmdd RC=$rc RELEASE=$release GIT_REF=$git_rev
    basename $(echo $template | envsubst '$PROJECT $RELEASE $RC $YYYYMMDD $GIT_REF')
    unset PROJECT YYYYMMDD RC RELEASE GIT_REF
}

export_sources() {
    local tag="llvmorg-$release"

    llvm_src_dir=$(readlink -f $(dirname "$(readlink -f "$0")")/../../..)
    [ -d $llvm_src_dir/.git ] || ( echo "No git repository at $llvm_src_dir" ; exit 1 )

    # Determine the release by fetching the version from LLVM's CMakeLists.txt
    # in the specified git ref.
    if [ -n "$snapshot" ]; then
        release=$(git -C $llvm_src_dir show $snapshot:cmake/Modules/LLVMVersion.cmake | grep -ioP 'set\(\s*LLVM_VERSION_(MAJOR|MINOR|PATCH)\s\K[0-9]+' | paste -sd '.')
    fi
    
    tag="llvmorg-$release"

    if [ "$rc" = "final" ]; then
        rc=""
    else
        tag="$tag-$rc"
    fi

    target_dir=$(pwd)

    echo "Creating tarball for llvm-project ..."
    pushd $llvm_src_dir/
    tree_id=$tag
    [ -n "$snapshot" ] && tree_id="$snapshot"
    echo "Tree ID to archive: $tree_id"

    # This might be a surprise but a package like clang or compiler-rt don't
    # know about the LLVM version itself. That's why we also export a the
    # llvm-version*-<YYYYMMDD> and llvm-git*-<YYYYMMDD> files.
    git_rev=$(git rev-parse $tree_id)
    echo "git revision: $git_rev"
    echo "$release" > $target_dir/llvm-release-$yyyymmdd.txt
    echo "$rc" > $target_dir/llvm-rc-$yyyymmdd.txt
    echo "$git_rev" > $target_dir/llvm-git-revision-$yyyymmdd.txt
    
    git archive --prefix=llvm-project-$release$rc.src/ $tree_id . | xz -T0 >$target_dir/$(template_file llvm-project)
    popd

    if [ -z "$snapshot" ]; then
        if [ ! -d test-suite-$release$rc.src ]; then
            echo "Fetching LLVM test-suite source ..."
            mkdir -p test-suite-$release$rc.src
            curl -L https://github.com/llvm/test-suite/archive/$tag.tar.gz | \
                tar -C test-suite-$release$rc.src --strip-components=1 -xzf -
        fi
        echo "Creating tarball for test-suite ..."
        tar --sort=name --owner=0 --group=0 \
            --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime \
            -cJf test-suite-$release$rc.src.tar.xz test-suite-$release$rc.src
    fi

    for proj in $projects; do
        echo "Creating tarball for $proj ..."
        pushd $llvm_src_dir/$proj
        git archive --prefix=$proj-$release$rc.src/ $tree_id . | xz -T0 >$target_dir/$(template_file $proj)
        popd
    done
}

while [ $# -gt 0 ]; do
    case $1 in
        -release | --release )
            shift
            release=$1
            ;;
        -rc | --rc )
            shift
            rc="rc$1"
            ;;
        -final | --final )
            rc="final"
            ;;
        -git-ref | --git-ref )
            shift
            snapshot="$1"
            ;;
        -template | --template )
            shift
            template="$1"
            ;;
        -h | -help | --help )
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

if [ -n "$snapshot" ]; then 
    if [[ "$rc" != "" || "$release" != "" ]]; then
        echo "error: must not specify -rc or -release when creating a snapshot"
        exit 1
    fi
elif [ -z "$release" ]; then
    echo "error: need to specify a release version"
    exit 1
fi

# Make sure umask is not overly restrictive.
umask 0022

export_sources
exit 0
