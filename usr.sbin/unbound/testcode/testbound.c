/*
 * testcode/testbound.c - test program for unbound.
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
 *
 */
/**
 * \file
 * Exits with code 1 on a failure. 0 if all unit tests are successful.
 */

#include "config.h"
#ifdef HAVE_TIME_H
#  include <time.h>
#endif
#include <ctype.h>
#include "testcode/testpkts.h"
#include "testcode/replay.h"
#include "testcode/fake_event.h"
#include "daemon/remote.h"
#include "libunbound/worker.h"
#include "daemon/worker.h"
#include "util/config_file.h"
#include "sldns/keyraw.h"
#ifdef UB_ON_WINDOWS
#include "winrc/win_svc.h"
#endif

/** signal that this is a testbound compile */
#define unbound_testbound 1
/** renamed main routine */
int daemon_main(int argc, char* argv[]);
/** 
 * include the main program from the unbound daemon.
 * rename main to daemon_main to call it
 */
#define main daemon_main
#include "daemon/unbound.c"
#undef main

/** maximum line length for lines in the replay file. */
#define MAX_LINE_LEN 1024
/** config files (removed at exit) */
static struct config_strlist* cfgfiles = NULL;

/** give commandline usage for testbound. */
static void
testbound_usage(void)
{
	printf("usage: testbound [options]\n");
	printf("\ttest the unbound daemon.\n");
	printf("-h      this help\n");
	printf("-p file	playback text file\n");
	printf("-1 	detect SHA1 support (exit code 0 or 1)\n");
	printf("-2 	detect SHA256 support (exit code 0 or 1)\n");
	printf("-g 	detect GOST support (exit code 0 or 1)\n");
	printf("-e 	detect ECDSA support (exit code 0 or 1)\n");
	printf("-c 	detect CLIENT_SUBNET support (exit code 0 or 1)\n");
	printf("-i 	detect IPSECMOD support (exit code 0 or 1)\n");
	printf("-s 	testbound self-test - unit test of testbound parts.\n");
	printf("-o str  unbound commandline options separated by spaces.\n");
	printf("Version %s\n", PACKAGE_VERSION);
	printf("BSD licensed, see LICENSE file in source package.\n");
	printf("Report bugs to %s.\n", PACKAGE_BUGREPORT);
}

/** Max number of arguments to pass to unbound. */
#define MAXARG 100

/** 
 * Add options from string to passed argc. splits on whitespace.
 * @param args: the option argument, "-v -p 12345" or so.
 * @param pass_argc: ptr to the argc for unbound. Modified.
 * @param pass_argv: the argv to pass to unbound. Modified.
 */
static void
add_opts(const char* args, int* pass_argc, char* pass_argv[])
{
	const char *p = args, *np;
	size_t len;
	while(p && isspace((unsigned char)*p)) 
		p++;
	while(p && *p) {
		/* find location of next string and length of this one */
		if((np = strchr(p, ' ')))
			len = (size_t)(np-p);
		else	len = strlen(p);
		/* allocate and copy option */
		if(*pass_argc >= MAXARG-1)
			fatal_exit("too many arguments: '%s'", p);
		pass_argv[*pass_argc] = (char*)malloc(len+1);
		if(!pass_argv[*pass_argc])
			fatal_exit("add_opts: out of memory");
		memcpy(pass_argv[*pass_argc], p, len);
		pass_argv[*pass_argc][len] = 0;
		(*pass_argc)++;
		/* go to next option */
	        p = np;
		while(p && isspace((unsigned char)*p)) 
			p++;
	}
}

/** pretty print commandline for unbound in this test */
static void
echo_cmdline(int argc, char* argv[])
{
	int i;
	fprintf(stderr, "testbound is starting:");
	for(i=0; i<argc; i++) {
		fprintf(stderr, " [%s]", argv[i]);
	}
	fprintf(stderr, "\n");
}

