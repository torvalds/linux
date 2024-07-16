#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Checks fast/slow prime_number generation for inconsistencies
$(dirname $0)/../kselftest/module.sh "prime numbers" prime_numbers selftest=65536
