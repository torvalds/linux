/*	$OpenBSD: lka_filter.c,v 1.78 2024/08/12 09:32:44 op Exp $	*/

/*
 * Copyright (c) 2018 Gilles Chehade <gilles@poolp.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

#define	PROTOCOL_VERSION	"0.7"

struct filter;
struct filter_session;
static void	filter_protocol_internal(struct filter_session *, uint64_t *, uint64_t, enum filter_phase, const char *);
static void	filter_protocol(uint64_t, enum filter_phase, const char *);
static void	filter_protocol_next(uint64_t, uint64_t, enum filter_phase);
static void	filter_protocol_query(struct filter *, uint64_t, uint64_t, const char *, const char *);

static void	filter_data_internal(struct filter_session *, uint64_t, uint64_t, const char *);
static void	filter_data(uint64_t, const char *);
static void	filter_data_next(uint64_t, uint64_t, const char *);
static void	filter_data_query(struct filter *, uint64_t, uint64_t, const char *);

static int	filter_builtins_notimpl(struct filter_session *, struct filter *, uint64_t, const char *);
static int	filter_builtins_connect(struct filter_session *, struct filter *, uint64_t, const char *);
static int	filter_builtins_helo(struct filter_session *, struct filter *, uint64_t, const char *);
static int	filter_builtins_mail_from(struct filter_session *, struct filter *, uint64_t, const char *);
static int	filter_builtins_rcpt_to(struct filter_session *, struct filter *, uint64_t, const char *);
static int	filter_builtins_data(struct filter_session *, struct filter *, uint64_t, const char *);
static int	filter_builtins_commit(struct filter_session *, struct filter *, uint64_t, const char *);

static void	filter_result_proceed(uint64_t);
static void	filter_result_report(uint64_t, const char *);
static void	filter_result_junk(uint64_t);
static void	filter_result_rewrite(uint64_t, const char *);
static void	filter_result_reject(uint64_t, const char *);
static void	filter_result_disconnect(uint64_t, const char *);

static void	filter_session_io(struct io *, int, void *);
void		lka_filter_process_response(const char *, const char *);


struct filter_session {
	uint64_t	id;
	struct io	*io;

	char *lastparam;

	char *filter_name;
	struct sockaddr_storage ss_src;
	struct sockaddr_storage ss_dest;
	char *rdns;
	int fcrdns;

	char *helo;
	char *username;
	char *mail_from;
	
	enum filter_phase	phase;
};

static struct filter_exec {
	enum filter_phase	phase;
	const char	       *phase_name;
	int		       (*func)(struct filter_session *, struct filter *, uint64_t, const char *);
} filter_execs[FILTER_PHASES_COUNT] = {
	{ FILTER_CONNECT,	"connect",	filter_builtins_connect },
	{ FILTER_HELO,		"helo",		filter_builtins_helo },
	{ FILTER_EHLO,		"ehlo",		filter_builtins_helo },
	{ FILTER_STARTTLS,     	"starttls",	filter_builtins_notimpl },
	{ FILTER_AUTH,     	"auth",		filter_builtins_notimpl },
	{ FILTER_MAIL_FROM,    	"mail-from",	filter_builtins_mail_from },
	{ FILTER_RCPT_TO,    	"rcpt-to",	filter_builtins_rcpt_to },
	{ FILTER_DATA,    	"data",		filter_builtins_data },
	{ FILTER_DATA_LINE,    	"data-line",   	filter_builtins_notimpl },
	{ FILTER_RSET,    	"rset",		filter_builtins_notimpl },
	{ FILTER_QUIT,    	"quit",		filter_builtins_notimpl },
	{ FILTER_NOOP,    	"noop",		filter_builtins_notimpl },
	{ FILTER_HELP,    	"help",		filter_builtins_notimpl },
	{ FILTER_WIZ,    	"wiz",		filter_builtins_notimpl },
	{ FILTER_COMMIT,    	"commit",      	filter_builtins_commit },
};

struct filter {
	uint64_t		id;
	uint32_t		phases;
	const char	       *name;
	const char	       *proc;
	struct filter  	      **chain;
	size_t 			chain_size;
	struct filter_config   *config;
};
static struct dict filters;

struct filter_entry {
	TAILQ_ENTRY(filter_entry)	entries;
	uint64_t			id;
	const char		       *name;
};

struct filter_chain {
	TAILQ_HEAD(, filter_entry)		chain[nitems(filter_execs)];
};

static struct tree	sessions;
static int		filters_inited;

static struct dict	filter_chains;

struct reporter_proc {
	TAILQ_ENTRY(reporter_proc)	entries;
	const char		       *name;
};
TAILQ_HEAD(reporters, reporter_proc);

static struct dict	report_smtp_in;
static struct dict	report_smtp_out;

static struct smtp_events {
	const char     *event;
} smtp_events[] = {
	{ "link-connect" },
	{ "link-disconnect" },
	{ "link-greeting" },
	{ "link-identify" },
	{ "link-tls" },
	{ "link-auth" },

	{ "tx-reset" },
	{ "tx-begin" },
	{ "tx-mail" },
	{ "tx-rcpt" },
	{ "tx-envelope" },
	{ "tx-data" },
	{ "tx-commit" },
	{ "tx-rollback" },

	{ "protocol-client" },
	{ "protocol-server" },

	{ "filter-report" },
	{ "filter-response" },

	{ "timeout" },
};

static int			processors_inited = 0;
static struct dict		processors;

struct processor_instance {
	char			*name;
	struct io		*io;
	struct io		*errfd;
	int			 ready;
	uint32_t		 subsystems;
};

static void	processor_io(struct io *, int, void *);
static void	processor_errfd(struct io *, int, void *);
void		lka_filter_process_response(const char *, const char *);

int
lka_proc_ready(void)
{
	void	*iter;
	struct processor_instance	*pi;

	iter = NULL;
	while (dict_iter(&processors, &iter, NULL, (void **)&pi))
		if (!pi->ready)
			return 0;
	return 1;
}

static void
lka_proc_config(struct processor_instance *pi)
{
	io_printf(pi->io, "config|smtpd-version|%s\n", SMTPD_VERSION);
	io_printf(pi->io, "config|protocol|%s\n", PROTOCOL_VERSION);
	io_printf(pi->io, "config|smtp-session-timeout|%d\n", SMTPD_SESSION_TIMEOUT);
	if (pi->subsystems & FILTER_SUBSYSTEM_SMTP_IN)
		io_printf(pi->io, "config|subsystem|smtp-in\n");
	if (pi->subsystems & FILTER_SUBSYSTEM_SMTP_OUT)
		io_printf(pi->io, "config|subsystem|smtp-out\n");
	io_printf(pi->io, "config|admd|%s\n",
	    env->sc_admd != NULL ? env->sc_admd : env->sc_hostname);
	io_printf(pi->io, "config|ready\n");
}

void
lka_proc_forked(const char *name, uint32_t subsystems, int fd)
{
	struct processor_instance	*processor;

	if (!processors_inited) {
		dict_init(&processors);
		processors_inited = 1;
	}

	processor = xcalloc(1, sizeof *processor);
	processor->name = xstrdup(name);
	processor->io = io_new();
	processor->subsystems = subsystems;

	io_set_nonblocking(fd);

	io_set_fd(processor->io, fd);
	io_set_callback(processor->io, processor_io, processor->name);
	dict_xset(&processors, name, processor);
}

void
lka_proc_errfd(const char *name, int fd)
{
	struct processor_instance	*processor;

	processor = dict_xget(&processors, name);

	io_set_nonblocking(fd);

	processor->errfd = io_new();
	io_set_fd(processor->errfd, fd);
	io_set_callback(processor->errfd, processor_errfd, processor->name);

	lka_proc_config(processor);
}

struct io *
lka_proc_get_io(const char *name)
{
	struct processor_instance *processor;

	processor = dict_xget(&processors, name);

	return processor->io;
}

static void
processor_register(const char *name, const char *line)
{
	struct processor_instance *processor;

	processor = dict_xget(&processors, name);

	if (strcmp(line, "register|ready") == 0) {
		processor->ready = 1;
		return;
	}

	if (strncmp(line, "register|report|", 16) == 0) {
		lka_report_register_hook(name, line+16);
		return;
	}

	if (strncmp(line, "register|filter|", 16) == 0) {
		lka_filter_register_hook(name, line+16);
		return;
	}

	fatalx("Invalid register line received: %s", line);
}

static void
processor_io(struct io *io, int evt, void *arg)
{
	struct processor_instance *processor;
	const char		*name = arg;
	char			*line = NULL;
	ssize_t			 len;

	switch (evt) {
	case IO_DATAIN:
		while ((line = io_getline(io, &len)) != NULL) {
			if (strncmp("register|", line, 9) == 0) {
				processor_register(name, line);
				continue;
			}
			
			processor = dict_xget(&processors, name);
			if (!processor->ready)
				fatalx("Non-register message before register|"
				    "ready: %s", line);
			else if (strncmp(line, "filter-result|", 14) == 0 ||
			    strncmp(line, "filter-dataline|", 16) == 0)
				lka_filter_process_response(name, line);
			else if (strncmp(line, "report|", 7) == 0)
				lka_report_proc(name, line);
			else
				fatalx("Invalid filter message type: %s", line);
		}
	}
}

static void
processor_errfd(struct io *io, int evt, void *arg)
{
	const char	*name = arg;
	char		*line = NULL;
	ssize_t		 len;

	switch (evt) {
	case IO_DATAIN:
		while ((line = io_getline(io, &len)) != NULL)
			log_warnx("%s: %s", name, line);
	}
}

void
lka_filter_init(void)
{
	void		*iter;
	const char	*name;
	struct filter  	*filter;
	struct filter_config	*filter_config;
	size_t		i;
	char		 buffer[LINE_MAX];	/* for traces */

	dict_init(&filters);
	dict_init(&filter_chains);

	/* first pass, allocate and init individual filters */
	iter = NULL;
	while (dict_iter(env->sc_filters_dict, &iter, &name, (void **)&filter_config)) {
		switch (filter_config->filter_type) {
		case FILTER_TYPE_BUILTIN:
			filter = xcalloc(1, sizeof(*filter));
			filter->name = name;
			filter->phases |= (1<<filter_config->phase);
			filter->config = filter_config;
			dict_set(&filters, name, filter);
			log_trace(TRACE_FILTERS, "filters init type=builtin, name=%s, hooks=%08x",
			    name, filter->phases);
			break;

		case FILTER_TYPE_PROC:
			filter = xcalloc(1, sizeof(*filter));
			filter->name = name;
			filter->proc = filter_config->proc;
			filter->config = filter_config;
			dict_set(&filters, name, filter);
			log_trace(TRACE_FILTERS, "filters init type=proc, name=%s, proc=%s",
			    name, filter_config->proc);
			break;

		case FILTER_TYPE_CHAIN:
			break;
		}
	}

	/* second pass, allocate and init filter chains but don't build yet */
	iter = NULL;
	while (dict_iter(env->sc_filters_dict, &iter, &name, (void **)&filter_config)) {
		switch (filter_config->filter_type) {
		case FILTER_TYPE_CHAIN:
			filter = xcalloc(1, sizeof(*filter));
			filter->name = name;
			filter->chain = xcalloc(filter_config->chain_size, sizeof(void **));
			filter->chain_size = filter_config->chain_size;
			filter->config = filter_config;

			buffer[0] = '\0';
			for (i = 0; i < filter->chain_size; ++i) {
				filter->chain[i] = dict_xget(&filters, filter_config->chain[i]);
				if (i)
					(void)strlcat(buffer, ", ", sizeof buffer);
				(void)strlcat(buffer, filter->chain[i]->name, sizeof buffer);
			}
			log_trace(TRACE_FILTERS, "filters init type=chain, name=%s { %s }", name, buffer);

			dict_set(&filters, name, filter);
			break;

		case FILTER_TYPE_BUILTIN:
		case FILTER_TYPE_PROC:
			break;
		}
	}
}

