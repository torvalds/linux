# //===----------------------------------------------------------------------===//
# //
# // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# // See https://llvm.org/LICENSE.txt for details.
# // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# //
# //===----------------------------------------------------------------------===//

set(LLVM_LINK_COMPONENTS Support)

add_clang_tool(amdgpu-arch AMDGPUArch.cpp AMDGPUArchByHSA.cpp AMDGPUArchByHIP.cpp)

target_link_libraries(amdgpu-arch PRIVATE clangBasic)
