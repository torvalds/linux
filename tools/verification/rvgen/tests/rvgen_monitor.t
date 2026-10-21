#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source ../tests/engine.sh
test_begin

set_timeout 30s

# Help and basic tests
check "verify help page" \
	"$RVGEN --help" 0 "Generate kernel rv monitor"

check "verify monitor subcommand help" \
	"$RVGEN monitor --help" 0 "Monitor class"

# DA monitor tests - test all monitor types
check_and_compare_folder "DA per_cpu (default name)" \
	"$RVGEN monitor -c da -s tests/specs/test_da.dot -t per_cpu" \
	"test_da" "obj-\$(CONFIG_RV_MON_TEST_DA) += monitors/test_da/test_da.o"

check_and_compare_folder "DA global type" \
	"$RVGEN monitor -c da -s tests/specs/test_da.dot -t global -n da_global" \
	"da_global" "DA_MON_EVENTS_IMPLICIT"

check_and_compare_folder "DA per_task with description" \
	"$RVGEN monitor -c da -s tests/specs/test_da2.dot -t per_task -n da_pertask_desc -D 'Custom description for testing'" \
	"da_pertask_desc" "#include <monitors/da_pertask_desc/da_pertask_desc_trace.h>"

check_and_compare_folder "DA per_obj with parent" \
	"$RVGEN monitor -c da -s tests/specs/test_da2.dot -t per_obj -n da_perobj_parent -p parent_mon" \
	"da_perobj_parent" "DA_MON_EVENTS_ID"

# HA monitor tests
check_and_compare_folder "HA per_task (default name)" \
	"$RVGEN monitor -c ha -s tests/specs/test_ha.dot -t per_task" \
	"test_ha" "HA_MON_EVENTS_ID"

check_and_compare_folder "HA per_cpu type" \
	"$RVGEN monitor -c ha -s tests/specs/test_ha.dot -t per_cpu -n ha_percpu" \
	"ha_percpu" "HA_MON_EVENTS_IMPLICIT"

# LTL monitor test
check_and_compare_folder "LTL per_task" \
	"$RVGEN monitor -c ltl -s tests/specs/test_ltl.ltl -t per_task -n ltl_pertask" \
	"ltl_pertask" "source \"kernel/trace/rv/monitors/ltl_pertask/Kconfig\""

check_and_compare_folder "LTL per_task with parent and description (default name)" \
	"$RVGEN monitor -c ltl -s tests/specs/test_ltl.ltl -t per_task -p ltl_parent -D 'Simple description'" \
	"test_ltl" "LTL_MON_EVENTS_ID"

# Error handling tests
check "missing required spec argument" \
	"$RVGEN monitor -c da -t per_cpu" 2 \
	"the following arguments are required: -s/--spec" "Traceback (most recent call last)"

check "missing required monitor type" \
	"$RVGEN monitor -c da -s tests/specs/test_da.dot" 2 \
	"the following arguments are required: -t/--monitor_type" "Traceback (most recent call last)"

check "missing required monitor class" \
	"$RVGEN monitor -s tests/specs/test_da.dot -t per_cpu" 2 \
	"the following arguments are required: -c/--class" "Traceback (most recent call last)"

check "invalid monitor class" \
	"$RVGEN monitor -c invalid -s tests/specs/test_da.dot -t per_cpu" 1 \
	"Unknown monitor class" "Traceback (most recent call last)"

check "missing dot file" \
	"$RVGEN monitor -c da -s tests/specs/nonexistent.dot -t per_cpu" 1 \
	"No such file or directory" "Traceback (most recent call last)"

check "missing ltl file" \
	"$RVGEN monitor -c ltl -s tests/specs/nonexistent.ltl -t per_task" 1 \
	"No such file or directory" "Traceback (most recent call last)"

check "invalid dot file syntax" \
	"$RVGEN monitor -c da -s tests/specs/test_invalid.dot -t per_cpu" 1 \
	"The automaton doesn't have an initial state" "Traceback (most recent call last)"

check "invalid ha file syntax" \
	"$RVGEN monitor -c ha -s tests/specs/test_invalid_ha.dot -t per_obj" 1 \
	"Unrecognised event" "Traceback (most recent call last)"

check "invalid ltl file syntax" \
	"$RVGEN monitor -c ltl -s tests/specs/test_invalid.ltl -t per_task" 1 \
	"No terminal matches 'i'" "Traceback (most recent call last)"

test_end
