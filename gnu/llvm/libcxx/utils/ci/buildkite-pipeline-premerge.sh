#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

#
# This file generates a Buildkite pipeline that triggers the libc++ CI
# job(s) if needed.
# See https://buildkite.com/docs/agent/v3/cli-pipeline#pipeline-format.
#
# Invoked by CI on pre-merge check for a commit.
#

if ! git diff --name-only HEAD~1 | grep -q -E "^libcxx/|^libcxxabi/|^libunwind/|^runtimes/|^cmake/|^clang/"; then
  # libcxx/, libcxxabi/, libunwind/, runtimes/, cmake/ or clang/ are not affected
  exit 0
fi

reviewID="$(git log --format=%B -n 1 | sed -nE 's/^Review-ID:[[:space:]]*(.+)$/\1/p')"
if [[ "${reviewID}" != "" ]]; then
  buildMessage="https://llvm.org/${reviewID}"
else
  buildMessage="Push to branch ${BUILDKITE_BRANCH}"
fi


cat <<EOF
steps:
  - trigger: "libcxx-ci"
    build:
      message: "${buildMessage}"
      commit: "${BUILDKITE_COMMIT}"
      branch: "${BUILDKITE_BRANCH}"
EOF