/** spool temp file name */
static void
spool_temp_file_name(int* lineno, FILE* cfg, char* id)
{
	char line[MAX_LINE_LEN];
	/* find filename for new file */
	while(isspace((unsigned char)*id))
		id++;
	if(*id == '\0') 
		fatal_exit("TEMPFILE_NAME must have id, line %d", *lineno);
	strip_end_white(id);
	fake_temp_file("_temp_", id, line, sizeof(line));
	fprintf(cfg, "\"%s\"\n", line);
}

/** spool temp file */
static void
spool_temp_file(FILE* in, int* lineno, char* id)
{
	char line[MAX_LINE_LEN];
	char* parse;
	FILE* spool;
	/* find filename for new file */
	while(isspace((unsigned char)*id))
		id++;
	if(*id == '\0') 
		fatal_exit("TEMPFILE_CONTENTS must have id, line %d", *lineno);
	strip_end_white(id);
	fake_temp_file("_temp_", id, line, sizeof(line));
	/* open file and spool to it */
	spool = fopen(line, "w");
	if(!spool) fatal_exit("could not open %s: %s", line, strerror(errno));
	fprintf(stderr, "testbound is spooling temp file: %s\n", line);
	if(!cfg_strlist_insert(&cfgfiles, strdup(line))) 
		fatal_exit("out of memory");
	line[sizeof(line)-1] = 0;
	while(fgets(line, MAX_LINE_LEN-1, in)) {
		parse = line;
		(*lineno)++;
		while(isspace((unsigned char)*parse))
			parse++;
		if(strncmp(parse, "$INCLUDE_TEMPFILE", 17) == 0) {
			char l2[MAX_LINE_LEN-30]; /* -30 makes it fit with
				a preceding $INCLUDE in the buf line[] */
			char* tid = parse+17;
			while(isspace((unsigned char)*tid))
				tid++;
			strip_end_white(tid);
			fake_temp_file("_temp_", tid, l2, sizeof(l2));
			snprintf(line, sizeof(line), "$INCLUDE %s\n", l2);
		}
		if(strncmp(parse, "TEMPFILE_END", 12) == 0) {
			fclose(spool);
			return;
		}
		fputs(line, spool);
	}
	fatal_exit("no TEMPFILE_END in input file");
}

/** spool autotrust file */
static void
spool_auto_file(FILE* in, int* lineno, FILE* cfg, char* id)
{
	char line[MAX_LINE_LEN];
	char* parse;
	FILE* spool;
	/* find filename for new file */
	while(isspace((unsigned char)*id))
		id++;
	if(*id == '\0') 
		fatal_exit("AUTROTRUST_FILE must have id, line %d", *lineno);
	strip_end_white(id);
	fake_temp_file("_auto_", id, line, sizeof(line));
	/* add option for the file */
	fprintf(cfg, "server:	auto-trust-anchor-file: \"%s\"\n", line);
	/* open file and spool to it */
	spool = fopen(line, "w");
	if(!spool) fatal_exit("could not open %s: %s", line, strerror(errno));
	fprintf(stderr, "testbound is spooling key file: %s\n", line);
	if(!cfg_strlist_insert(&cfgfiles, strdup(line))) 
		fatal_exit("out of memory");
	line[sizeof(line)-1] = 0;
	while(fgets(line, MAX_LINE_LEN-1, in)) {
		parse = line;
		(*lineno)++;
		while(isspace((unsigned char)*parse))
			parse++;
		if(strncmp(parse, "AUTOTRUST_END", 13) == 0) {
			fclose(spool);
			return;
		}
		fputs(line, spool);
	}
	fatal_exit("no AUTOTRUST_END in input file");
}