void
lka_filter_register_hook(const char *name, const char *hook)
{
	struct filter		*filter;
	const char	*filter_name;
	void		*iter;
	size_t	i;

	if (strncasecmp(hook, "smtp-in|", 8) == 0) {
		hook += 8;
	}
	else
		fatalx("Invalid message direction: %s", hook);

	for (i = 0; i < nitems(filter_execs); i++)
		if (strcmp(hook, filter_execs[i].phase_name) == 0)
			break;
	if (i == nitems(filter_execs))
		fatalx("Unrecognized report name: %s", hook);

	iter = NULL;
	while (dict_iter(&filters, &iter, &filter_name, (void **)&filter))
		if (filter->proc && strcmp(name, filter->proc) == 0)
			filter->phases |= (1<<filter_execs[i].phase);
}

void
lka_filter_ready(void)
{
	struct filter  	*filter;
	struct filter  	*subfilter;
	const char	*filter_name;
	struct filter_entry	*filter_entry;
	struct filter_chain	*filter_chain;
	void		*iter;
	size_t		i;
	size_t		j;

	/* all filters are ready, actually build the filter chains */
	iter = NULL;
	while (dict_iter(&filters, &iter, &filter_name, (void **)&filter)) {
		filter_chain = xcalloc(1, sizeof *filter_chain);
		for (i = 0; i < nitems(filter_execs); i++)
			TAILQ_INIT(&filter_chain->chain[i]);
		dict_set(&filter_chains, filter_name, filter_chain);

		if (filter->chain) {
			for (i = 0; i < filter->chain_size; i++) {
				subfilter = filter->chain[i];
				for (j = 0; j < nitems(filter_execs); ++j) {
					if (subfilter->phases & (1<<j)) {
						filter_entry = xcalloc(1, sizeof *filter_entry);
						filter_entry->id = generate_uid();
						filter_entry->name = subfilter->name;
						TAILQ_INSERT_TAIL(&filter_chain->chain[j],
						    filter_entry, entries);
					}
				}
			}
			continue;
		}

		for (i = 0; i < nitems(filter_execs); ++i) {
			if (filter->phases & (1<<i)) {
				filter_entry = xcalloc(1, sizeof *filter_entry);
				filter_entry->id = generate_uid();
				filter_entry->name = filter_name;
				TAILQ_INSERT_TAIL(&filter_chain->chain[i],
				    filter_entry, entries);
			}
		}
	}
}

