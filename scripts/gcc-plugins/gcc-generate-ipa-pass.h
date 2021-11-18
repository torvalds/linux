/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generator for IPA pass related boilerplate code/data
 *
 * Supports gcc 4.5-6
 *
 * Usage:
 *
 * 1. before inclusion define PASS_NAME
 * 2. before inclusion define NO_* for unimplemented callbacks
 *    NO_GENERATE_SUMMARY
 *    NO_READ_SUMMARY
 *    NO_WRITE_SUMMARY
 *    NO_READ_OPTIMIZATION_SUMMARY
 *    NO_WRITE_OPTIMIZATION_SUMMARY
 *    NO_STMT_FIXUP
 *    NO_FUNCTION_TRANSFORM
 *    NO_VARIABLE_TRANSFORM
 *    NO_GATE
 *    NO_EXECUTE
 * 3. before inclusion define PROPERTIES_* and *TODO_FLAGS_* to override
 *    the default 0 values
 * 4. for convenience, all the above will be undefined after inclusion!
 * 5. the only exported name is make_PASS_NAME_pass() to register with gcc
 */

#ifndef PASS_NAME
#error at least PASS_NAME must be defined
#else
#define __GCC_PLUGIN_STRINGIFY(n)	#n
#define _GCC_PLUGIN_STRINGIFY(n)	__GCC_PLUGIN_STRINGIFY(n)
#define _GCC_PLUGIN_CONCAT2(x, y)	x ## y
#define _GCC_PLUGIN_CONCAT3(x, y, z)	x ## y ## z

#define __PASS_NAME_PASS_DATA(n)	_GCC_PLUGIN_CONCAT2(n, _pass_data)
#define _PASS_NAME_PASS_DATA		__PASS_NAME_PASS_DATA(PASS_NAME)

#define __PASS_NAME_PASS(n)		_GCC_PLUGIN_CONCAT2(n, _pass)
#define _PASS_NAME_PASS			__PASS_NAME_PASS(PASS_NAME)

#define _PASS_NAME_NAME			_GCC_PLUGIN_STRINGIFY(PASS_NAME)

#define __MAKE_PASS_NAME_PASS(n)	_GCC_PLUGIN_CONCAT3(make_, n, _pass)
#define _MAKE_PASS_NAME_PASS		__MAKE_PASS_NAME_PASS(PASS_NAME)

#ifdef NO_GENERATE_SUMMARY
#define _GENERATE_SUMMARY NULL
#else
#define __GENERATE_SUMMARY(n)		_GCC_PLUGIN_CONCAT2(n, _generate_summary)
#define _GENERATE_SUMMARY		__GENERATE_SUMMARY(PASS_NAME)
#endif

#ifdef NO_READ_SUMMARY
#define _READ_SUMMARY NULL
#else
#define __READ_SUMMARY(n)		_GCC_PLUGIN_CONCAT2(n, _read_summary)
#define _READ_SUMMARY			__READ_SUMMARY(PASS_NAME)
#endif

#ifdef NO_WRITE_SUMMARY
#define _WRITE_SUMMARY NULL
#else
#define __WRITE_SUMMARY(n)		_GCC_PLUGIN_CONCAT2(n, _write_summary)
#define _WRITE_SUMMARY			__WRITE_SUMMARY(PASS_NAME)
#endif

#ifdef NO_READ_OPTIMIZATION_SUMMARY
#define _READ_OPTIMIZATION_SUMMARY NULL
#else
#define __READ_OPTIMIZATION_SUMMARY(n)	_GCC_PLUGIN_CONCAT2(n, _read_optimization_summary)
#define _READ_OPTIMIZATION_SUMMARY	__READ_OPTIMIZATION_SUMMARY(PASS_NAME)
#endif

#ifdef NO_WRITE_OPTIMIZATION_SUMMARY
#define _WRITE_OPTIMIZATION_SUMMARY NULL
#else
#define __WRITE_OPTIMIZATION_SUMMARY(n)	_GCC_PLUGIN_CONCAT2(n, _write_optimization_summary)
#define _WRITE_OPTIMIZATION_SUMMARY	__WRITE_OPTIMIZATION_SUMMARY(PASS_NAME)
#endif

#ifdef NO_STMT_FIXUP
#define _STMT_FIXUP NULL
#else
#define __STMT_FIXUP(n)			_GCC_PLUGIN_CONCAT2(n, _stmt_fixup)
#define _STMT_FIXUP			__STMT_FIXUP(PASS_NAME)
#endif

#ifdef NO_FUNCTION_TRANSFORM
#define _FUNCTION_TRANSFORM NULL
#else
#define __FUNCTION_TRANSFORM(n)		_GCC_PLUGIN_CONCAT2(n, _function_transform)
#define _FUNCTION_TRANSFORM		__FUNCTION_TRANSFORM(PASS_NAME)
#endif

#ifdef NO_VARIABLE_TRANSFORM
#define _VARIABLE_TRANSFORM NULL
#else
#define __VARIABLE_TRANSFORM(n)		_GCC_PLUGIN_CONCAT2(n, _variable_transform)
#define _VARIABLE_TRANSFORM		__VARIABLE_TRANSFORM(PASS_NAME)
#endif

