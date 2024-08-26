# SPDX-License-Identifier: GPL-2.0
# Makefile for damon selftests

TEST_GEN_FILES += huge_count_read_write
TEST_GEN_FILES += debugfs_target_ids_read_before_terminate_race
TEST_GEN_FILES += debugfs_target_ids_pid_leak
TEST_GEN_FILES += access_memory access_memory_even

TEST_FILES = _chk_dependency.sh _debugfs_common.sh

# functionality tests
TEST_PROGS = debugfs_attrs.sh debugfs_schemes.sh debugfs_target_ids.sh
TEST_PROGS += sysfs.sh
TEST_PROGS += sysfs_update_schemes_tried_regions_wss_estimation.py
TEST_PROGS += damos_quota.py damos_quota_goal.py damos_apply_interval.py
TEST_PROGS += damos_tried_regions.py damon_nr_regions.py
TEST_PROGS += reclaim.sh lru_sort.sh

# regression tests (reproducers of previously found bugs)
TEST_PROGS += debugfs_empty_targets.sh debugfs_huge_count_read_write.sh
TEST_PROGS += debugfs_duplicate_context_creation.sh
TEST_PROGS += debugfs_rm_non_contexts.sh
TEST_PROGS += debugfs_target_ids_read_before_terminate_race.sh
TEST_PROGS += debugfs_target_ids_pid_leak.sh
TEST_PROGS += sysfs_update_removed_scheme_dir.sh
TEST_PROGS += sysfs_update_schemes_tried_regions_hang.py

include ../lib.mk