int
lka_filter_proc_in_session(uint64_t reqid, const char *proc)
{
	struct filter_session	*fs;
	struct filter		*filter;
	size_t			 i;

	if ((fs = tree_get(&sessions, reqid)) == NULL)
		return 0;

	filter = dict_get(&filters, fs->filter_name);
	if (filter == NULL || (filter->proc == NULL && filter->chain == NULL))
		return 0;

	if (filter->proc)
		return strcmp(filter->proc, proc) == 0 ? 1 : 0;

	for (i = 0; i < filter->chain_size; i++)
		if (filter->chain[i]->proc &&
		    strcmp(filter->chain[i]->proc, proc) == 0)
			return 1;

	return 0;
}

void
lka_filter_begin(uint64_t reqid, const char *filter_name)
{
	struct filter_session	*fs;

	if (!filters_inited) {
		tree_init(&sessions);
		filters_inited = 1;
	}

	fs = xcalloc(1, sizeof (struct filter_session));
	fs->id = reqid;
	fs->filter_name = xstrdup(filter_name);
	tree_xset(&sessions, fs->id, fs);

	log_trace(TRACE_FILTERS, "%016"PRIx64" filters session-begin", reqid);
}

void
lka_filter_end(uint64_t reqid)
{
	struct filter_session	*fs;

	fs = tree_xpop(&sessions, reqid);
	free(fs->rdns);
	free(fs->helo);
	free(fs->mail_from);
	free(fs->username);
	free(fs->lastparam);
	free(fs->filter_name);
	free(fs);
	log_trace(TRACE_FILTERS, "%016"PRIx64" filters session-end", reqid);
}

void
lka_filter_data_begin(uint64_t reqid)
{
	struct filter_session  *fs;
	int	sp[2];
	int	fd = -1;

	fs = tree_xget(&sessions, reqid);

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) == -1)
		goto end;
	io_set_nonblocking(sp[0]);
	io_set_nonblocking(sp[1]);
	fd = sp[0];
	fs->io = io_new();
	io_set_fd(fs->io, sp[1]);
	io_set_callback(fs->io, filter_session_io, fs);

end:
	m_create(p_dispatcher, IMSG_FILTER_SMTP_DATA_BEGIN, 0, 0, fd);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, fd != -1 ? 1 : 0);
	m_close(p_dispatcher);
	log_trace(TRACE_FILTERS, "%016"PRIx64" filters data-begin fd=%d", reqid, fd);
}

void
lka_filter_data_end(uint64_t reqid)
{
	struct filter_session	*fs;

	fs = tree_xget(&sessions, reqid);
	if (fs->io) {
		io_free(fs->io);
		fs->io = NULL;
	}
	log_trace(TRACE_FILTERS, "%016"PRIx64" filters data-end", reqid);
}

static void
filter_session_io(struct io *io, int evt, void *arg)
{
	struct filter_session *fs = arg;
	char *line = NULL;
	ssize_t len;

	log_trace(TRACE_IO, "filter session: %p: %s %s", fs, io_strevent(evt),
	    io_strio(io));

	switch (evt) {
	case IO_DATAIN:
	nextline:
		line = io_getline(fs->io, &len);
		/* No complete line received */
		if (line == NULL)
			return;

		filter_data(fs->id, line);

		goto nextline;
	}
}

void
lka_filter_process_response(const char *name, const char *line)
{
	uint64_t reqid;
	uint64_t token;
	char *ep = NULL;
	const char *kind = NULL;
	const char *qid = NULL;
	const char *response = NULL;
	const char *parameter = NULL;
	struct filter_session *fs;

	kind = line;

	if ((ep = strchr(kind, '|')) == NULL)
		fatalx("Missing token: %s", line);
	qid = ep+1;

	errno = 0;
	reqid = strtoull(qid, &ep, 16);
	if (qid[0] == '\0' || *ep != '|')
		fatalx("Invalid reqid: %s", line);
	if (errno == ERANGE && reqid == ULLONG_MAX)
		fatal("Invalid reqid: %s", line);

	qid = ep + 1;
	token = strtoull(qid, &ep, 16);
	if (qid[0] == '\0' || *ep != '|')
		fatalx("Invalid token: %s", line);
	if (errno == ERANGE && token == ULLONG_MAX)
		fatal("Invalid token: %s", line);

	response = ep+1;

	/* session can legitimately disappear on a resume */
	if ((fs = tree_get(&sessions, reqid)) == NULL)
		return;

	if (strncmp(kind, "filter-dataline|", 16) == 0) {
		if (fs->phase != FILTER_DATA_LINE)
			fatalx("filter-dataline out of dataline phase");
		filter_data_next(token, reqid, response);
		return;
	}
	if (fs->phase == FILTER_DATA_LINE)
		fatalx("filter-result in dataline phase");

	if ((ep = strchr(response, '|')) != NULL)
		parameter = ep + 1;

	if (strcmp(response, "proceed") == 0) {
		filter_protocol_next(token, reqid, 0);
		return;
	} else if (strcmp(response, "junk") == 0) {
		if (fs->phase == FILTER_COMMIT)
			fatalx("filter-reponse junk after DATA");
		filter_result_junk(reqid);
		return;
	} else {
		if (parameter == NULL)
			fatalx("Missing parameter: %s", line);

		if (strncmp(response, "rewrite|", 8) == 0)
			filter_result_rewrite(reqid, parameter);
		else if (strncmp(response, "reject|", 7) == 0)
			filter_result_reject(reqid, parameter);
		else if (strncmp(response, "disconnect|", 11) == 0)
			filter_result_disconnect(reqid, parameter);
		else if (strncmp(response, "report|", 7) == 0)
			filter_result_report(reqid, parameter);
		else
			fatalx("Invalid directive: %s", line);
	}
}

