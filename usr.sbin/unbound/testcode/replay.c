/*
 * testcode/replay.c - store and use a replay of events for the DNS resolver.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * Store and use a replay of events for the DNS resolver.
 * Used to test known scenarios to get known outcomes.
 */

#include "config.h"
/* for strtod prototype */
#include <math.h>
#include <ctype.h>
#include <time.h>
#include "util/log.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "testcode/replay.h"
#include "testcode/testpkts.h"
#include "testcode/fake_event.h"
#include "sldns/str2wire.h"
#include "util/timeval_func.h"

/** max length of lines in file */
#define MAX_LINE_LEN 10240

/**
 * Expand a macro
 * @param store: value storage
 * @param runtime: replay runtime for other stuff.
 * @param text: the macro text, after the ${, Updated to after the } when
 * 	done (successfully).
 * @return expanded text, malloced. NULL on failure.
 */
static char* macro_expand(rbtree_type* store,
	struct replay_runtime* runtime, char** text);

/** parse keyword in string.
 * @param line: if found, the line is advanced to after the keyword.
 * @param keyword: string.
 * @return: true if found, false if not.
 */
static int
parse_keyword(char** line, const char* keyword)
{
	size_t len = (size_t)strlen(keyword);
	if(strncmp(*line, keyword, len) == 0) {
		*line += len;
		return 1;
	}
	return 0;
}

/** delete moment */
static void
replay_moment_delete(struct replay_moment* mom)
{
	if(!mom)
		return;
	if(mom->match) {
		delete_entry(mom->match);
	}
	free(mom->autotrust_id);
	free(mom->string);
	free(mom->variable);
	config_delstrlist(mom->file_content);
	free(mom);
}

/** delete range */
static void
replay_range_delete(struct replay_range* rng)
{
	if(!rng)
		return;
	delete_entry(rng->match);
	free(rng);
}

void
strip_end_white(char* p)
{
	size_t i;
	for(i = strlen(p); i > 0; i--) {
		if(isspace((unsigned char)p[i-1]))
			p[i-1] = 0;
		else return;
	}
}

/**
 * Read a range from file.
 * @param remain: Rest of line (after RANGE keyword).
 * @param in: file to read from.
 * @param name: name to print in errors.
 * @param pstate: read state structure with
 * 	with lineno : incremented as lines are read.
 * 	ttl, origin, prev for readentry.
 * @param line: line buffer.
 * @return: range object to add to list, or NULL on error.
 */
static struct replay_range*
replay_range_read(char* remain, FILE* in, const char* name,
	struct sldns_file_parse_state* pstate, char* line)
{
	struct replay_range* rng = (struct replay_range*)malloc(
		sizeof(struct replay_range));
	off_t pos;
	char *parse;
	struct entry* entry, *last = NULL;
	if(!rng)
		return NULL;
	memset(rng, 0, sizeof(*rng));
	/* read time range */
	if(sscanf(remain, " %d %d", &rng->start_step, &rng->end_step)!=2) {
		log_err("Could not read time range: %s", line);
		free(rng);
		return NULL;
	}
	/* read entries */
	pos = ftello(in);
	while(fgets(line, MAX_LINE_LEN-1, in)) {
		pstate->lineno++;
		parse = line;
		while(isspace((unsigned char)*parse))
			parse++;
		if(!*parse || *parse == ';') {
			pos = ftello(in);
			continue;
		}
		if(parse_keyword(&parse, "ADDRESS")) {
			while(isspace((unsigned char)*parse))
				parse++;
			strip_end_white(parse);
			if(!extstrtoaddr(parse, &rng->addr, &rng->addrlen,
				UNBOUND_DNS_PORT)) {
				log_err("Line %d: could not read ADDRESS: %s",
					pstate->lineno, parse);
				free(rng);
				return NULL;
			}
			pos = ftello(in);
			continue;
		}
		if(parse_keyword(&parse, "RANGE_END")) {
			return rng;
		}
		/* set position before line; read entry */
		pstate->lineno--;
		fseeko(in, pos, SEEK_SET);
		entry = read_entry(in, name, pstate, 1);
		if(!entry)
			fatal_exit("%d: bad entry", pstate->lineno);
		entry->next = NULL;
		if(last)
			last->next = entry;
		else	rng->match = entry;
		last = entry;

		pos = ftello(in);
	}
	replay_range_delete(rng);
	return NULL;
}

