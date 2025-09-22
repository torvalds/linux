#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

#
# This file generates a Buildkite pipeline that triggers the libc++ CI jobs.
# See https://buildkite.com/docs/agent/v3/cli-pipeline#pipeline-format.
#
# Invoked by CI on full builds.
#

cat <<EOF
steps:
  - trigger: "libcxx-ci"
    build:
      message: "${BUILDKITE_MESSAGE}"
      commit: "${BUILDKITE_COMMIT}"
      branch: "${BUILDKITE_BRANCH}"
EOF