void
lka_filter_protocol(uint64_t reqid, enum filter_phase phase, const char *param)
{
	filter_protocol(reqid, phase, param);
}

static void
filter_protocol_internal(struct filter_session *fs, uint64_t *token, uint64_t reqid, enum filter_phase phase, const char *param)
{
	struct filter_chain	*filter_chain;
	struct filter_entry	*filter_entry;
	struct filter		*filter;
	struct timeval		 tv;
	const char		*phase_name = filter_execs[phase].phase_name;
	int			 resume = 1;

	if (!*token) {
		fs->phase = phase;
		resume = 0;
	}

	/* XXX - this sanity check requires a protocol change, stub for now */
	phase = fs->phase;
	if (fs->phase != phase)
		fatalx("misbehaving filter");

	/* based on token, identify the filter_entry we should apply  */
	filter_chain = dict_get(&filter_chains, fs->filter_name);
	filter_entry = TAILQ_FIRST(&filter_chain->chain[fs->phase]);
	if (*token) {
		TAILQ_FOREACH(filter_entry, &filter_chain->chain[fs->phase], entries)
		    if (filter_entry->id == *token)
			    break;
		if (filter_entry == NULL)
			fatalx("misbehaving filter");
		filter_entry = TAILQ_NEXT(filter_entry, entries);
	}

	/* no filter_entry, we either had none or reached end of chain */
	if (filter_entry == NULL) {
		log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, resume=%s, "
		    "action=proceed",
		    fs->id, phase_name, resume ? "y" : "n");
		filter_result_proceed(reqid);
		return;
	}

	/* process param with current filter_entry */
	*token = filter_entry->id;
	filter = dict_get(&filters, filter_entry->name);
	if (filter->proc) {
		log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
		    "resume=%s, action=deferred, filter=%s",
		    fs->id, phase_name, resume ? "y" : "n",
		    filter->name);
		filter_protocol_query(filter, filter_entry->id, reqid,
		    filter_execs[fs->phase].phase_name, param);
		return;	/* deferred response */
	}

	if (filter_execs[fs->phase].func(fs, filter, reqid, param)) {
		if (filter->config->rewrite) {
			log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
			    "resume=%s, action=rewrite, filter=%s, query=%s, response=%s",
			    fs->id, phase_name, resume ? "y" : "n",
			    filter->name,
			    param,
			    filter->config->rewrite);
			filter_result_rewrite(reqid, filter->config->rewrite);
			return;
		}
		else if (filter->config->disconnect) {
			log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
			    "resume=%s, action=disconnect, filter=%s, query=%s, response=%s",
			    fs->id, phase_name, resume ? "y" : "n",
			    filter->name,
			    param,
			    filter->config->disconnect);
			filter_result_disconnect(reqid, filter->config->disconnect);
			return;
		}
		else if (filter->config->junk) {
			log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
			    "resume=%s, action=junk, filter=%s, query=%s",
			    fs->id, phase_name, resume ? "y" : "n",
			    filter->name,
			    param);
			filter_result_junk(reqid);
			return;
		} else if (filter->config->report) {
			log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
			    "resume=%s, action=report, filter=%s, query=%s response=%s",
			    fs->id, phase_name, resume ? "y" : "n",
			    filter->name,
			    param, filter->config->report);

			gettimeofday(&tv, NULL);
			lka_report_filter_report(fs->id, filter->name, 1,
			    "smtp-in", &tv, filter->config->report);
		} else if (filter->config->bypass) {
			log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
			    "resume=%s, action=bypass, filter=%s, query=%s",
			    fs->id, phase_name, resume ? "y" : "n",
			    filter->name,
			    param);
			filter_result_proceed(reqid);
			return;
		} else {
			log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
			    "resume=%s, action=reject, filter=%s, query=%s, response=%s",
			    fs->id, phase_name, resume ? "y" : "n",
			    filter->name,
			    param,
			    filter->config->reject);
			filter_result_reject(reqid, filter->config->reject);
			return;
		}
	}

	log_trace(TRACE_FILTERS, "%016"PRIx64" filters protocol phase=%s, "
	    "resume=%s, action=proceed, filter=%s, query=%s",
	    fs->id, phase_name, resume ? "y" : "n",
	    filter->name,
	    param);

	/* filter_entry resulted in proceed, try next filter */
	filter_protocol_internal(fs, token, reqid, phase, param);
	return;
}

static void
filter_data_internal(struct filter_session *fs, uint64_t token, uint64_t reqid, const char *line)
{
	struct filter_chain	*filter_chain;
	struct filter_entry	*filter_entry;
	struct filter		*filter;

	if (!token)
		fs->phase = FILTER_DATA_LINE;
	if (fs->phase != FILTER_DATA_LINE)
		fatalx("misbehaving filter");

	/* based on token, identify the filter_entry we should apply  */
	filter_chain = dict_get(&filter_chains, fs->filter_name);
	filter_entry = TAILQ_FIRST(&filter_chain->chain[fs->phase]);
	if (token) {
		TAILQ_FOREACH(filter_entry, &filter_chain->chain[fs->phase], entries)
		    if (filter_entry->id == token)
			    break;
		if (filter_entry == NULL)
			fatalx("misbehaving filter");
		filter_entry = TAILQ_NEXT(filter_entry, entries);
	}

	/* no filter_entry, we either had none or reached end of chain */
	if (filter_entry == NULL) {
		io_printf(fs->io, "%s\n", line);
		return;
	}

	/* pass data to the filter */
	filter = dict_get(&filters, filter_entry->name);
	filter_data_query(filter, filter_entry->id, reqid, line);
}