/** process config elements */
static void
setup_config(FILE* in, int* lineno, int* pass_argc, char* pass_argv[])
{
	char configfile[MAX_LINE_LEN];
	char line[MAX_LINE_LEN];
	char* parse;
	FILE* cfg;
	fake_temp_file("_cfg", "", configfile, sizeof(configfile));
	add_opts("-c", pass_argc, pass_argv);
	add_opts(configfile, pass_argc, pass_argv);
	cfg = fopen(configfile, "w");
	if(!cfg) fatal_exit("could not open %s: %s", 
			configfile, strerror(errno));
	if(!cfg_strlist_insert(&cfgfiles, strdup(configfile))) 
		fatal_exit("out of memory");
	line[sizeof(line)-1] = 0;
	/* some basic settings to not pollute the host system */
	fprintf(cfg, "server:	use-syslog: no\n");
	fprintf(cfg, "		directory: \"\"\n");
	fprintf(cfg, "		chroot: \"\"\n");
	fprintf(cfg, "		username: \"\"\n");
	fprintf(cfg, "		pidfile: \"\"\n");
	fprintf(cfg, "		val-log-level: 2\n");
	fprintf(cfg, "		log-servfail: yes\n");
	fprintf(cfg, "remote-control:	control-enable: no\n");
	while(fgets(line, MAX_LINE_LEN-1, in)) {
		parse = line;
		(*lineno)++;
		while(isspace((unsigned char)*parse))
			parse++;
		if(!*parse || parse[0] == ';')
			continue;
		if(strncmp(parse, "COMMANDLINE", 11) == 0) {
			parse[strlen(parse)-1] = 0; /* strip off \n */
			add_opts(parse+11, pass_argc, pass_argv);
			continue;
		}
		if(strncmp(parse, "AUTOTRUST_FILE", 14) == 0) {
			spool_auto_file(in, lineno, cfg, parse+14);
			continue;
		}
		if(strncmp(parse, "TEMPFILE_NAME", 13) == 0) {
			spool_temp_file_name(lineno, cfg, parse+13);
			continue;
		}
		if(strncmp(parse, "TEMPFILE_CONTENTS", 17) == 0) {
			spool_temp_file(in, lineno, parse+17);
			continue;
		}
		if(strncmp(parse, "CONFIG_END", 10) == 0) {
			fclose(cfg);
			return;
		}
		fputs(line, cfg);
	}
	fatal_exit("No CONFIG_END in input file");

}

/** read playback file */
static struct replay_scenario* 
setup_playback(const char* filename, int* pass_argc, char* pass_argv[])
{
	struct replay_scenario* scen = NULL;
	int lineno = 0;

	if(filename) {
		FILE *in = fopen(filename, "rb");
		if(!in) {
			perror(filename);
			exit(1);
		}
		setup_config(in, &lineno, pass_argc, pass_argv);
		scen = replay_scenario_read(in, filename, &lineno);
		fclose(in);
		if(!scen)
			fatal_exit("Could not read: %s", filename);
	}
	else fatal_exit("need a playback file (-p)");
	log_info("Scenario: %s", scen->title);
	return scen;
}

/** remove config file at exit */
static void remove_configfile(void)
{
	struct config_strlist* p;
	for(p=cfgfiles; p; p=p->next)
		unlink(p->str);
	config_delstrlist(cfgfiles);
	cfgfiles = NULL;
}

/**
 * Main fake event test program. Setup, teardown and report errors.
 * @param argc: arg count.
 * @param argv: array of commandline arguments.
 * @return program failure if test fails.
 */
