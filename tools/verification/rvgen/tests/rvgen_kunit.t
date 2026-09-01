#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source ../tests/engine.sh
test_begin

set_timeout 30s

# Help tests
check "verify kunit subcommand help" \
	"$RVGEN kunit -h" 0 "model_name" "spec"

check_and_compare_folder "KUnit generation with local lookup and test_da_kunit" \
	"$RVGEN monitor -c da -s tests/specs/test_da.dot -t per_cpu -n test_da_kunit && $RVGEN kunit -a -l -n test_da_kunit" \
	"test_da_kunit" "Now complete the test and add it to rv_monitors_test.c" "RV_MON_OPS_INIT"

check_and_compare_folder "KUnit generation with local lookup and test_ha_kunit" \
	"$RVGEN monitor -c ha -s tests/specs/test_ha.dot -t per_task -n test_ha_kunit && $RVGEN kunit -a -l -n test_ha_kunit" \
	"test_ha_kunit" "Successfully created KUnit" "Append the following to"

check_and_compare_folder "KUnit generation with local lookup and test_ltl_kunit" \
	"$RVGEN monitor -c ltl -s tests/specs/test_ltl.ltl -t per_task -n test_ltl_kunit && $RVGEN kunit -l -n test_ltl_kunit" \
	"test_ltl_kunit" "RV_MON_OPS_INIT"

check_and_compare_folder "KUnit generation with backup file" \
	"$RVGEN monitor -c ltl -s tests/specs/test_ltl.ltl -t per_task -n test_bak_kunit && echo DUMMY > test_bak_kunit/test_bak_kunit_kunit.c && $RVGEN kunit -l -n test_bak_kunit" \
	"test_bak_kunit" "KUnit file(s) already exist.*backing up existing files"

# Error handling tests
check "missing required model_name" \
	"$RVGEN kunit" 2 "the following arguments are required: -n/--model_name"

check "non-existent model_name with auto_patch" \
	"$RVGEN kunit -a -n nonexistent" 1 \
	"Could not find monitor C file" "Traceback (most recent call last)"

check "monitor without handlers" \
	"mkdir -p nohandler ; echo DUMMY > nohandler/nohandler.c ; $RVGEN kunit -l -n nohandler" 1 \
	"No handlers found" "Traceback (most recent call last)"
rm -rf nohandler

test_end
