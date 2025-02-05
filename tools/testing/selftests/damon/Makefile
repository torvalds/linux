# SPDX-License-Identifier: GPL-2.0
# Makefile for damon selftests

TEST_GEN_FILES += access_memory access_memory_even

TEST_FILES = _chk_dependency.sh _damon_sysfs.py

# functionality tests
TEST_PROGS += sysfs.sh
TEST_PROGS += sysfs_update_schemes_tried_regions_wss_estimation.py
TEST_PROGS += damos_quota.py damos_quota_goal.py damos_apply_interval.py
TEST_PROGS += damos_tried_regions.py damon_nr_regions.py
TEST_PROGS += reclaim.sh lru_sort.sh

# regression tests (reproducers of previously found bugs)
TEST_PROGS += sysfs_update_removed_scheme_dir.sh
TEST_PROGS += sysfs_update_schemes_tried_regions_hang.py

EXTRA_CLEAN = __pycache__

include ../lib.mk