int 
main(int argc, char* argv[])
{
	int c, res;
	int pass_argc = 0;
	char* pass_argv[MAXARG];
	char* playback_file = NULL;
	int init_optind = optind;
	char* init_optarg = optarg;
	struct replay_scenario* scen = NULL;

	/* we do not want the test to depend on the timezone */
	(void)putenv("TZ=UTC");
	memset(pass_argv, 0, sizeof(pass_argv));
#ifdef HAVE_SYSTEMD
	/* we do not want the test to use systemd daemon startup notification*/
	(void)unsetenv("NOTIFY_SOCKET");
#endif /* HAVE_SYSTEMD */

	checklock_start();
	log_init(NULL, 0, NULL);
	/* determine commandline options for the daemon */
	pass_argc = 1;
	pass_argv[0] = "unbound";
	add_opts("-d", &pass_argc, pass_argv);
	while( (c=getopt(argc, argv, "12egciho:p:s")) != -1) {
		switch(c) {
		case 's':
			free(pass_argv[1]);
			testbound_selftest();
			checklock_stop();
			if(log_get_lock()) {
				lock_basic_destroy((lock_basic_type*)log_get_lock());
			}
			exit(0);
		case '1':
#ifdef USE_SHA1
			printf("SHA1 supported\n");
			exit(0);
#else
			printf("SHA1 not supported\n");
			exit(1);
#endif
			break;
		case '2':
#if (defined(HAVE_EVP_SHA256) || defined(HAVE_NSS) || defined(HAVE_NETTLE)) && defined(USE_SHA2)
			printf("SHA256 supported\n");
			exit(0);
#else
			printf("SHA256 not supported\n");
			exit(1);
#endif
			break;
		case 'e':
#if defined(USE_ECDSA)
			printf("ECDSA supported\n");
			exit(0);
#else
			printf("ECDSA not supported\n");
			exit(1);
#endif
			break;
		case 'g':
#ifdef USE_GOST
			if(sldns_key_EVP_load_gost_id()) {
				printf("GOST supported\n");
				exit(0);
			} else {
				printf("GOST not supported\n");
				exit(1);
			}
#else
			printf("GOST not supported\n");
			exit(1);
#endif
			break;
		case 'c':
#ifdef CLIENT_SUBNET
			printf("CLIENT_SUBNET supported\n");
			exit(0);
#else
			printf("CLIENT_SUBNET not supported\n");
			exit(1);
#endif
			break;
		case 'i':
#ifdef USE_IPSECMOD
			printf("IPSECMOD supported\n");
			exit(0);
#else
			printf("IPSECMOD not supported\n");
			exit(1);
#endif
			break;
		case 'p':
			playback_file = optarg;
			break;
		case 'o':
			add_opts(optarg, &pass_argc, pass_argv);
			break;
		case '?':
		case 'h':
		default:
			testbound_usage();
			exit(1);
		}
	}
	argc -= optind;
	/* argv += optind; not using further arguments */
	if(argc != 0) {
		testbound_usage();
		exit(1);
	}
	log_info("Start of %s testbound program.", PACKAGE_STRING);
	if(atexit(&remove_configfile) != 0)
		fatal_exit("atexit() failed: %s", strerror(errno));

	/* setup test environment */
	scen = setup_playback(playback_file, &pass_argc, pass_argv);
	/* init fake event backend */
	fake_event_init(scen);

	pass_argv[pass_argc] = NULL;
	echo_cmdline(pass_argc, pass_argv);

	/* reset getopt processing */
	optind = init_optind;
	optarg = init_optarg;

	/* run the normal daemon */
	res = daemon_main(pass_argc, pass_argv);

	fake_event_cleanup();
	for(c=1; c<pass_argc; c++)
		free(pass_argv[c]);
	if(res == 0) {
		log_info("Testbound Exit Success\n");
		/* remove configfile from here, the atexit() is for when
		 * there is a crash to remove the tmpdir file.
		 * This one removes the file while alloc and log locks are
		 * still valid, and can be logged (for memory calculation),
		 * it leaves the ptr NULL so the atexit does nothing. */
		remove_configfile();
		if(log_get_lock()) {
			lock_basic_destroy((lock_basic_type*)log_get_lock());
		}
#ifdef HAVE_PTHREAD
		/* dlopen frees its thread state (dlopen of gost engine) */
		pthread_exit(NULL);
#endif
	}
	return res;
}

/* fake remote control */
struct listen_port* daemon_remote_open_ports(struct config_file* 
	ATTR_UNUSED(cfg))
{
	return NULL;
}

