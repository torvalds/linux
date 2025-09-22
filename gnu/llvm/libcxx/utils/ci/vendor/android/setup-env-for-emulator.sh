#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

export ADB_SERVER_SOCKET="tcp:$(docker inspect \
    -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' \
    libcxx-ci-android-emulator):5037"

echo "setup-env-for-emulator.sh: setting ADB_SERVER_SOCKET to ${ADB_SERVER_SOCKET}"