/** Read FILE match content */
static void
read_file_content(FILE* in, int* lineno, struct replay_moment* mom)
{
	char line[MAX_LINE_LEN];
	char* remain = line;
	struct config_strlist** last = &mom->file_content;
	line[MAX_LINE_LEN-1]=0;
	if(!fgets(line, MAX_LINE_LEN-1, in))
		fatal_exit("FILE_BEGIN expected at line %d", *lineno);
	if(!parse_keyword(&remain, "FILE_BEGIN"))
		fatal_exit("FILE_BEGIN expected at line %d", *lineno);
	while(fgets(line, MAX_LINE_LEN-1, in)) {
		(*lineno)++;
		if(strncmp(line, "FILE_END", 8) == 0) {
			return;
		}
		strip_end_white(line);
		if(!cfg_strlist_insert(last, strdup(line)))
			fatal_exit("malloc failure");
		last = &( (*last)->next );
	}
	fatal_exit("no FILE_END in input file");
}

/** read assign step info */
static void
read_assign_step(char* remain, struct replay_moment* mom)
{
	char buf[1024];
	char eq;
	int skip;
	buf[sizeof(buf)-1]=0;
	if(sscanf(remain, " %1023s %c %n", buf, &eq, &skip) != 2)
		fatal_exit("cannot parse assign: %s", remain);
	mom->variable = strdup(buf);
	if(eq != '=')
		fatal_exit("no '=' in assign: %s", remain);
	remain += skip;
	strip_end_white(remain);
	mom->string = strdup(remain);
	if(!mom->variable || !mom->string)
		fatal_exit("out of memory");
}

/**
 * Read a replay moment 'STEP' from file.
 * @param remain: Rest of line (after STEP keyword).
 * @param in: file to read from.
 * @param name: name to print in errors.
 * @param pstate: with lineno, ttl, origin, prev for parse state.
 * 	lineno is incremented.
 * @return: range object to add to list, or NULL on error.
 */