#ifdef NO_GATE
#define _GATE NULL
#define _HAS_GATE false
#else
#define __GATE(n)			_GCC_PLUGIN_CONCAT2(n, _gate)
#define _GATE				__GATE(PASS_NAME)
#define _HAS_GATE true
#endif

#ifdef NO_EXECUTE
#define _EXECUTE NULL
#define _HAS_EXECUTE false
#else
#define __EXECUTE(n)			_GCC_PLUGIN_CONCAT2(n, _execute)
#define _EXECUTE			__EXECUTE(PASS_NAME)
#define _HAS_EXECUTE true
#endif

#ifndef PROPERTIES_REQUIRED
#define PROPERTIES_REQUIRED 0
#endif

#ifndef PROPERTIES_PROVIDED
#define PROPERTIES_PROVIDED 0
#endif

#ifndef PROPERTIES_DESTROYED
#define PROPERTIES_DESTROYED 0
#endif

#ifndef TODO_FLAGS_START
#define TODO_FLAGS_START 0
#endif

#ifndef TODO_FLAGS_FINISH
#define TODO_FLAGS_FINISH 0
#endif

#ifndef FUNCTION_TRANSFORM_TODO_FLAGS_START
#define FUNCTION_TRANSFORM_TODO_FLAGS_START 0
#endif

namespace {
static const pass_data _PASS_NAME_PASS_DATA = {
		.type			= IPA_PASS,
		.name			= _PASS_NAME_NAME,
		.optinfo_flags		= OPTGROUP_NONE,
		.tv_id			= TV_NONE,
		.properties_required	= PROPERTIES_REQUIRED,
		.properties_provided	= PROPERTIES_PROVIDED,
		.properties_destroyed	= PROPERTIES_DESTROYED,
		.todo_flags_start	= TODO_FLAGS_START,
		.todo_flags_finish	= TODO_FLAGS_FINISH,
};

class _PASS_NAME_PASS : public ipa_opt_pass_d {
public:
	_PASS_NAME_PASS() : ipa_opt_pass_d(_PASS_NAME_PASS_DATA,
			 g,
			 _GENERATE_SUMMARY,
			 _WRITE_SUMMARY,
			 _READ_SUMMARY,
			 _WRITE_OPTIMIZATION_SUMMARY,
			 _READ_OPTIMIZATION_SUMMARY,
			 _STMT_FIXUP,
			 FUNCTION_TRANSFORM_TODO_FLAGS_START,
			 _FUNCTION_TRANSFORM,
			 _VARIABLE_TRANSFORM) {}

#ifndef NO_GATE
	virtual bool gate(function *) { return _GATE(); }

	virtual opt_pass *clone() { return new _PASS_NAME_PASS(); }

#ifndef NO_EXECUTE
	virtual unsigned int execute(function *) { return _EXECUTE(); }
#endif
};
}

opt_pass *_MAKE_PASS_NAME_PASS(void)
{
	return new _PASS_NAME_PASS();
}
#else
struct opt_pass *_MAKE_PASS_NAME_PASS(void)
{
	return &_PASS_NAME_PASS.pass;
}
#endif

/* clean up user provided defines */
#undef PASS_NAME
#undef NO_GENERATE_SUMMARY
#undef NO_WRITE_SUMMARY
#undef NO_READ_SUMMARY
#undef NO_WRITE_OPTIMIZATION_SUMMARY
#undef NO_READ_OPTIMIZATION_SUMMARY
#undef NO_STMT_FIXUP
#undef NO_FUNCTION_TRANSFORM
#undef NO_VARIABLE_TRANSFORM
#undef NO_GATE
#undef NO_EXECUTE

#undef FUNCTION_TRANSFORM_TODO_FLAGS_START
#undef PROPERTIES_DESTROYED
#undef PROPERTIES_PROVIDED
#undef PROPERTIES_REQUIRED
#undef TODO_FLAGS_FINISH
#undef TODO_FLAGS_START

/* clean up generated defines */
#undef _EXECUTE
#undef __EXECUTE
#undef _FUNCTION_TRANSFORM
#undef __FUNCTION_TRANSFORM
#undef _GATE
#undef __GATE
#undef _GCC_PLUGIN_CONCAT2
#undef _GCC_PLUGIN_CONCAT3
#undef _GCC_PLUGIN_STRINGIFY
#undef __GCC_PLUGIN_STRINGIFY
#undef _GENERATE_SUMMARY
#undef __GENERATE_SUMMARY
#undef _HAS_EXECUTE
#undef _HAS_GATE
#undef _MAKE_PASS_NAME_PASS
#undef __MAKE_PASS_NAME_PASS
#undef _PASS_NAME_NAME
#undef _PASS_NAME_PASS
#undef __PASS_NAME_PASS
#undef _PASS_NAME_PASS_DATA
#undef __PASS_NAME_PASS_DATA
#undef _READ_OPTIMIZATION_SUMMARY
#undef __READ_OPTIMIZATION_SUMMARY
#undef _READ_SUMMARY
#undef __READ_SUMMARY
#undef _STMT_FIXUP
#undef __STMT_FIXUP
#undef _VARIABLE_TRANSFORM
#undef __VARIABLE_TRANSFORM
#undef _WRITE_OPTIMIZATION_SUMMARY
#undef __WRITE_OPTIMIZATION_SUMMARY
#undef _WRITE_SUMMARY
#undef __WRITE_SUMMARY

#endif /* PASS_NAME */
