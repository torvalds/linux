/*
 * util/as112.c - list of local zones.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file provides a list of lan zones.
 */

#include "util/as112.h"

static const char* as112_zone_array[] = {
	"10.in-addr.arpa.",
	"16.172.in-addr.arpa.",
	"17.172.in-addr.arpa.",
	"18.172.in-addr.arpa.",
	"19.172.in-addr.arpa.",
	"20.172.in-addr.arpa.",
	"21.172.in-addr.arpa.",
	"22.172.in-addr.arpa.",
	"23.172.in-addr.arpa.",
	"24.172.in-addr.arpa.",
	"25.172.in-addr.arpa.",
	"26.172.in-addr.arpa.",
	"27.172.in-addr.arpa.",
	"28.172.in-addr.arpa.",
	"29.172.in-addr.arpa.",
	"30.172.in-addr.arpa.",
	"31.172.in-addr.arpa.",
	"168.192.in-addr.arpa.",
	"0.in-addr.arpa.",
	"64.100.in-addr.arpa.",
	"65.100.in-addr.arpa.",
	"66.100.in-addr.arpa.",
	"67.100.in-addr.arpa.",
	"68.100.in-addr.arpa.",
	"69.100.in-addr.arpa.",
	"70.100.in-addr.arpa.",
	"71.100.in-addr.arpa.",
	"72.100.in-addr.arpa.",
	"73.100.in-addr.arpa.",
	"74.100.in-addr.arpa.",
	"75.100.in-addr.arpa.",
	"76.100.in-addr.arpa.",
	"77.100.in-addr.arpa.",
	"78.100.in-addr.arpa.",
	"79.100.in-addr.arpa.",
	"80.100.in-addr.arpa.",
	"81.100.in-addr.arpa.",
	"82.100.in-addr.arpa.",
	"83.100.in-addr.arpa.",
	"84.100.in-addr.arpa.",
	"85.100.in-addr.arpa.",
	"86.100.in-addr.arpa.",
	"87.100.in-addr.arpa.",
	"88.100.in-addr.arpa.",
	"89.100.in-addr.arpa.",
	"90.100.in-addr.arpa.",
	"91.100.in-addr.arpa.",
	"92.100.in-addr.arpa.",
	"93.100.in-addr.arpa.",
	"94.100.in-addr.arpa.",
	"95.100.in-addr.arpa.",
	"96.100.in-addr.arpa.",
	"97.100.in-addr.arpa.",
	"98.100.in-addr.arpa.",
	"99.100.in-addr.arpa.",
	"100.100.in-addr.arpa.",
	"101.100.in-addr.arpa.",
	"102.100.in-addr.arpa.",
	"103.100.in-addr.arpa.",
	"104.100.in-addr.arpa.",
	"105.100.in-addr.arpa.",
	"106.100.in-addr.arpa.",
	"107.100.in-addr.arpa.",
	"108.100.in-addr.arpa.",
	"109.100.in-addr.arpa.",
	"110.100.in-addr.arpa.",
	"111.100.in-addr.arpa.",
	"112.100.in-addr.arpa.",
	"113.100.in-addr.arpa.",
	"114.100.in-addr.arpa.",
	"115.100.in-addr.arpa.",
	"116.100.in-addr.arpa.",
	"117.100.in-addr.arpa.",
	"118.100.in-addr.arpa.",
	"119.100.in-addr.arpa.",
	"120.100.in-addr.arpa.",
	"121.100.in-addr.arpa.",
	"122.100.in-addr.arpa.",
	"123.100.in-addr.arpa.",
	"124.100.in-addr.arpa.",
	"125.100.in-addr.arpa.",
	"126.100.in-addr.arpa.",
	"127.100.in-addr.arpa.",
	"254.169.in-addr.arpa.",
	"2.0.192.in-addr.arpa.",
	"100.51.198.in-addr.arpa.",
	"113.0.203.in-addr.arpa.",
	"255.255.255.255.in-addr.arpa.",
	"0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa.",
	"d.f.ip6.arpa.",
	"8.e.f.ip6.arpa.",
	"9.e.f.ip6.arpa.",
	"a.e.f.ip6.arpa.",
	"b.e.f.ip6.arpa.",
	"8.b.d.0.1.0.0.2.ip6.arpa.",
	0
};

const char** as112_zones = as112_zone_array;
