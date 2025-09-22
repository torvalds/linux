# Basic sanity check for `--help` and `--version` options.
#
# RUN: %{lit} --help         | FileCheck %s --check-prefix=HELP
# RUN: %{lit} --version 2>&1 | FileCheck %s --check-prefix=VERSION
#
# HELP: usage: lit [-h]
# VERSION: lit {{[0-9]+\.[0-9]+\.[0-9]+[a-zA-Z0-9]*}}
