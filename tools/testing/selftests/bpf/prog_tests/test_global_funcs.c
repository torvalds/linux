// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <test_progs.h>
#include "test_global_func1.skel.h"
#include "test_global_func2.skel.h"
#include "test_global_func3.skel.h"
#include "test_global_func4.skel.h"
#include "test_global_func5.skel.h"
#include "test_global_func6.skel.h"
#include "test_global_func7.skel.h"
#include "test_global_func8.skel.h"
#include "test_global_func9.skel.h"
#include "test_global_func10.skel.h"
#include "test_global_func11.skel.h"
#include "test_global_func12.skel.h"
#include "test_global_func13.skel.h"
#include "test_global_func14.skel.h"
#include "test_global_func15.skel.h"
#include "test_global_func16.skel.h"
#include "test_global_func17.skel.h"
#include "test_global_func_ctx_args.skel.h"

void test_test_global_funcs(void)
{
	RUN_TESTS(test_global_func1);
	RUN_TESTS(test_global_func2);
	RUN_TESTS(test_global_func3);
	RUN_TESTS(test_global_func4);
	RUN_TESTS(test_global_func5);
	RUN_TESTS(test_global_func6);
	RUN_TESTS(test_global_func7);
	RUN_TESTS(test_global_func8);
	RUN_TESTS(test_global_func9);
	RUN_TESTS(test_global_func10);
	RUN_TESTS(test_global_func11);
	RUN_TESTS(test_global_func12);
	RUN_TESTS(test_global_func13);
	RUN_TESTS(test_global_func14);
	RUN_TESTS(test_global_func15);
	RUN_TESTS(test_global_func16);
	RUN_TESTS(test_global_func17);
	RUN_TESTS(test_global_func_ctx_args);
}