static void
filter_protocol(uint64_t reqid, enum filter_phase phase, const char *param)
{
	struct filter_session  *fs;
	uint64_t		token = 0;
	char		       *nparam = NULL;
	
	fs = tree_xget(&sessions, reqid);

	switch (phase) {
	case FILTER_HELO:
	case FILTER_EHLO:
		free(fs->helo);
		fs->helo = xstrdup(param);
		break;
	case FILTER_MAIL_FROM:
		free(fs->mail_from);
		fs->mail_from = xstrdup(param + 1);
		*strchr(fs->mail_from, '>') = '\0';
		param = fs->mail_from;

		break;
	case FILTER_RCPT_TO:
		nparam = xstrdup(param + 1);
		*strchr(nparam, '>') = '\0';
		param = nparam;
		break;
	case FILTER_STARTTLS:
		/* TBD */
		break;
	default:
		break;
	}

	free(fs->lastparam);
	fs->lastparam = xstrdup(param);

	filter_protocol_internal(fs, &token, reqid, phase, param);
	if (nparam)
		free(nparam);
}

static void
filter_protocol_next(uint64_t token, uint64_t reqid, enum filter_phase phase)
{
	struct filter_session  *fs;

	/* session can legitimately disappear on a resume */
	if ((fs = tree_get(&sessions, reqid)) == NULL)
		return;

	filter_protocol_internal(fs, &token, reqid, phase, fs->lastparam);
}

static void
filter_data(uint64_t reqid, const char *line)
{
	struct filter_session  *fs;

	fs = tree_xget(&sessions, reqid);

	filter_data_internal(fs, 0, reqid, line);
}

static void
filter_data_next(uint64_t token, uint64_t reqid, const char *line)
{
	struct filter_session  *fs;

	/* session can legitimately disappear on a resume */
	if ((fs = tree_get(&sessions, reqid)) == NULL)
		return;

	filter_data_internal(fs, token, reqid, line);
}

static void
filter_protocol_query(struct filter *filter, uint64_t token, uint64_t reqid, const char *phase, const char *param)
{
	int	n;
	struct filter_session	*fs;
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	
	fs = tree_xget(&sessions, reqid);
	if (strcmp(phase, "connect") == 0)
		n = io_printf(lka_proc_get_io(filter->proc),
		    "filter|%s|%lld.%06ld|smtp-in|%s|%016"PRIx64"|%016"PRIx64"|%s|%s\n",
		    PROTOCOL_VERSION,
		    (long long)tv.tv_sec, (long)tv.tv_usec,
		    phase, reqid, token, fs->rdns, param);
	else
		n = io_printf(lka_proc_get_io(filter->proc),
		    "filter|%s|%lld.%06ld|smtp-in|%s|%016"PRIx64"|%016"PRIx64"|%s\n",
		    PROTOCOL_VERSION,
		    (long long)tv.tv_sec, (long)tv.tv_usec,
		    phase, reqid, token, param);
	if (n == -1)
		fatalx("failed to write to processor");
}

static void
filter_data_query(struct filter *filter, uint64_t token, uint64_t reqid, const char *line)
{
	int	n;
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	n = io_printf(lka_proc_get_io(filter->proc),
	    "filter|%s|%lld.%06ld|smtp-in|data-line|"
	    "%016"PRIx64"|%016"PRIx64"|%s\n",
	    PROTOCOL_VERSION,
	    (long long)tv.tv_sec, (long)tv.tv_usec,
	    reqid, token, line);
	if (n == -1)
		fatalx("failed to write to processor");
}

static void
filter_result_proceed(uint64_t reqid)
{
	m_create(p_dispatcher, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, FILTER_PROCEED);
	m_close(p_dispatcher);
}

static void
filter_result_report(uint64_t reqid, const char *param)
{
	m_create(p_dispatcher, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, FILTER_REPORT);
	m_add_string(p_dispatcher, param);
	m_close(p_dispatcher);
}

static void
filter_result_junk(uint64_t reqid)
{
	m_create(p_dispatcher, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, FILTER_JUNK);
	m_close(p_dispatcher);
}

static void
filter_result_rewrite(uint64_t reqid, const char *param)
{
	m_create(p_dispatcher, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, FILTER_REWRITE);
	m_add_string(p_dispatcher, param);
	m_close(p_dispatcher);
}

static void
filter_result_reject(uint64_t reqid, const char *message)
{
	m_create(p_dispatcher, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, FILTER_REJECT);
	m_add_string(p_dispatcher, message);
	m_close(p_dispatcher);
}

static void
filter_result_disconnect(uint64_t reqid, const char *message)
{
	m_create(p_dispatcher, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_dispatcher, reqid);
	m_add_int(p_dispatcher, FILTER_DISCONNECT);
	m_add_string(p_dispatcher, message);
	m_close(p_dispatcher);
}


/* below is code for builtin filters */

static int
filter_check_rdns_table(struct filter *filter, enum table_service kind, const char *key)
{
	int	ret = 0;

	if (filter->config->rdns_table == NULL)
		return 0;
	
	if (table_match(filter->config->rdns_table, kind, key) > 0)
		ret = 1;

	return filter->config->not_rdns_table < 0 ? !ret : ret;
}

static int
filter_check_rdns_regex(struct filter *filter, const char *key)
{
	int	ret = 0;

	if (filter->config->rdns_regex == NULL)
		return 0;

	if (table_match(filter->config->rdns_regex, K_REGEX, key) > 0)
		ret = 1;
	return filter->config->not_rdns_regex < 0 ? !ret : ret;
}

static int
filter_check_src_table(struct filter *filter, enum table_service kind, const char *key)
{
	int	ret = 0;

	if (filter->config->src_table == NULL)
		return 0;

	if (table_match(filter->config->src_table, kind, key) > 0)
		ret = 1;
	return filter->config->not_src_table < 0 ? !ret : ret;
}

static int
filter_check_src_regex(struct filter *filter, const char *key)
{
	int	ret = 0;

	if (filter->config->src_regex == NULL)
		return 0;

	if (table_match(filter->config->src_regex, K_REGEX, key) > 0)
		ret = 1;
	return filter->config->not_src_regex < 0 ? !ret : ret;
}

