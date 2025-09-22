#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

import os
import sys

rsp_path = os.path.join(os.path.dirname(os.path.realpath(__file__)), "rsp")

with open(rsp_path) as f:
    success = "../Other/./foo" in f.read()

sys.exit(1 if success else 0)