static struct replay_moment*
replay_moment_read(char* remain, FILE* in, const char* name,
	struct sldns_file_parse_state* pstate)
{
	struct replay_moment* mom = (struct replay_moment*)malloc(
		sizeof(struct replay_moment));
	int skip = 0;
	int readentry = 0;
	if(!mom)
		return NULL;
	memset(mom, 0, sizeof(*mom));
	if(sscanf(remain, " %d%n", &mom->time_step, &skip) != 1) {
		log_err("%d: cannot read number: %s", pstate->lineno, remain);
		free(mom);
		return NULL;
	}
	remain += skip;
	while(isspace((unsigned char)*remain))
		remain++;
	if(parse_keyword(&remain, "NOTHING")) {
		mom->evt_type = repevt_nothing;
	} else if(parse_keyword(&remain, "QUERY")) {
		mom->evt_type = repevt_front_query;
		readentry = 1;
		if(!extstrtoaddr("127.0.0.1", &mom->addr, &mom->addrlen,
			UNBOUND_DNS_PORT))
			fatal_exit("internal error");
	} else if(parse_keyword(&remain, "CHECK_ANSWER")) {
		mom->evt_type = repevt_front_reply;
		readentry = 1;
	} else if(parse_keyword(&remain, "CHECK_OUT_QUERY")) {
		mom->evt_type = repevt_back_query;
		readentry = 1;
	} else if(parse_keyword(&remain, "REPLY")) {
		mom->evt_type = repevt_back_reply;
		readentry = 1;
	} else if(parse_keyword(&remain, "TIMEOUT")) {
		mom->evt_type = repevt_timeout;
	} else if(parse_keyword(&remain, "TIME_PASSES")) {
		mom->evt_type = repevt_time_passes;
		while(isspace((unsigned char)*remain))
			remain++;
		if(parse_keyword(&remain, "EVAL")) {
			while(isspace((unsigned char)*remain))
				remain++;
			mom->string = strdup(remain);
			if(!mom->string) fatal_exit("out of memory");
			if(strlen(mom->string)>0)
				mom->string[strlen(mom->string)-1]=0;
			remain += strlen(mom->string);
		}
	} else if(parse_keyword(&remain, "CHECK_AUTOTRUST")) {
		mom->evt_type = repevt_autotrust_check;
		while(isspace((unsigned char)*remain))
			remain++;
		strip_end_white(remain);
		mom->autotrust_id = strdup(remain);
		if(!mom->autotrust_id) fatal_exit("out of memory");
		read_file_content(in, &pstate->lineno, mom);
	} else if(parse_keyword(&remain, "CHECK_TEMPFILE")) {
		mom->evt_type = repevt_tempfile_check;
		while(isspace((unsigned char)*remain))
			remain++;
		strip_end_white(remain);
		mom->autotrust_id = strdup(remain);
		if(!mom->autotrust_id) fatal_exit("out of memory");
		read_file_content(in, &pstate->lineno, mom);
	} else if(parse_keyword(&remain, "ERROR")) {
		mom->evt_type = repevt_error;
	} else if(parse_keyword(&remain, "TRAFFIC")) {
		mom->evt_type = repevt_traffic;
	} else if(parse_keyword(&remain, "ASSIGN")) {
		mom->evt_type = repevt_assign;
		read_assign_step(remain, mom);
	} else if(parse_keyword(&remain, "INFRA_RTT")) {
		char *s, *m;
		mom->evt_type = repevt_infra_rtt;
		while(isspace((unsigned char)*remain))
			remain++;
		s = remain;
		remain = strchr(s, ' ');
		if(!remain) fatal_exit("expected three args for INFRA_RTT");
		remain[0] = 0;
		remain++;
		while(isspace((unsigned char)*remain))
			remain++;
		m = strchr(remain, ' ');
		if(!m) fatal_exit("expected three args for INFRA_RTT");
		m[0] = 0;
		m++;
		while(isspace((unsigned char)*m))
			m++;
		if(!extstrtoaddr(s, &mom->addr, &mom->addrlen, UNBOUND_DNS_PORT))
			fatal_exit("bad infra_rtt address %s", s);
		strip_end_white(m);
		mom->variable = strdup(remain);
		mom->string = strdup(m);
		if(!mom->string) fatal_exit("out of memory");
		if(!mom->variable) fatal_exit("out of memory");
	} else if(parse_keyword(&remain, "FLUSH_MESSAGE")) {
		mom->evt_type = repevt_flush_message;
		while(isspace((unsigned char)*remain))
			remain++;
		strip_end_white(remain);
		mom->string = strdup(remain);
		if(!mom->string) fatal_exit("out of memory");
	} else if(parse_keyword(&remain, "EXPIRE_MESSAGE")) {
		mom->evt_type = repevt_expire_message;
		while(isspace((unsigned char)*remain))
			remain++;
		strip_end_white(remain);
		mom->string = strdup(remain);
		if(!mom->string) fatal_exit("out of memory");
	} else {
		log_err("%d: unknown event type %s", pstate->lineno, remain);
		free(mom);
		return NULL;
	}
	while(isspace((unsigned char)*remain))
		remain++;
	if(parse_keyword(&remain, "ADDRESS")) {
		while(isspace((unsigned char)*remain))
			remain++;
		strip_end_white(remain);
		if(!extstrtoaddr(remain, &mom->addr, &mom->addrlen,
			UNBOUND_DNS_PORT)) {
			log_err("line %d: could not parse ADDRESS: %s",
				pstate->lineno, remain);
			free(mom);
			return NULL;
		}
	}
	if(parse_keyword(&remain, "ELAPSE")) {
		double sec;
		errno = 0;
		sec = strtod(remain, &remain);
		if(sec == 0. && errno != 0) {
			log_err("line %d: could not parse ELAPSE: %s (%s)",
				pstate->lineno, remain, strerror(errno));
			free(mom);
			return NULL;
		}
#ifndef S_SPLINT_S
		mom->elapse.tv_sec = (int)sec;
		mom->elapse.tv_usec = (int)((sec - (double)mom->elapse.tv_sec)
			*1000000. + 0.5);
#endif
	}