static int
filter_check_helo_table(struct filter *filter, enum table_service kind, const char *key)
{
	int	ret = 0;

	if (filter->config->helo_table == NULL)
		return 0;

	if (table_match(filter->config->helo_table, kind, key) > 0)
		ret = 1;
	return filter->config->not_helo_table < 0 ? !ret : ret;
}

static int
filter_check_helo_regex(struct filter *filter, const char *key)
{
	int	ret = 0;

	if (filter->config->helo_regex == NULL)
		return 0;

	if (table_match(filter->config->helo_regex, K_REGEX, key) > 0)
		ret = 1;
	return filter->config->not_helo_regex < 0 ? !ret : ret;
}

static int
filter_check_auth(struct filter *filter, const char *username)
{
	int ret = 0;

	if (!filter->config->auth)
		return 0;

	ret = username ? 1 : 0;

	return filter->config->not_auth < 0 ? !ret : ret;
}

static int
filter_check_auth_table(struct filter *filter, enum table_service kind, const char *key)
{
	int	ret = 0;

	if (filter->config->auth_table == NULL)
		return 0;
	
	if (key && table_match(filter->config->auth_table, kind, key) > 0)
		ret = 1;

	return filter->config->not_auth_table < 0 ? !ret : ret;
}

static int
filter_check_auth_regex(struct filter *filter, const char *key)
{
	int	ret = 0;

	if (filter->config->auth_regex == NULL)
		return 0;

	if (key && table_match(filter->config->auth_regex, K_REGEX, key) > 0)
		ret = 1;
	return filter->config->not_auth_regex < 0 ? !ret : ret;
}


static int
filter_check_mail_from_table(struct filter *filter, enum table_service kind, const char *key)
{
	int	ret = 0;

	if (filter->config->mail_from_table == NULL)
		return 0;

	if (table_match(filter->config->mail_from_table, kind, key) > 0)
		ret = 1;
	return filter->config->not_mail_from_table < 0 ? !ret : ret;
}

static int
filter_check_mail_from_regex(struct filter *filter, const char *key)
{
	int	ret = 0;

	if (filter->config->mail_from_regex == NULL)
		return 0;

	if (table_match(filter->config->mail_from_regex, K_REGEX, key) > 0)
		ret = 1;
	return filter->config->not_mail_from_regex < 0 ? !ret : ret;
}

static int
filter_check_rcpt_to_table(struct filter *filter, enum table_service kind, const char *key)
{
	int	ret = 0;

	if (filter->config->rcpt_to_table == NULL)
		return 0;

	if (table_match(filter->config->rcpt_to_table, kind, key) > 0)
		ret = 1;
	return filter->config->not_rcpt_to_table < 0 ? !ret : ret;
}

static int
filter_check_rcpt_to_regex(struct filter *filter, const char *key)
{
	int	ret = 0;

	if (filter->config->rcpt_to_regex == NULL)
		return 0;

	if (table_match(filter->config->rcpt_to_regex, K_REGEX, key) > 0)
		ret = 1;
	return filter->config->not_rcpt_to_regex < 0 ? !ret : ret;
}

static int
filter_check_fcrdns(struct filter *filter, int fcrdns)
{
	int	ret = 0;

	if (!filter->config->fcrdns)
		return 0;

	ret = fcrdns == 1;
	return filter->config->not_fcrdns < 0 ? !ret : ret;
}

static int
filter_check_rdns(struct filter *filter, const char *hostname)
{
	int	ret = 0;
	struct netaddr	netaddr;

	if (!filter->config->rdns)
		return 0;

	/* this is a hack until smtp session properly deals with lack of rdns */
	ret = strcmp("<unknown>", hostname);
	if (ret == 0)
		return filter->config->not_rdns < 0 ? !ret : ret;

	/* if text_to_netaddress succeeds,
	 * we don't have an rDNS so the filter should match
	 */
	ret = !text_to_netaddr(&netaddr, hostname);
	return filter->config->not_rdns < 0 ? !ret : ret;
}

static int
filter_builtins_notimpl(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return 0;
}

static int
filter_builtins_global(struct filter_session *fs, struct filter *filter, uint64_t reqid)
{
	return filter_check_fcrdns(filter, fs->fcrdns) ||
	    filter_check_rdns(filter, fs->rdns) ||
	    filter_check_rdns_table(filter, K_DOMAIN, fs->rdns) ||
	    filter_check_rdns_regex(filter, fs->rdns) ||
	    filter_check_src_table(filter, K_NETADDR, ss_to_text(&fs->ss_src)) ||
	    filter_check_src_regex(filter, ss_to_text(&fs->ss_src)) ||
	    filter_check_helo_table(filter, K_DOMAIN, fs->helo) ||
	    filter_check_helo_regex(filter, fs->helo) ||
	    filter_check_auth(filter, fs->username) ||
	    filter_check_auth_table(filter, K_STRING, fs->username) ||
	    filter_check_auth_table(filter, K_CREDENTIALS, fs->username) ||
	    filter_check_auth_regex(filter, fs->username) ||
	    filter_check_mail_from_table(filter, K_MAILADDR, fs->mail_from) ||
	    filter_check_mail_from_regex(filter, fs->mail_from);
}

static int
filter_builtins_connect(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return filter_builtins_global(fs, filter, reqid);
}

static int
filter_builtins_helo(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return filter_builtins_global(fs, filter, reqid);
}

static int
filter_builtins_mail_from(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return filter_builtins_global(fs, filter, reqid);
}

static int
filter_builtins_rcpt_to(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return filter_builtins_global(fs, filter, reqid) ||
	    filter_check_rcpt_to_table(filter, K_MAILADDR, param) ||
	    filter_check_rcpt_to_regex(filter, param);
}

static int
filter_builtins_data(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return filter_builtins_global(fs, filter, reqid);
}

static int
filter_builtins_commit(struct filter_session *fs, struct filter *filter, uint64_t reqid, const char *param)
{
	return filter_builtins_global(fs, filter, reqid);
}

static void
report_smtp_broadcast(uint64_t, const char *, struct timeval *, const char *,
    const char *, ...) __attribute__((__format__ (printf, 5, 6)));

void
lka_report_init(void)
{
	struct reporters	*tailq;
	size_t			 i;

	dict_init(&report_smtp_in);
	dict_init(&report_smtp_out);

	for (i = 0; i < nitems(smtp_events); ++i) {
		tailq = xcalloc(1, sizeof (struct reporters));
		TAILQ_INIT(tailq);
		dict_xset(&report_smtp_in, smtp_events[i].event, tailq);

		tailq = xcalloc(1, sizeof (struct reporters));
		TAILQ_INIT(tailq);
		dict_xset(&report_smtp_out, smtp_events[i].event, tailq);
	}
}

