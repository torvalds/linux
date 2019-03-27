/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 David Chisnall
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CHECKING_HH_
#define _CHECKING_HH_
#include <string>
#include "fdt.hh"

namespace dtc
{
namespace fdt
{
namespace checking
{
/**
 * Base class for all checkers.  This will visit the entire tree and perform
 * semantic checks defined in subclasses.  Note that device trees are generally
 * small (a few dozen nodes at most) and so we optimise for flexibility and
 * extensibility here, not for performance.  Each checker will visit the entire
 * tree.
 */
class checker
{
	/**
	 * The path to the current node being checked.  This is used for
	 * printing error messages.
	 */
	device_tree::node_path path;
	/**
	 * The name of the checker.  This is used for printing error messages
	 * and for enabling / disabling specific checkers from the command
	 * line.
	 */
	const char *checker_name;
	/**
	 * Visits each node, calling the checker functions on properties and
	 * nodes.
	 */
	bool visit_node(device_tree *tree, const node_ptr &n);
	protected:
	/**
	 * Prints the error message, along with the path to the node that
	 * caused the error and the name of the checker.
	 */
	void report_error(const char *errmsg);
	public:
	/**
	 * Constructor.  Takes the name of this checker, which is which is used
	 * when reporting errors.
	 */
	checker(const char *name) : checker_name(name) {}
	/**
	 * Virtual destructor in case any subclasses need to do cleanup.
	 */
	virtual ~checker() {}
	/**
	 * Method for checking that a node is valid.  The root class version
	 * does nothing, subclasses should override this.
	 */
	virtual bool check_node(device_tree *, const node_ptr &)
	{
		return true;
	}
	/**
	 * Method for checking that a property is valid.  The root class
	 * version does nothing, subclasses should override this.
	 */
	virtual bool check_property(device_tree *, const node_ptr &, property_ptr )
	{
		return true;
	}
	/**
	 * Runs the checker on the specified device tree.
	 */
	bool check_tree(fdt::device_tree *tree)
	{
		return visit_node(tree, tree->get_root());
	}
};

/**
 * Abstract base class for simple property checks.  This class defines a check
 * method for subclasses, which is invoked only when it finds a property with
 * the matching name.  To define simple property checkers, just subclass this
 * and override the check() method.
 */
class property_checker : public checker
{
	/**
	 * The name of the property that this checker is looking for.
	 */
	std::string key;
	public:
	/**
	 * Implementation of the generic property-checking method that checks
	 * for a property with the name specified in the constructor.
	 */
	virtual bool check_property(device_tree *tree, const node_ptr &n, property_ptr p);
	/**
	 * Constructor.  Takes the name of the checker and the name of the
	 * property to check.
	 */
	property_checker(const char* name, const std::string &property_name)
		: checker(name), key(property_name) {}
	/**
	 * The check method, which subclasses should implement.
	 */
	virtual bool check(device_tree *tree, const node_ptr &n, property_ptr p) = 0;
};

/**
 * Property type checker.
 */
template<property_value::value_type T>
struct property_type_checker : public property_checker
{
	/**
	 * Constructor, takes the name of the checker and the name of the
	 * property to check as arguments.
	 */
	property_type_checker(const char* name, const std::string &property_name) :
		property_checker(name, property_name) {}
	virtual bool check(device_tree *tree, const node_ptr &n, property_ptr p) = 0;
};

/**
 * Empty property checker.  This checks that the property has no value.
 */
template<>
struct property_type_checker <property_value::EMPTY> : public property_checker
{
	property_type_checker(const char* name, const std::string &property_name) :
		property_checker(name, property_name) {}
	virtual bool check(device_tree *, const node_ptr &, property_ptr p)
	{
		return p->begin() == p->end();
	}
};

/**
 * String property checker.  This checks that the property has exactly one
 * value, which is a string.
 */
template<>
struct property_type_checker <property_value::STRING> : public property_checker
{
	property_type_checker(const char* name, const std::string &property_name) :
		property_checker(name, property_name) {}
	virtual bool check(device_tree *, const node_ptr &, property_ptr p)
	{
		return (p->begin() + 1 == p->end()) && p->begin()->is_string();
	}
};
/**
 * String list property checker.  This checks that the property has at least
 * one value, all of which are strings.
 */
template<>
struct property_type_checker <property_value::STRING_LIST> :
	public property_checker
{
	property_type_checker(const char* name, const std::string &property_name) :
		property_checker(name, property_name) {}
	virtual bool check(device_tree *, const node_ptr &, property_ptr p)
	{
		for (property::value_iterator i=p->begin(),e=p->end() ; i!=e ;
		     ++i)
		{
			if (!(i->is_string() || i->is_string_list()))
			{
				return false;
			}
		}
		return p->begin() != p->end();
	}
};

/**
 * Phandle property checker.  This checks that the property has exactly one
 * value, which is a valid phandle.
 */
template<>
struct property_type_checker <property_value::PHANDLE> : public property_checker
{
	property_type_checker(const char* name, const std::string &property_name) :
		property_checker(name, property_name) {}
	virtual bool check(device_tree *tree, const node_ptr &, property_ptr p)
	{
		return (p->begin() + 1 == p->end()) &&
			(tree->referenced_node(*p->begin()) != 0);
	}
};

/**
 * Check that a property has the correct size.
 */
struct property_size_checker : public property_checker
{
	/**
	 * The expected size of the property.
	 */
	uint32_t size;
	public:
	/**
	 * Constructor, takes the name of the checker, the name of the property
	 * to check, and its expected size as arguments.
	 */
	property_size_checker(const char* name,
	                      const std::string &property_name,
	                      uint32_t bytes)
		: property_checker(name, property_name), size(bytes) {}
	/**
	 * Check, validates that the property has the correct size.
	 */
	virtual bool check(device_tree *tree, const node_ptr &n, property_ptr p);
};


/**
 * The check manager is the interface to running the checks.  This allows
 * default checks to be enabled, non-default checks to be enabled, and so on.
 */
class check_manager
{
	/**
	 * The enabled checkers, indexed by their names.  The name is used when
	 * disabling checkers from the command line.  When this manager runs,
	 * it will only run the checkers from this map.
	 */
	std::unordered_map<std::string, checker*> checkers;
	/**
	 * The disabled checkers.  Moving checkers to this list disables them,
	 * but allows them to be easily moved back.
	 */
	std::unordered_map<std::string, checker*> disabled_checkers;
	/**
	 * Helper function for adding a property value checker.
	 */
	template<property_value::value_type T>
	void add_property_type_checker(const char *name, const std::string &prop);
	/**
	 * Helper function for adding a simple type checker.
	 */
	void add_property_type_checker(const char *name, const std::string &prop);
	/**
	 * Helper function for adding a property value checker.
	 */
	void add_property_size_checker(const char *name,
	                               const std::string &prop,
	                               uint32_t size);
	public:
	/**
	 * Delete all of the checkers that are part of this checker manager.
	 */
	~check_manager();
	/**
	 * Default constructor, creates check manager containing all of the
	 * default checks.
	 */
	check_manager();
	/**
	 * Run all of the checks on the specified tree.
	 */
	bool run_checks(device_tree *tree, bool keep_going);
	/**
	 * Disables the named checker.
	 */
	bool disable_checker(const std::string &name);
	/**
	 * Enables the named checker.
	 */
	bool enable_checker(const std::string &name);
};

} // namespace checking

} // namespace fdt

} // namespace dtc

#endif // !_CHECKING_HH_