	if(readentry) {
		mom->match = read_entry(in, name, pstate, 1);
		if(!mom->match) {
			free(mom);
			return NULL;
		}
	}

	return mom;
}

/** makes scenario with title on rest of line */
static struct replay_scenario*
make_scenario(char* line)
{
	struct replay_scenario* scen;
	while(isspace((unsigned char)*line))
		line++;
	if(!*line) {
		log_err("scenario: no title given");
		return NULL;
	}
	scen = (struct replay_scenario*)malloc(sizeof(struct replay_scenario));
	if(!scen)
		return NULL;
	memset(scen, 0, sizeof(*scen));
	scen->title = strdup(line);
	if(!scen->title) {
		free(scen);
		return NULL;
	}
	return scen;
}

struct replay_scenario*
replay_scenario_read(FILE* in, const char* name, int* lineno)
{
	char line[MAX_LINE_LEN];
	char *parse;
	struct replay_scenario* scen = NULL;
	struct sldns_file_parse_state pstate;
	line[MAX_LINE_LEN-1]=0;
	memset(&pstate, 0, sizeof(pstate));
	pstate.default_ttl = 3600;
	pstate.lineno = *lineno;

	while(fgets(line, MAX_LINE_LEN-1, in)) {
		parse=line;
		pstate.lineno++;
		(*lineno)++;
		while(isspace((unsigned char)*parse))
			parse++;
		if(!*parse)
			continue; /* empty line */
		if(parse_keyword(&parse, ";"))
			continue; /* comment */
		if(parse_keyword(&parse, "SCENARIO_BEGIN")) {
			if(scen)
				fatal_exit("%d: double SCENARIO_BEGIN", *lineno);
			scen = make_scenario(parse);
			if(!scen)
				fatal_exit("%d: could not make scen", *lineno);
			continue;
		}
		if(!scen)
			fatal_exit("%d: expected SCENARIO", *lineno);
		if(parse_keyword(&parse, "RANGE_BEGIN")) {
			struct replay_range* newr = replay_range_read(parse,
				in, name, &pstate, line);
			if(!newr)
				fatal_exit("%d: bad range", pstate.lineno);
			*lineno = pstate.lineno;
			newr->next_range = scen->range_list;
			scen->range_list = newr;
		} else if(parse_keyword(&parse, "STEP")) {
			struct replay_moment* mom = replay_moment_read(parse,
				in, name, &pstate);
			if(!mom)
				fatal_exit("%d: bad moment", pstate.lineno);
			*lineno = pstate.lineno;
			if(scen->mom_last &&
				scen->mom_last->time_step >= mom->time_step)
				fatal_exit("%d: time goes backwards", *lineno);
			if(scen->mom_last)
				scen->mom_last->mom_next = mom;
			else	scen->mom_first = mom;
			scen->mom_last = mom;
		} else if(parse_keyword(&parse, "SCENARIO_END")) {
			struct replay_moment *p = scen->mom_first;
			int num = 0;
			while(p) {
				num++;
				p = p->mom_next;
			}
			log_info("Scenario has %d steps", num);
			return scen;
		}
	}
	log_err("scenario read failed at line %d (no SCENARIO_END?)", *lineno);
	replay_scenario_delete(scen);
	return NULL;
}

