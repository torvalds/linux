/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2003 M. Warner Losh.
 * All rights reserved.
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

#ifndef DEVD_HH
#define DEVD_HH

class config;

/**
 * var_list is a collection of variables.  These collections of variables
 * are stacked up and popped down for each event that we have to process.
 * We have multiple levels so that we can push variables that are unique
 * to the event in question, in addition to having global variables.  This
 * allows for future flexibility.
 */
class var_list
{
public:
	/** Set a variable in this var list.
	 */
	void set_variable(const std::string &var, const std::string &val);
	/** Get the variable out of this, and no other, var_list.  If
	 * no variable of %var is set, then %bogus will be returned.
	 */
	const std::string &get_variable(const std::string &var) const;
	/** Is there a variable of %var set in this table?
	 */
	bool is_set(const std::string &var) const;
	/** A completely bogus string.
	 */
	static const std::string bogus;
	static const std::string nothing;

private:
	std::string fix_value(const std::string &val) const;

	std::map<std::string, std::string> _vars;
};

/**
 * eps is short for event_proc_single.  It is a single entry in an
 * event_proc.  Each keyword needs its own subclass from eps.
 */
struct eps
{
public:
	virtual ~eps() {}
	/** Does this eps match the current config?
	 */
	virtual bool do_match(config &) = 0;
	/** Perform some action for this eps.
	 */
	virtual bool do_action(config &) = 0;
};

/**
 * match is the subclass used to match an individual variable.  Its
 * actions are nops.
 */
class match : public eps
{
public:
	match(config &, const char *var, const char *re);
	virtual ~match();
	virtual bool do_match(config &);
	virtual bool do_action(config &) { return true; }
private:
	bool _inv;
	std::string _var;
	std::string _re;
	regex_t _regex;
};

/**
 * media is the subclass used to match an individual variable.  Its
 * actions are nops.
 */
class media : public eps
{
public:
	media(config &, const char *var, const char *type);
	virtual ~media();
	virtual bool do_match(config &);
	virtual bool do_action(config &) { return true; }
private:
	std::string _var;
	int _type;
};

/**
 * action is used to fork a process.  It matches everything.
 */
class action : public eps
{
public:
	action(const char *cmd);
	virtual ~action();
	virtual bool do_match(config &) { return true; }
	virtual bool do_action(config &);
private:
	std::string _cmd;
};

struct event_proc
{
public:
	event_proc();
	virtual ~event_proc();
	int get_priority() const { return (_prio); }
	void set_priority(int prio) { _prio = prio; }
	void add(eps *);
	bool matches(config &) const;
	bool run(config &) const;
private:
	int _prio;
	std::vector<eps *> _epsvec;
};

class config
{
public:
	config() { push_var_table(); }
	virtual ~config() { reset(); }
	void add_attach(int, event_proc *);
	void add_detach(int, event_proc *);
	void add_directory(const char *);
	void add_nomatch(int, event_proc *);
	void add_notify(int, event_proc *);
	void set_pidfile(const char *);
	void reset();
	void parse();
	void close_pidfile();
	void open_pidfile();
	void write_pidfile();
	void remove_pidfile();
	void push_var_table();
	void pop_var_table();
	void set_variable(const char *var, const char *val);
	const std::string &get_variable(const std::string &var);
	const std::string expand_string(const char * var, 
	    const char * prepend = NULL, const char * append = NULL);
	char *set_vars(char *);
	void find_and_execute(char);
protected:
	void sort_vector(std::vector<event_proc *> &);
	void parse_one_file(const char *fn);
	void parse_files_in_dir(const char *dirname);
	void expand_one(const char *&src, std::string &dst, bool is_shell);
	std::string shell_quote(const std::string &s);
	bool is_id_char(char) const;
	bool chop_var(char *&buffer, char *&lhs, char *&rhs) const;
private:
	std::vector<std::string> _dir_list;
	std::string _pidfile;
	std::vector<var_list *> _var_list_table;
	std::vector<event_proc *> _attach_list;
	std::vector<event_proc *> _detach_list;
	std::vector<event_proc *> _nomatch_list;
	std::vector<event_proc *> _notify_list;
};

#endif /* DEVD_HH */