struct daemon_remote* daemon_remote_create(struct config_file* ATTR_UNUSED(cfg))
{
	return (struct daemon_remote*)calloc(1, sizeof(struct daemon_remote));
}

void daemon_remote_delete(struct daemon_remote* rc)
{
	free(rc);
}

void daemon_remote_clear(struct daemon_remote* ATTR_UNUSED(rc))
{
	/* nothing */
}

int daemon_remote_open_accept(struct daemon_remote* ATTR_UNUSED(rc),
        struct listen_port* ATTR_UNUSED(ports), struct worker* worker)
{
	struct replay_runtime* runtime = (struct replay_runtime*)worker->base;
	runtime->daemon = worker->daemon;
	return 1;
}

int remote_accept_callback(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

int remote_control_callback(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

void remote_get_opt_ssl(char* ATTR_UNUSED(str), void* ATTR_UNUSED(arg))
{
        log_assert(0);
}

#ifdef UB_ON_WINDOWS
void wsvc_command_option(const char* ATTR_UNUSED(wopt), 
	const char* ATTR_UNUSED(cfgfile), int ATTR_UNUSED(v), 
	int ATTR_UNUSED(c))
{
	log_assert(0);
}
#endif

#ifdef UB_ON_WINDOWS
void wsvc_setup_worker(struct worker* ATTR_UNUSED(worker))
{
	/* do nothing */
}
#endif

#ifdef UB_ON_WINDOWS
void wsvc_desetup_worker(struct worker* ATTR_UNUSED(worker))
{
	/* do nothing */
}
#endif

#ifdef UB_ON_WINDOWS
void worker_win_stop_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev),
	void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void wsvc_cron_cb(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}
#endif /* UB_ON_WINDOWS */

int tcp_connect_errno_needs_log(struct sockaddr* ATTR_UNUSED(addr),
	socklen_t ATTR_UNUSED(addrlen))
{
	return 1;
}

int squelch_err_ssl_handshake(unsigned long ATTR_UNUSED(err))
{
	return 0;
}

void listen_setup_locks(void)
{
	/* nothing */
}

void listen_desetup_locks(void)
{
	/* nothing */
}

void fast_reload_printq_list_delete(
	struct fast_reload_printq* ATTR_UNUSED(list))
{
	/* nothing */
}

void fast_reload_worker_pickup_changes(struct worker* ATTR_UNUSED(worker))
{
	/* nothing */
}

#ifdef HAVE_NGTCP2
void* quic_sslctx_create(char* ATTR_UNUSED(key), char* ATTR_UNUSED(pem),
	char* ATTR_UNUSED(verifypem))
{
    return NULL;
}

void comm_point_doq_callback(int ATTR_UNUSED(fd), short ATTR_UNUSED(event),
	void* ATTR_UNUSED(arg))
{
	/* nothing */
}

int doq_conn_cmp(const void* ATTR_UNUSED(key1), const void* ATTR_UNUSED(key2))
{
	return 0;
}

int doq_conid_cmp(const void* ATTR_UNUSED(key1), const void* ATTR_UNUSED(key2))
{
	return 0;
}

int doq_timer_cmp(const void* ATTR_UNUSED(key1), const void* ATTR_UNUSED(key2))
{
	return 0;
}

int doq_stream_cmp(const void* ATTR_UNUSED(key1), const void* ATTR_UNUSED(key2))
{
	return 0;
}

struct doq_table* doq_table_create(struct config_file* ATTR_UNUSED(cfg),
	struct ub_randstate* ATTR_UNUSED(rnd))
{
	return calloc(1, sizeof(struct doq_table));
}

void doq_table_delete(struct doq_table* table)
{
	free(table);
}

void doq_timer_cb(void* ATTR_UNUSED(arg))
{
	/* nothing */
}

size_t doq_table_quic_size_get(struct doq_table* ATTR_UNUSED(table))
{
	return 0;
}
#endif
