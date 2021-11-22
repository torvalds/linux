/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generator for GIMPLE pass related boilerplate code/data
 *
 * Supports gcc 4.5-6
 *
 * Usage:
 *
 * 1. before inclusion define PASS_NAME
 * 2. before inclusion define NO_* for unimplemented callbacks
 *    NO_GATE
 *    NO_EXECUTE
 * 3. before inclusion define PROPERTIES_* and TODO_FLAGS_* to override
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

namespace {
static const pass_data _PASS_NAME_PASS_DATA = {
		.type			= GIMPLE_PASS,
		.name			= _PASS_NAME_NAME,
		.optinfo_flags		= OPTGROUP_NONE,
		.tv_id			= TV_NONE,
		.properties_required	= PROPERTIES_REQUIRED,
		.properties_provided	= PROPERTIES_PROVIDED,
		.properties_destroyed	= PROPERTIES_DESTROYED,
		.todo_flags_start	= TODO_FLAGS_START,
		.todo_flags_finish	= TODO_FLAGS_FINISH,
};

class _PASS_NAME_PASS : public gimple_opt_pass {
public:
	_PASS_NAME_PASS() : gimple_opt_pass(_PASS_NAME_PASS_DATA, g) {}

#ifndef NO_GATE
	virtual bool gate(function *) { return _GATE(); }
#endif

	virtual opt_pass * clone () { return new _PASS_NAME_PASS(); }

#ifndef NO_EXECUTE
	virtual unsigned int execute(function *) { return _EXECUTE(); }
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
#undef NO_GATE
#undef NO_EXECUTE

#undef PROPERTIES_DESTROYED
#undef PROPERTIES_PROVIDED
#undef PROPERTIES_REQUIRED
#undef TODO_FLAGS_FINISH
#undef TODO_FLAGS_START

/* clean up generated defines */
#undef _EXECUTE
#undef __EXECUTE
#undef _GATE
#undef __GATE
#undef _GCC_PLUGIN_CONCAT2
#undef _GCC_PLUGIN_CONCAT3
#undef _GCC_PLUGIN_STRINGIFY
#undef __GCC_PLUGIN_STRINGIFY
#undef _HAS_EXECUTE
#undef _HAS_GATE
#undef _MAKE_PASS_NAME_PASS
#undef __MAKE_PASS_NAME_PASS
#undef _PASS_NAME_NAME
#undef _PASS_NAME_PASS
#undef __PASS_NAME_PASS
#undef _PASS_NAME_PASS_DATA
#undef __PASS_NAME_PASS_DATA

#endif /* PASS_NAME */