void
lka_report_register_hook(const char *name, const char *hook)
{
	struct dict	*subsystem;
	struct reporter_proc	*rp;
	struct reporters	*tailq;
	void *iter;
	size_t	i;

	if (strncmp(hook, "smtp-in|", 8) == 0) {
		subsystem = &report_smtp_in;
		hook += 8;
	}
	else if (strncmp(hook, "smtp-out|", 9) == 0) {
		subsystem = &report_smtp_out;
		hook += 9;
	}
	else
		fatalx("Invalid message direction: %s", hook);

	if (strcmp(hook, "*") == 0) {
		iter = NULL;
		while (dict_iter(subsystem, &iter, NULL, (void **)&tailq)) {
			rp = xcalloc(1, sizeof *rp);
			rp->name = xstrdup(name);
			TAILQ_INSERT_TAIL(tailq, rp, entries);
		}
		return;
	}

	for (i = 0; i < nitems(smtp_events); i++)
		if (strcmp(hook, smtp_events[i].event) == 0)
			break;
	if (i == nitems(smtp_events))
		fatalx("Unrecognized report name: %s", hook);

	tailq = dict_get(subsystem, hook);
	rp = xcalloc(1, sizeof *rp);
	rp->name = xstrdup(name);
	TAILQ_INSERT_TAIL(tailq, rp, entries);
}

static void
report_smtp_broadcast(uint64_t reqid, const char *direction, struct timeval *tv, const char *event,
    const char *format, ...)
{
	va_list		ap;
	struct dict	*d;
	struct reporters	*tailq;
	struct reporter_proc	*rp;

	if (strcmp("smtp-in", direction) == 0)
		d = &report_smtp_in;

	else if (strcmp("smtp-out", direction) == 0)
		d = &report_smtp_out;

	else
		fatalx("unexpected direction: %s", direction);

	tailq = dict_xget(d, event);
	TAILQ_FOREACH(rp, tailq, entries) {
		if (!lka_filter_proc_in_session(reqid, rp->name))
			continue;

		va_start(ap, format);
		if (io_printf(lka_proc_get_io(rp->name),
		    "report|%s|%lld.%06ld|%s|%s|%016"PRIx64"%s",
		    PROTOCOL_VERSION, (long long)tv->tv_sec, (long)tv->tv_usec,
		    direction, event, reqid,
		    format[0] != '\n' ? "|" : "") == -1 ||
		    io_vprintf(lka_proc_get_io(rp->name), format, ap) == -1)
			fatalx("failed to write to processor");
		va_end(ap);
	}
}

void
lka_report_smtp_link_connect(const char *direction, struct timeval *tv, uint64_t reqid, const char *rdns,
    int fcrdns,
    const struct sockaddr_storage *ss_src,
    const struct sockaddr_storage *ss_dest)
{
	struct filter_session *fs;
	char	src[NI_MAXHOST + 5];
	char	dest[NI_MAXHOST + 5];
	uint16_t	src_port = 0;
	uint16_t	dest_port = 0;
	const char     *fcrdns_str;

	if (ss_src->ss_family == AF_INET)
		src_port = ntohs(((const struct sockaddr_in *)ss_src)->sin_port);
	else if (ss_src->ss_family == AF_INET6)
		src_port = ntohs(((const struct sockaddr_in6 *)ss_src)->sin6_port);

	if (ss_dest->ss_family == AF_INET)
		dest_port = ntohs(((const struct sockaddr_in *)ss_dest)->sin_port);
	else if (ss_dest->ss_family == AF_INET6)
		dest_port = ntohs(((const struct sockaddr_in6 *)ss_dest)->sin6_port);

	if (strcmp(ss_to_text(ss_src), "local") == 0) {
		(void)snprintf(src, sizeof src, "unix:%s", SMTPD_SOCKET);
		(void)snprintf(dest, sizeof dest, "unix:%s", SMTPD_SOCKET);
	} else {
		(void)snprintf(src, sizeof src, "%s:%d", ss_to_text(ss_src), src_port);
		(void)snprintf(dest, sizeof dest, "%s:%d", ss_to_text(ss_dest), dest_port);
	}

	switch (fcrdns) {
	case 1:
		fcrdns_str = "pass";
		break;
	case 0:
		fcrdns_str = "fail";
		break;
	default:
		fcrdns_str = "error";
		break;
	}

	fs = tree_xget(&sessions, reqid);
	fs->rdns = xstrdup(rdns);
	fs->fcrdns = fcrdns;
	fs->ss_src = *ss_src;
	fs->ss_dest = *ss_dest;

	report_smtp_broadcast(reqid, direction, tv, "link-connect",
	    "%s|%s|%s|%s\n", rdns, fcrdns_str, src, dest);
}

void
lka_report_smtp_link_disconnect(const char *direction, struct timeval *tv, uint64_t reqid)
{
	report_smtp_broadcast(reqid, direction, tv, "link-disconnect", "\n");
}

void
lka_report_smtp_link_greeting(const char *direction, uint64_t reqid,
    struct timeval *tv, const char *domain)
{
	report_smtp_broadcast(reqid, direction, tv, "link-greeting", "%s\n",
	    domain);
}

void
lka_report_smtp_link_auth(const char *direction, struct timeval *tv, uint64_t reqid,
    const char *username, const char *result)
{
	struct filter_session *fs;

	if (strcmp(result, "pass") == 0) {
		fs = tree_xget(&sessions, reqid);
		fs->username = xstrdup(username);
	}
	report_smtp_broadcast(reqid, direction, tv, "link-auth", "%s|%s\n",
	    result, username);
}

void
lka_report_smtp_link_identify(const char *direction, struct timeval *tv,
    uint64_t reqid, const char *method, const char *heloname)
{
	report_smtp_broadcast(reqid, direction, tv, "link-identify", "%s|%s\n",
	    method, heloname);
}

void
lka_report_smtp_link_tls(const char *direction, struct timeval *tv, uint64_t reqid, const char *ciphers)
{
	report_smtp_broadcast(reqid, direction, tv, "link-tls", "%s\n",
	    ciphers);
}

void
lka_report_smtp_tx_reset(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid)
{
	report_smtp_broadcast(reqid, direction, tv, "tx-reset", "%08x\n",
	    msgid);
}

