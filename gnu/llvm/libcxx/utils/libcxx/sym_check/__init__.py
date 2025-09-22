# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

"""libcxx abi symbol checker"""

__author__ = "Eric Fiselier"
__email__ = "eric@efcs.ca"
__versioninfo__ = (0, 1, 0)
__version__ = " ".join(str(v) for v in __versioninfo__) + "dev"

__all__ = ["diff", "extract", "util"]