void
replay_scenario_delete(struct replay_scenario* scen)
{
	struct replay_moment* mom, *momn;
	struct replay_range* rng, *rngn;
	if(!scen)
		return;
	free(scen->title);
	mom = scen->mom_first;
	while(mom) {
		momn = mom->mom_next;
		replay_moment_delete(mom);
		mom = momn;
	}
	rng = scen->range_list;
	while(rng) {
		rngn = rng->next_range;
		replay_range_delete(rng);
		rng = rngn;
	}
	free(scen);
}

/** fetch oldest timer in list that is enabled */
static struct fake_timer*
first_timer(struct replay_runtime* runtime)
{
	struct fake_timer* p, *res = NULL;
	for(p=runtime->timer_list; p; p=p->next) {
		if(!p->enabled)
			continue;
		if(!res)
			res = p;
		else if(timeval_smaller(&p->tv, &res->tv))
			res = p;
	}
	return res;
}

struct fake_timer*
replay_get_oldest_timer(struct replay_runtime* runtime)
{
	struct fake_timer* t = first_timer(runtime);
	if(t && timeval_smaller(&t->tv, &runtime->now_tv))
		return t;
	return NULL;
}

int
replay_var_compare(const void* a, const void* b)
{
	struct replay_var* x = (struct replay_var*)a;
	struct replay_var* y = (struct replay_var*)b;
	return strcmp(x->name, y->name);
}

rbtree_type*
macro_store_create(void)
{
	return rbtree_create(&replay_var_compare);
}

/** helper function to delete macro values */
static void
del_macro(rbnode_type* x, void* ATTR_UNUSED(arg))
{
	struct replay_var* v = (struct replay_var*)x;
	free(v->name);
	free(v->value);
	free(v);
}

void
macro_store_delete(rbtree_type* store)
{
	if(!store)
		return;
	traverse_postorder(store, del_macro, NULL);
	free(store);
}

/** return length of macro */
static size_t
macro_length(char* text)
{
	/* we are after ${, looking for } */
	int depth = 0;
	size_t len = 0;
	while(*text) {
		len++;
		if(*text == '}') {
			if(depth == 0)
				break;
			depth--;
		} else if(text[0] == '$' && text[1] == '{') {
			depth++;
		}
		text++;
	}
	return len;
}

/** insert new stuff at start of buffer */
static int
do_buf_insert(char* buf, size_t remain, char* after, char* inserted)
{
	char* save = strdup(after);
	size_t len;
	if(!save) return 0;
	if(strlen(inserted) > remain) {
		free(save);
		return 0;
	}
	len = strlcpy(buf, inserted, remain);
	buf += len;
	remain -= len;
	(void)strlcpy(buf, save, remain);
	free(save);
	return 1;
}

/** do macro recursion */
static char*
do_macro_recursion(rbtree_type* store, struct replay_runtime* runtime,
	char* at, size_t remain)
{
	char* after = at+2;
	char* expand = macro_expand(store, runtime, &after);
	if(!expand)
		return NULL; /* expansion failed */
	if(!do_buf_insert(at, remain, after, expand)) {
		free(expand);
		return NULL;
	}
	free(expand);
	return at; /* and parse over the expanded text to see if again */
}

/** get var from store */
static struct replay_var*
macro_getvar(rbtree_type* store, char* name)
{
	struct replay_var k;
	k.node.key = &k;
	k.name = name;
	return (struct replay_var*)rbtree_search(store, &k);
}

/** do macro variable */
static char*
do_macro_variable(rbtree_type* store, char* buf, size_t remain)
{
	struct replay_var* v;
	char* at = buf+1;
	char* name = at;
	char sv;
	if(at[0]==0)
		return NULL; /* no variable name after $ */
	while(*at && (isalnum((unsigned char)*at) || *at=='_')) {
		at++;
	}
	/* terminator, we are working in macro_expand() buffer */
	sv = *at;
	*at = 0;
	v = macro_getvar(store, name);
	*at = sv;

	if(!v) {
		log_err("variable is not defined: $%s", name);
		return NULL; /* variable undefined is error for now */
	}

	/* insert the variable contents */
	if(!do_buf_insert(buf, remain, at, v->value))
		return NULL;
	return buf; /* and expand the variable contents */
}