void
lka_report_smtp_tx_begin(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid)
{
	report_smtp_broadcast(reqid, direction, tv, "tx-begin", "%08x\n",
	    msgid);
}

void
lka_report_smtp_tx_mail(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid, const char *address, int ok)
{
	const char *result;

	switch (ok) {
	case 1:
		result = "ok";
		break;
	case 0:
		result = "permfail";
		break;
	default:
		result = "tempfail";
		break;
	}
	report_smtp_broadcast(reqid, direction, tv, "tx-mail", "%08x|%s|%s\n",
	    msgid, result, address);
}

void
lka_report_smtp_tx_rcpt(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid, const char *address, int ok)
{
	const char *result;

	switch (ok) {
	case 1:
		result = "ok";
		break;
	case 0:
		result = "permfail";
		break;
	default:
		result = "tempfail";
		break;
	}
	report_smtp_broadcast(reqid, direction, tv, "tx-rcpt", "%08x|%s|%s\n",
	    msgid, result, address);
}

void
lka_report_smtp_tx_envelope(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid, uint64_t evpid)
{
	report_smtp_broadcast(reqid, direction, tv, "tx-envelope",
	    "%08x|%016"PRIx64"\n", msgid, evpid);
}

void
lka_report_smtp_tx_data(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid, int ok)
{
	const char *result;

	switch (ok) {
	case 1:
		result = "ok";
		break;
	case 0:
		result = "permfail";
		break;
	default:
		result = "tempfail";
		break;
	}
	report_smtp_broadcast(reqid, direction, tv, "tx-data", "%08x|%s\n",
	    msgid, result);
}

void
lka_report_smtp_tx_commit(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid, size_t msgsz)
{
	report_smtp_broadcast(reqid, direction, tv, "tx-commit", "%08x|%zd\n",
	    msgid, msgsz);
}

void
lka_report_smtp_tx_rollback(const char *direction, struct timeval *tv, uint64_t reqid, uint32_t msgid)
{
	report_smtp_broadcast(reqid, direction, tv, "tx-rollback", "%08x\n",
	    msgid);
}

void
lka_report_smtp_protocol_client(const char *direction, struct timeval *tv, uint64_t reqid, const char *command)
{
	report_smtp_broadcast(reqid, direction, tv, "protocol-client", "%s\n",
	    command);
}

void
lka_report_smtp_protocol_server(const char *direction, struct timeval *tv, uint64_t reqid, const char *response)
{
	report_smtp_broadcast(reqid, direction, tv, "protocol-server", "%s\n",
	    response);
}

void
lka_report_smtp_filter_response(const char *direction, struct timeval *tv, uint64_t reqid,
    int phase, int response, const char *param)
{
	const char *phase_name;
	const char *response_name;

	switch (phase) {
	case FILTER_CONNECT:
		phase_name = "connected";
		break;
	case FILTER_HELO:
		phase_name = "helo";
		break;
	case FILTER_EHLO:
		phase_name = "ehlo";
		break;
	case FILTER_STARTTLS:
		phase_name = "tls";
		break;
	case FILTER_AUTH:
		phase_name = "auth";
		break;
	case FILTER_MAIL_FROM:
		phase_name = "mail-from";
		break;
	case FILTER_RCPT_TO:
		phase_name = "rcpt-to";
		break;
	case FILTER_DATA:
		phase_name = "data";
		break;
	case FILTER_DATA_LINE:
		phase_name = "data-line";
		break;
	case FILTER_RSET:
		phase_name = "rset";
		break;
	case FILTER_QUIT:
		phase_name = "quit";
		break;
	case FILTER_NOOP:
		phase_name = "noop";
		break;
	case FILTER_HELP:
		phase_name = "help";
		break;
	case FILTER_WIZ:
		phase_name = "wiz";
		break;
	case FILTER_COMMIT:
		phase_name = "commit";
		break;
	default:
		phase_name = "";
	}

	switch (response) {
	case FILTER_PROCEED:
		response_name = "proceed";
		break;
	case FILTER_REPORT:
		response_name = "report";
		break;
	case FILTER_JUNK:
		response_name = "junk";
		break;
	case FILTER_REWRITE:
		response_name = "rewrite";
		break;
	case FILTER_REJECT:
		response_name = "reject";
		break;
	case FILTER_DISCONNECT:
		response_name = "disconnect";
		break;
	default:
		response_name = "";
	}

	report_smtp_broadcast(reqid, direction, tv, "filter-response",
	    "%s|%s%s%s\n", phase_name, response_name, param ? "|" : "",
	    param ? param : "");
}

void
lka_report_smtp_timeout(const char *direction, struct timeval *tv, uint64_t reqid)
{
	report_smtp_broadcast(reqid, direction, tv, "timeout", "\n");
}

void
lka_report_filter_report(uint64_t reqid, const char *name, int builtin,
    const char *direction, struct timeval *tv, const char *message)
{
	report_smtp_broadcast(reqid, direction, tv, "filter-report",
	    "%s|%s|%s\n", builtin ? "builtin" : "proc",
	    name, message);
}

void
lka_report_proc(const char *name, const char *line)
{
	char buffer[LINE_MAX];
	struct timeval tv;
	char *ep, *sp, *direction;
	uint64_t reqid;

	if (strlcpy(buffer, line + 7, sizeof(buffer)) >= sizeof(buffer))
		fatalx("Invalid report: line too long: %s", line);

	errno = 0;
	tv.tv_sec = strtoll(buffer, &ep, 10);
	if (ep[0] != '.' || errno != 0)
		fatalx("Invalid report: invalid time: %s", line);
	sp = ep + 1;
	tv.tv_usec = strtol(sp, &ep, 10);
	if (ep[0] != '|' || errno != 0)
		fatalx("Invalid report: invalid time: %s", line);
	if (ep - sp != 6)
		fatalx("Invalid report: invalid time: %s", line);

	direction = ep + 1;
	if (strncmp(direction, "smtp-in|", 8) == 0) {
		direction[7] = '\0';
		direction += 7;
#if 0
	} else if (strncmp(direction, "smtp-out|", 9) == 0) {
		direction[8] = '\0';
		direction += 8;
#endif
	} else
		fatalx("Invalid report: invalid direction: %s", line);

	reqid = strtoull(sp, &ep, 16);
	if (ep[0] != '|' || errno != 0)
		fatalx("Invalid report: invalid reqid: %s", line);
	sp = ep + 1;

	lka_report_filter_report(reqid, name, 0, direction, &tv, sp);
}
