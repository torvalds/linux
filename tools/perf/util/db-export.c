/*
 * db-export.c: Support for exporting data suitable for import to a database
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <errno.h>

#include "evsel.h"
#include "machine.h"
#include "thread.h"
#include "comm.h"
#include "symbol.h"
#include "event.h"
#include "util.h"
#include "thread-stack.h"
#include "db-export.h"

struct deferred_export {
	struct list_head node;
	struct comm *comm;
};

static int db_export__deferred(struct db_export *dbe)
{
	struct deferred_export *de;
	int err;

	while (!list_empty(&dbe->deferred)) {
		de = list_entry(dbe->deferred.next, struct deferred_export,
				node);
		err = dbe->export_comm(dbe, de->comm);
		list_del(&de->node);
		free(de);
		if (err)
			return err;
	}

	return 0;
}

static void db_export__free_deferred(struct db_export *dbe)
{
	struct deferred_export *de;

	while (!list_empty(&dbe->deferred)) {
		de = list_entry(dbe->deferred.next, struct deferred_export,
				node);
		list_del(&de->node);
		free(de);
	}
}

static int db_export__defer_comm(struct db_export *dbe, struct comm *comm)
{
	struct deferred_export *de;

	de = zalloc(sizeof(struct deferred_export));
	if (!de)
		return -ENOMEM;

	de->comm = comm;
	list_add_tail(&de->node, &dbe->deferred);

	return 0;
}

int db_export__init(struct db_export *dbe)
{
	memset(dbe, 0, sizeof(struct db_export));
	INIT_LIST_HEAD(&dbe->deferred);
	return 0;
}

int db_export__flush(struct db_export *dbe)
{
	return db_export__deferred(dbe);
}

void db_export__exit(struct db_export *dbe)
{
	db_export__free_deferred(dbe);
	call_return_processor__free(dbe->crp);
	dbe->crp = NULL;
}

int db_export__evsel(struct db_export *dbe, struct perf_evsel *evsel)
{
	if (evsel->db_id)
		return 0;

	evsel->db_id = ++dbe->evsel_last_db_id;

	if (dbe->export_evsel)
		return dbe->export_evsel(dbe, evsel);

	return 0;
}

int db_export__machine(struct db_export *dbe, struct machine *machine)
{
	if (machine->db_id)
		return 0;

	machine->db_id = ++dbe->machine_last_db_id;

	if (dbe->export_machine)
		return dbe->export_machine(dbe, machine);

	return 0;
}

int db_export__thread(struct db_export *dbe, struct thread *thread,
		      struct machine *machine, struct comm *comm)
{
	struct thread *main_thread;
	u64 main_thread_db_id = 0;
	int err;

	if (thread->db_id)
		return 0;

	thread->db_id = ++dbe->thread_last_db_id;

	if (thread->pid_ != -1) {
		if (thread->pid_ == thread->tid) {
			main_thread = thread;
		} else {
			main_thread = machine__findnew_thread(machine,
							      thread->pid_,
							      thread->pid_);
			if (!main_thread)
				return -ENOMEM;
			err = db_export__thread(dbe, main_thread, machine,
						comm);
			if (err)
				goto out_put;
			if (comm) {
				err = db_export__comm_thread(dbe, comm, thread);
				if (err)
					goto out_put;
			}
		}
		main_thread_db_id = main_thread->db_id;
		if (main_thread != thread)
			thread__put(main_thread);
	}

	if (dbe->export_thread)
		return dbe->export_thread(dbe, thread, main_thread_db_id,
					  machine);

	return 0;

out_put:
	thread__put(main_thread);
	return err;
}

int db_export__comm(struct db_export *dbe, struct comm *comm,
		    struct thread *main_thread)
{
	int err;

	if (comm->db_id)
		return 0;

	comm->db_id = ++dbe->comm_last_db_id;

	if (dbe->export_comm) {
		if (main_thread->comm_set)
			err = dbe->export_comm(dbe, comm);
		else
			err = db_export__defer_comm(dbe, comm);
		if (err)
			return err;
	}

	return db_export__comm_thread(dbe, comm, main_thread);
}

int db_export__comm_thread(struct db_export *dbe, struct comm *comm,
			   struct thread *thread)
{
	u64 db_id;

	db_id = ++dbe->comm_thread_last_db_id;

	if (dbe->export_comm_thread)
		return dbe->export_comm_thread(dbe, db_id, comm, thread);

	return 0;
}

int db_export__dso(struct db_export *dbe, struct dso *dso,
		   struct machine *machine)
{
	if (dso->db_id)
		return 0;

	dso->db_id = ++dbe->dso_last_db_id;

	if (dbe->export_dso)
		return dbe->export_dso(dbe, dso, machine);

	return 0;
}

int db_export__symbol(struct db_export *dbe, struct symbol *sym,
		      struct dso *dso)
{
	u64 *sym_db_id = symbol__priv(sym);

	if (*sym_db_id)
		return 0;

	*sym_db_id = ++dbe->symbol_last_db_id;

	if (dbe->export_symbol)
		return dbe->export_symbol(dbe, sym, dso);

	return 0;
}

static struct thread *get_main_thread(struct machine *machine, struct thread *thread)
{
	if (thread->pid_ == thread->tid)
		return thread__get(thread);

	if (thread->pid_ == -1)
		return NULL;

	return machine__find_thread(machine, thread->pid_, thread->pid_);
}

static int db_ids_from_al(struct db_export *dbe, struct addr_location *al,
			  u64 *dso_db_id, u64 *sym_db_id, u64 *offset)
{
	int err;

	if (al->map) {
		struct dso *dso = al->map->dso;

		err = db_export__dso(dbe, dso, al->machine);
		if (err)
			return err;
		*dso_db_id = dso->db_id;

		if (!al->sym) {
			al->sym = symbol__new(al->addr, 0, 0, "unknown");
			if (al->sym)
				symbols__insert(&dso->symbols[al->map->type],
						al->sym);
		}

		if (al->sym) {
			u64 *db_id = symbol__priv(al->sym);

			err = db_export__symbol(dbe, al->sym, dso);
			if (err)
				return err;
			*sym_db_id = *db_id;
			*offset = al->addr - al->sym->start;
		}
	}

	return 0;
}

int db_export__branch_type(struct db_export *dbe, u32 branch_type,
			   const char *name)
{
	if (dbe->export_branch_type)
		return dbe->export_branch_type(dbe, branch_type, name);

	return 0;
}

int db_export__sample(struct db_export *dbe, union perf_event *event,
		      struct perf_sample *sample, struct perf_evsel *evsel,
		      struct addr_location *al)
{
	struct thread* thread = al->thread;
	struct export_sample es = {
		.event = event,
		.sample = sample,
		.evsel = evsel,
		.al = al,
	};
	struct thread *main_thread;
	struct comm *comm = NULL;
	int err;

	err = db_export__evsel(dbe, evsel);
	if (err)
		return err;

	err = db_export__machine(dbe, al->machine);
	if (err)
		return err;

	main_thread = get_main_thread(al->machine, thread);
	if (main_thread)
		comm = machine__thread_exec_comm(al->machine, main_thread);

	err = db_export__thread(dbe, thread, al->machine, comm);
	if (err)
		goto out_put;

	if (comm) {
		err = db_export__comm(dbe, comm, main_thread);
		if (err)
			goto out_put;
		es.comm_db_id = comm->db_id;
	}

	es.db_id = ++dbe->sample_last_db_id;

	err = db_ids_from_al(dbe, al, &es.dso_db_id, &es.sym_db_id, &es.offset);
	if (err)
		goto out_put;

	if ((evsel->attr.sample_type & PERF_SAMPLE_ADDR) &&
	    sample_addr_correlates_sym(&evsel->attr)) {
		struct addr_location addr_al;

		perf_event__preprocess_sample_addr(event, sample, thread, &addr_al);
		err = db_ids_from_al(dbe, &addr_al, &es.addr_dso_db_id,
				     &es.addr_sym_db_id, &es.addr_offset);
		if (err)
			goto out_put;
		if (dbe->crp) {
			err = thread_stack__process(thread, comm, sample, al,
						    &addr_al, es.db_id,
						    dbe->crp);
			if (err)
				goto out_put;
		}
	}

	if (dbe->export_sample)
		err = dbe->export_sample(dbe, &es);

out_put:
	thread__put(main_thread);
	return err;
}

static struct {
	u32 branch_type;
	const char *name;
} branch_types[] = {
	{0, "no branch"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL, "call"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN, "return"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CONDITIONAL, "conditional jump"},
	{PERF_IP_FLAG_BRANCH, "unconditional jump"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_INTERRUPT,
	 "software interrupt"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN | PERF_IP_FLAG_INTERRUPT,
	 "return from interrupt"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_SYSCALLRET,
	 "system call"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN | PERF_IP_FLAG_SYSCALLRET,
	 "return from system call"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_ASYNC, "asynchronous branch"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_ASYNC |
	 PERF_IP_FLAG_INTERRUPT, "hardware interrupt"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TX_ABORT, "transaction abort"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TRACE_BEGIN, "trace begin"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TRACE_END, "trace end"},
	{0, NULL}
};

int db_export__branch_types(struct db_export *dbe)
{
	int i, err = 0;

	for (i = 0; branch_types[i].name ; i++) {
		err = db_export__branch_type(dbe, branch_types[i].branch_type,
					     branch_types[i].name);
		if (err)
			break;
	}
	return err;
}

int db_export__call_path(struct db_export *dbe, struct call_path *cp)
{
	int err;

	if (cp->db_id)
		return 0;

	if (cp->parent) {
		err = db_export__call_path(dbe, cp->parent);
		if (err)
			return err;
	}

	cp->db_id = ++dbe->call_path_last_db_id;

	if (dbe->export_call_path)
		return dbe->export_call_path(dbe, cp);

	return 0;
}

int db_export__call_return(struct db_export *dbe, struct call_return *cr)
{
	int err;

	if (cr->db_id)
		return 0;

	err = db_export__call_path(dbe, cr->cp);
	if (err)
		return err;

	cr->db_id = ++dbe->call_return_last_db_id;

	if (dbe->export_call_return)
		return dbe->export_call_return(dbe, cr);

	return 0;
}