/** do ctime macro on argument */
static char*
do_macro_ctime(char* arg)
{
	char buf[32];
	time_t tt = (time_t)atoi(arg);
	if(tt == 0 && strcmp(arg, "0") != 0) {
		log_err("macro ctime: expected number, not: %s", arg);
		return NULL;
	}
	ctime_r(&tt, buf);
#ifdef USE_WINSOCK
	if(strlen(buf) > 10 && buf[7]==' ' && buf[8]=='0')
		buf[8]=' '; /* fix error in windows ctime */
#endif
	strip_end_white(buf);
	return strdup(buf);
}

/** perform arithmetic operator */
static double
perform_arith(double x, char op, double y, double* res)
{
	switch(op) {
	case '+':
		*res = x+y;
		break;
	case '-':
		*res = x-y;
		break;
	case '/':
		*res = x/y;
		break;
	case '*':
		*res = x*y;
		break;
	default:
		*res = 0;
		return 0;
	}

	return 1;
}

/** do macro arithmetic on two numbers and operand */
static char*
do_macro_arith(char* orig, size_t remain, char** arithstart)
{
	double x, y, result;
	char operator;
	int skip;
	char buf[32];
	char* at;
	/* not yet done? we want number operand number expanded first. */
	if(!*arithstart) {
		/* remember start pos of expr, skip the first number */
		at = orig;
		*arithstart = at;
		while(*at && (isdigit((unsigned char)*at) || *at == '.'))
			at++;
		return at;
	}
	/* move back to start */
	remain += (size_t)(orig - *arithstart);
	at = *arithstart;

	/* parse operands */
	if(sscanf(at, " %lf %c %lf%n", &x, &operator, &y, &skip) != 3) {
		*arithstart = NULL;
		return do_macro_arith(orig, remain, arithstart);
	}
	if(isdigit((unsigned char)operator)) {
		*arithstart = orig;
		return at+skip; /* do nothing, but setup for later number */
	}

	/* calculate result */
	if(!perform_arith(x, operator, y, &result)) {
		log_err("unknown operator: %s", at);
		return NULL;
	}

	/* put result back in buffer */
	snprintf(buf, sizeof(buf), "%.12g", result);
	if(!do_buf_insert(at, remain, at+skip, buf))
		return NULL;

	/* the result can be part of another expression, restart that */
	*arithstart = NULL;
	return at;
}

/** Do range macro on expanded buffer */
static char*
do_macro_range(char* buf)
{
	double x, y, z;
	if(sscanf(buf, " %lf %lf %lf", &x, &y, &z) != 3) {
		log_err("range func requires 3 args: %s", buf);
		return NULL;
	}
	if(x <= y && y <= z) {
		char res[1024];
		snprintf(res, sizeof(res), "%.24g", y);
		return strdup(res);
	}
	fatal_exit("value %.24g not in range [%.24g, %.24g]", y, x, z);
	return NULL;
}

