#!/usr/bin/env bash
#===- llvm/utils/docker/scripts/checkout.sh ---------------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===-----------------------------------------------------------------------===//

set -e

function show_usage() {
  cat << EOF
Usage: checkout.sh [options]

Checkout git sources into /tmp/clang-build/src. Used inside a docker container.

Available options:
  -h|--help           show this help message
  -b|--branch         git branch to checkout, i.e. 'main',
                      'release/10.x'
                      (default: 'main')
  -r|--revision       git revision to checkout
  -c|--cherrypick     revision to cherry-pick. Can be specified multiple times.
                      Cherry-picks are performed in the sorted order using the
                      following command:
                      'git cherry-pick \$rev)'.
EOF
}

LLVM_GIT_REV=""
CHERRYPICKS=""
LLVM_BRANCH=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--revision)
      shift
      LLVM_GIT_REV="$1"
      shift
      ;;
    -c|--cherrypick)
      shift
      CHERRYPICKS="$CHERRYPICKS $1"
      shift
      ;;
    -b|--branch)
      shift
      LLVM_BRANCH="$1"
      shift
      ;;
    -h|--help)
      show_usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
  esac
done

if [ "$LLVM_BRANCH" == "" ]; then
  LLVM_BRANCH="main"
fi

if [ "$LLVM_GIT_REV" != "" ]; then
  GIT_REV_ARG="$LLVM_GIT_REV"
  echo "Checking out git revision $LLVM_GIT_REV."
else
  GIT_REV_ARG=""
  echo "Checking out latest git revision."
fi

# Sort cherrypicks and remove duplicates.
CHERRYPICKS="$(echo "$CHERRYPICKS" | xargs -n1 | sort | uniq | xargs)"

function apply_cherrypicks() {
  local CHECKOUT_DIR="$1"

  [ "$CHERRYPICKS" == "" ] || echo "Applying cherrypicks"
  pushd "$CHECKOUT_DIR"

  # This function is always called on a sorted list of cherrypicks.
  for CHERRY_REV in $CHERRYPICKS; do
    echo "Cherry-picking $CHERRY_REV into $CHECKOUT_DIR"
    EMAIL="someone@somewhere.net" git cherry-pick $CHERRY_REV
  done

  popd
}

CLANG_BUILD_DIR=/tmp/clang-build

# Get the sources from git.
echo "Checking out sources from git"
mkdir -p "$CLANG_BUILD_DIR/src"
CHECKOUT_DIR="$CLANG_BUILD_DIR/src"

echo "Checking out https://github.com/llvm/llvm-project.git to $CHECKOUT_DIR"
git clone -b $LLVM_BRANCH --single-branch \
  "https://github.com/llvm/llvm-project.git" \
  "$CHECKOUT_DIR"

pushd $CHECKOUT_DIR
git checkout -q $GIT_REV_ARG
popd

  # We apply cherrypicks to all repositories regardless of whether the revision
  # changes this repository or not. For repositories not affected by the
  # cherrypick, applying the cherrypick is a no-op.
  apply_cherrypicks "$CHECKOUT_DIR"

CHECKSUMS_FILE="/tmp/checksums/checksums.txt"

if [ -f "$CHECKSUMS_FILE" ]; then
  echo "Validating checksums for LLVM checkout..."
  python "$(dirname $0)/llvm_checksum/llvm_checksum.py" -c "$CHECKSUMS_FILE" \
    --partial --multi_dir "$CLANG_BUILD_DIR/src"
else
  echo "Skipping checksumming checks..."
fi

echo "Done"
