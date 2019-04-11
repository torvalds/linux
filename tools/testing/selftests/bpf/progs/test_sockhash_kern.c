// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
#undef SOCKMAP
#define TEST_MAP_TYPE BPF_MAP_TYPE_SOCKHASH
#include "./test_sockmap_kern.h"