static char*
macro_expand(rbtree_type* store, struct replay_runtime* runtime, char** text)
{
	char buf[10240];
	char* at = *text;
	size_t len = macro_length(at);
	int dofunc = 0;
	char* arithstart = NULL;
	if(len >= sizeof(buf))
		return NULL; /* too long */
	buf[0] = 0;
	(void)strlcpy(buf, at, len+1-1); /* do not copy last '}' character */
	at = buf;

	/* check for functions */
	if(strcmp(buf, "time") == 0) {
		if(runtime)
			snprintf(buf, sizeof(buf), ARG_LL "d", (long long)runtime->now_secs);
		else
			snprintf(buf, sizeof(buf), ARG_LL "d", (long long)0);
		*text += len;
		return strdup(buf);
	} else if(strcmp(buf, "timeout") == 0) {
		time_t res = 0;
		if(runtime) {
			struct fake_timer* t = first_timer(runtime);
			if(t && (time_t)t->tv.tv_sec >= runtime->now_secs)
				res = (time_t)t->tv.tv_sec - runtime->now_secs;
		}
		snprintf(buf, sizeof(buf), ARG_LL "d", (long long)res);
		*text += len;
		return strdup(buf);
	} else if(strncmp(buf, "ctime ", 6) == 0 ||
		strncmp(buf, "ctime\t", 6) == 0) {
		at += 6;
		dofunc = 1;
	} else if(strncmp(buf, "range ", 6) == 0 ||
		strncmp(buf, "range\t", 6) == 0) {
		at += 6;
		dofunc = 1;
	}

	/* actual macro text expansion */
	while(*at) {
		size_t remain = sizeof(buf)-strlen(buf);
		if(strncmp(at, "${", 2) == 0) {
			at = do_macro_recursion(store, runtime, at, remain);
		} else if(*at == '$') {
			at = do_macro_variable(store, at, remain);
		} else if(isdigit((unsigned char)*at)) {
			at = do_macro_arith(at, remain, &arithstart);
		} else {
			/* copy until whitespace or operator */
			if(*at && (isalnum((unsigned char)*at) || *at=='_')) {
				at++;
				while(*at && (isalnum((unsigned char)*at) || *at=='_'))
					at++;
			} else at++;
		}
		if(!at) return NULL; /* failure */
	}
	*text += len;
	if(dofunc) {
		/* post process functions, buf has the argument(s) */
		if(strncmp(buf, "ctime", 5) == 0) {
			return do_macro_ctime(buf+6);
		} else if(strncmp(buf, "range", 5) == 0) {
			return do_macro_range(buf+6);
		}
	}
	return strdup(buf);
}

char*
macro_process(rbtree_type* store, struct replay_runtime* runtime, char* text)
{
	char buf[10240];
	char* next, *expand;
	char* at = text;
	if(!strstr(text, "${"))
		return strdup(text); /* no macros */
	buf[0] = 0;
	buf[sizeof(buf)-1]=0;
	while( (next=strstr(at, "${")) ) {
		/* copy text before next macro */
		if((size_t)(next-at) >= sizeof(buf)-strlen(buf))
			return NULL; /* string too long */
		(void)strlcpy(buf+strlen(buf), at, (size_t)(next-at+1));
		/* process the macro itself */
		next += 2;
		expand = macro_expand(store, runtime, &next);
		if(!expand) return NULL; /* expansion failed */
		(void)strlcpy(buf+strlen(buf), expand, sizeof(buf)-strlen(buf));
		free(expand);
		at = next;
	}
	/* copy remainder fixed text */
	(void)strlcpy(buf+strlen(buf), at, sizeof(buf)-strlen(buf));
	return strdup(buf);
}

char*
macro_lookup(rbtree_type* store, char* name)
{
	struct replay_var* x = macro_getvar(store, name);
	if(!x) return strdup("");
	return strdup(x->value);
}

void macro_print_debug(rbtree_type* store)
{
	struct replay_var* x;
	RBTREE_FOR(x, struct replay_var*, store) {
		log_info("%s = %s", x->name, x->value);
	}
}

int
macro_assign(rbtree_type* store, char* name, char* value)
{
	struct replay_var* x = macro_getvar(store, name);
	if(x) {
		free(x->value);
	} else {
		x = (struct replay_var*)malloc(sizeof(*x));
		if(!x) return 0;
		x->node.key = x;
		x->name = strdup(name);
		if(!x->name) {
			free(x);
			return 0;
		}
		(void)rbtree_insert(store, &x->node);
	}
	x->value = strdup(value);
	return x->value != NULL;
}

