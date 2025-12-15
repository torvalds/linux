#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2025, Oracle and/or its affiliates.
#
# Usage: awk -f gen-cpu-feature-names-x86.awk cpufeatures.h > cpu-feature-names.c
#

BEGIN {
	print "/* cpu feature name array generated from cpufeatures.h */"
	print "/* Do not change this code. */"
	print
	print "static const char *cpu_feature_names[(NCAPINTS+NBUGINTS)*32] = {"

	value_expr = "\\([0-9*+ ]+\\)"
}

/^#define X86_FEATURE_/ {
	if (match($0, value_expr)) {
		value = substr($0, RSTART + 1, RLENGTH - 2)
		print "\t[" value "] = \"" $2 "\","
	}
}

/^#define X86_BUG_/ {
	if (match($0, value_expr)) {
		value = substr($0, RSTART + 1, RLENGTH - 2)
		print "\t[NCAPINTS*32+(" value ")] = \"" $2 "\","
	}
}

END {
	print "};"
}
