/*
 * Common eBPF ELF object loading operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */

#include <stdlib.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/bpf.h>

#include "libbpf.h"