/* testbound assert function for selftest.  counts the number of tests */
#define tb_assert(x) \
	do { if(!(x)) fatal_exit("%s:%d: %s: assertion %s failed", \
		__FILE__, __LINE__, __func__, #x); \
		num_asserts++; \
	} while(0);

void testbound_selftest(void)
{
	/* test the macro store */
	rbtree_type* store = macro_store_create();
	char* v;
	int r;
	int num_asserts = 0;
	tb_assert(store);

	v = macro_lookup(store, "bla");
	tb_assert(strcmp(v, "") == 0);
	free(v);

	v = macro_lookup(store, "vlerk");
	tb_assert(strcmp(v, "") == 0);
	free(v);

	r = macro_assign(store, "bla", "waarde1");
	tb_assert(r);

	v = macro_lookup(store, "vlerk");
	tb_assert(strcmp(v, "") == 0);
	free(v);

	v = macro_lookup(store, "bla");
	tb_assert(strcmp(v, "waarde1") == 0);
	free(v);

	r = macro_assign(store, "vlerk", "kanteel");
	tb_assert(r);

	v = macro_lookup(store, "bla");
	tb_assert(strcmp(v, "waarde1") == 0);
	free(v);

	v = macro_lookup(store, "vlerk");
	tb_assert(strcmp(v, "kanteel") == 0);
	free(v);

	r = macro_assign(store, "bla", "ww");
	tb_assert(r);

	v = macro_lookup(store, "bla");
	tb_assert(strcmp(v, "ww") == 0);
	free(v);

	tb_assert( macro_length("}") == 1);
	tb_assert( macro_length("blabla}") == 7);
	tb_assert( macro_length("bla${zoink}bla}") == 7+8);
	tb_assert( macro_length("bla${zoink}${bla}bla}") == 7+8+6);

	v = macro_process(store, NULL, "");
	tb_assert( v && strcmp(v, "") == 0);
	free(v);

	v = macro_process(store, NULL, "${}");
	tb_assert( v && strcmp(v, "") == 0);
	free(v);

	v = macro_process(store, NULL, "blabla ${} dinges");
	tb_assert( v && strcmp(v, "blabla  dinges") == 0);
	free(v);

	v = macro_process(store, NULL, "1${$bla}2${$bla}3");
	tb_assert( v && strcmp(v, "1ww2ww3") == 0);
	free(v);

	v = macro_process(store, NULL, "it is ${ctime 123456}");
	tb_assert( v && strcmp(v, "it is Fri Jan  2 10:17:36 1970") == 0);
	free(v);

	r = macro_assign(store, "t1", "123456");
	tb_assert(r);
	v = macro_process(store, NULL, "it is ${ctime ${$t1}}");
	tb_assert( v && strcmp(v, "it is Fri Jan  2 10:17:36 1970") == 0);
	free(v);

	v = macro_process(store, NULL, "it is ${ctime $t1}");
	tb_assert( v && strcmp(v, "it is Fri Jan  2 10:17:36 1970") == 0);
	free(v);

	r = macro_assign(store, "x", "1");
	tb_assert(r);
	r = macro_assign(store, "y", "2");
	tb_assert(r);
	v = macro_process(store, NULL, "${$x + $x}");
	tb_assert( v && strcmp(v, "2") == 0);
	free(v);
	v = macro_process(store, NULL, "${$x - $x}");
	tb_assert( v && strcmp(v, "0") == 0);
	free(v);
	v = macro_process(store, NULL, "${$y * $y}");
	tb_assert( v && strcmp(v, "4") == 0);
	free(v);
	v = macro_process(store, NULL, "${32 / $y + $x + $y}");
	tb_assert( v && strcmp(v, "19") == 0);
	free(v);

	v = macro_process(store, NULL, "${32 / ${$y+$y} + ${${100*3}/3}}");
	tb_assert( v && strcmp(v, "108") == 0);
	free(v);

	v = macro_process(store, NULL, "${1 2 33 2 1}");
	tb_assert( v && strcmp(v, "1 2 33 2 1") == 0);
	free(v);

	v = macro_process(store, NULL, "${123 3 + 5}");
	tb_assert( v && strcmp(v, "123 8") == 0);
	free(v);

	v = macro_process(store, NULL, "${123 glug 3 + 5}");
	tb_assert( v && strcmp(v, "123 glug 8") == 0);
	free(v);

	macro_store_delete(store);
	printf("selftest successful (%d checks).\n", num_asserts);
}
