/*
 * db-export.h: Support for exporting data suitable for import to a database
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

#ifndef __PERF_DB_EXPORT_H
#define __PERF_DB_EXPORT_H

#include <linux/types.h>
#include <linux/list.h>

struct perf_evsel;
struct machine;
struct thread;
struct comm;
struct dso;
struct perf_sample;
struct addr_location;
struct call_return_processor;
struct call_path_root;
struct call_path;
struct call_return;

struct export_sample {
	union perf_event	*event;
	struct perf_sample	*sample;
	struct perf_evsel	*evsel;
	struct addr_location	*al;
	u64			db_id;
	u64			comm_db_id;
	u64			dso_db_id;
	u64			sym_db_id;
	u64			offset; /* ip offset from symbol start */
	u64			addr_dso_db_id;
	u64			addr_sym_db_id;
	u64			addr_offset; /* addr offset from symbol start */
	u64			call_path_id;
};

struct db_export {
	int (*export_evsel)(struct db_export *dbe, struct perf_evsel *evsel);
	int (*export_machine)(struct db_export *dbe, struct machine *machine);
	int (*export_thread)(struct db_export *dbe, struct thread *thread,
			     u64 main_thread_db_id, struct machine *machine);
	int (*export_comm)(struct db_export *dbe, struct comm *comm);
	int (*export_comm_thread)(struct db_export *dbe, u64 db_id,
				  struct comm *comm, struct thread *thread);
	int (*export_dso)(struct db_export *dbe, struct dso *dso,
			  struct machine *machine);
	int (*export_symbol)(struct db_export *dbe, struct symbol *sym,
			     struct dso *dso);
	int (*export_branch_type)(struct db_export *dbe, u32 branch_type,
				  const char *name);
	int (*export_sample)(struct db_export *dbe, struct export_sample *es);
	int (*export_call_path)(struct db_export *dbe, struct call_path *cp);
	int (*export_call_return)(struct db_export *dbe,
				  struct call_return *cr);
	struct call_return_processor *crp;
	struct call_path_root *cpr;
	u64 evsel_last_db_id;
	u64 machine_last_db_id;
	u64 thread_last_db_id;
	u64 comm_last_db_id;
	u64 comm_thread_last_db_id;
	u64 dso_last_db_id;
	u64 symbol_last_db_id;
	u64 sample_last_db_id;
	u64 call_path_last_db_id;
	u64 call_return_last_db_id;
	struct list_head deferred;
};

int db_export__init(struct db_export *dbe);
int db_export__flush(struct db_export *dbe);
void db_export__exit(struct db_export *dbe);
int db_export__evsel(struct db_export *dbe, struct perf_evsel *evsel);
int db_export__machine(struct db_export *dbe, struct machine *machine);
int db_export__thread(struct db_export *dbe, struct thread *thread,
		      struct machine *machine, struct comm *comm);
int db_export__comm(struct db_export *dbe, struct comm *comm,
		    struct thread *main_thread);
int db_export__comm_thread(struct db_export *dbe, struct comm *comm,
			   struct thread *thread);
int db_export__dso(struct db_export *dbe, struct dso *dso,
		   struct machine *machine);
int db_export__symbol(struct db_export *dbe, struct symbol *sym,
		      struct dso *dso);
int db_export__branch_type(struct db_export *dbe, u32 branch_type,
			   const char *name);
int db_export__sample(struct db_export *dbe, union perf_event *event,
		      struct perf_sample *sample, struct perf_evsel *evsel,
		      struct addr_location *al);

int db_export__branch_types(struct db_export *dbe);

int db_export__call_path(struct db_export *dbe, struct call_path *cp);
int db_export__call_return(struct db_export *dbe, struct call_return *cr,
			   u64 *parent_db_id);

#endif
