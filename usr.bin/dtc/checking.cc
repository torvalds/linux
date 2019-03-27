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

#include "checking.hh"
#include <stdio.h>

using std::string;

namespace dtc
{
namespace fdt
{
namespace checking
{

namespace
{
	struct deleted_node_checker : public checker
	{
		deleted_node_checker(const char *name) : checker(name) {}
		virtual bool check_node(device_tree *, const node_ptr &n)
		{
			auto &deleted = n->deleted_child_nodes();
			if (deleted.empty())
			{
				return true;
			}
			bool plural = deleted.size() > 1;
			string errmsg("Attempts to delete ");
			errmsg += plural ? "nodes" : "node";
			errmsg += " that ";
			errmsg += plural ? "were" : "was";
			errmsg += " not added in merge: ";
			for (auto &d : deleted)
			{
				errmsg += d;
			}
			report_error(errmsg.c_str());
			return false;
		}
	};
	/**
	 * Checker that verifies that every node that has children has
	 * #address-cells and #size-cells properties.
	 */
	struct address_cells_checker : public checker
	{
		address_cells_checker(const char *name) : checker(name) {}
		virtual bool check_node(device_tree *, const node_ptr &n)
		{
			// If this has no children, it trivially meets the
			// conditions.
			if (n->child_begin() == n->child_end())
			{
				return true;
			}
			bool found_address = false;
			bool found_size = false;
			for (auto i=n->property_begin(), e=n->property_end() ; i!=e ; ++i)
			{
				if (!found_address)
				{
					found_address = ((*i)->get_key() == "#address-cells");
				}
				if (!found_size)
				{
					found_size = ((*i)->get_key() == "#size-cells");
				}
				if (found_size && found_address)
				{
						break;
				}
			}
			if (!found_address)
			{
					report_error("Missing #address-cells property");
			}
			if (!found_size)
			{
					report_error("Missing #size-cells property");
			}
			return found_address && found_size;
		}
	};
} // anonymous namespace

bool
checker::visit_node(device_tree *tree, const node_ptr &n)
{
	path.push_back(std::make_pair(n->name, n->unit_address));
	// Check this node
	if (!check_node(tree, n))
	{
		return false;
	}
	// Now check its properties
	for (auto i=n->property_begin(), e=n->property_end() ; i!=e ; ++i)
	{
		if (!check_property(tree, n, *i))
		{
			return false;
		}
	}
	// And then recursively check the children
	for (node::child_iterator i=n->child_begin(), e=n->child_end() ; i!=e ;
	     ++i)
	{
		if (!visit_node(tree, *i))
		{
			return false;
		}
	}
	path.pop_back();
	return true;
}

void
checker::report_error(const char *errmsg)
{
	fprintf(stderr, "Error: %s, while checking node: ", errmsg);
	for (auto &p : path)
	{
		putc('/', stderr);
		puts(p.first.c_str());
		if (!(p.second.empty()))
		{
			putc('@', stderr);
			puts(p.second.c_str());
		}
	}
	fprintf(stderr, " [-W%s]\n", checker_name);
}

bool
property_checker::check_property(device_tree *tree, const node_ptr &n, property_ptr p)
{
	if (p->get_key() == key)
	{
		if (!check(tree, n, p))
		{
			report_error("property check failed");
			return false;
		}
	}
	return true;
}

bool
property_size_checker::check(device_tree *, const node_ptr &, property_ptr p)
{
	uint32_t psize = 0;
	for (property::value_iterator i=p->begin(),e=p->end() ; i!=e ; ++i)
	{
		if (!i->is_binary())
		{
			return false;
		}
		psize += i->byte_data.size();
	}
	return psize == size;
}

template<property_value::value_type T>
void
check_manager::add_property_type_checker(const char *name, const string &prop)
{
	checkers.insert(std::make_pair(string(name),
		new property_type_checker<T>(name, prop)));
}

void
check_manager::add_property_size_checker(const char *name,
                                         const string &prop,
                                         uint32_t size)
{
	checkers.insert(std::make_pair(string(name),
		new property_size_checker(name, prop, size)));
}

check_manager::~check_manager()
{
	while (checkers.begin() != checkers.end())
	{
		delete checkers.begin()->second;
		checkers.erase(checkers.begin());
	}
	while (disabled_checkers.begin() != disabled_checkers.end())
	{
		delete disabled_checkers.begin()->second;
		disabled_checkers.erase(disabled_checkers.begin());
	}
}

check_manager::check_manager()
{
	// NOTE: All checks listed here MUST have a corresponding line
	// in the man page!
	add_property_type_checker<property_value::STRING_LIST>(
			"type-compatible", string("compatible"));
	add_property_type_checker<property_value::STRING>(
			"type-model", string("model"));
	add_property_size_checker("type-phandle", string("phandle"), 4);
	disabled_checkers.insert(std::make_pair(string("cells-attributes"),
		new address_cells_checker("cells-attributes")));
	checkers.insert(std::make_pair(string("deleted-nodes"),
		new deleted_node_checker("deleted-nodes")));
}

bool
check_manager::run_checks(device_tree *tree, bool keep_going)
{
	bool success = true;
	for (auto &i : checkers)
	{
		success &= i.second->check_tree(tree);
		if (!(success || keep_going))
		{
			break;
		}
	}
	return success;
}

bool
check_manager::disable_checker(const string &name)
{
	auto checker = checkers.find(name);
	if (checker != checkers.end())
	{
		disabled_checkers.insert(std::make_pair(name,
		                                        checker->second));
		checkers.erase(checker);
		return true;
	}
	return false;
}

bool
check_manager::enable_checker(const string &name)
{
	auto checker = disabled_checkers.find(name);
	if (checker != disabled_checkers.end())
	{
		checkers.insert(std::make_pair(name, checker->second));
		disabled_checkers.erase(checker);
		return true;
	}
	return false;
}

} // namespace checking

} // namespace fdt

} // namespace dtc

