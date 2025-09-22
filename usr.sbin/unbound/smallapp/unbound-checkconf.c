/*
 * smallapp/unbound-checkconf.c - config file checker for unbound.conf file.
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
 *
 * The config checker checks for syntax and other errors in the unbound.conf
 * file, and can be used to check for errors before the server is started
 * or sigHUPped.
 * Exit status 1 means an error.
 */

#include "config.h"
#include <ctype.h>
#include "util/log.h"
#include "util/config_file.h"
#include "util/module.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "iterator/iterator.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_hints.h"
#include "validator/validator.h"
#include "services/localzone.h"
#include "services/listen_dnsport.h"
#include "services/view.h"
#include "services/authzone.h"
#include "respip/respip.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_GLOB_H
#include <glob.h>
#endif
#ifdef WITH_PYTHONMODULE
#include "pythonmod/pythonmod.h"
#endif
#ifdef CLIENT_SUBNET
#include "edns-subnet/subnet-whitelist.h"
#endif

/** Give checkconf usage, and exit (1). */
static void
usage(void)
{
	printf("Usage:	unbound-checkconf [file]\n");
	printf("	Checks unbound configuration file for errors.\n");
	printf("file	if omitted %s is used.\n", CONFIGFILE);
	printf("-o option	print value of option to stdout.\n");
	printf("-f 		output full pathname with chroot applied, eg. with -o pidfile.\n");
	printf("-q 		quiet (suppress output on success).\n");
	printf("-h		show this usage help.\n");
	printf("Version %s\n", PACKAGE_VERSION);
	printf("BSD licensed, see LICENSE in source package for details.\n");
	printf("Report bugs to %s\n", PACKAGE_BUGREPORT);
	exit(1);
}

/**
 * Print given option to stdout
 * @param cfg: config
 * @param opt: option name without trailing :.
 *	This is different from config_set_option.
 * @param final: if final pathname with chroot applied has to be printed.
 */
static void
print_option(struct config_file* cfg, const char* opt, int final)
{
	if(strcmp(opt, "pidfile") == 0 && final) {
		char *p = fname_after_chroot(cfg->pidfile, cfg, 1);
		if(!p) fatal_exit("out of memory");
		printf("%s\n", p);
		free(p);
		return;
	}
	if(strcmp(opt, "auto-trust-anchor-file") == 0 && final) {
		struct config_strlist* s = cfg->auto_trust_anchor_file_list;
		for(; s; s=s->next) {
			char *p = fname_after_chroot(s->str, cfg, 1);
			if(!p) fatal_exit("out of memory");
			printf("%s\n", p);
			free(p);
		}
		return;
	}
	if(!config_get_option(cfg, opt, config_print_func, stdout))
		fatal_exit("cannot print option '%s'", opt);
}

/** check if module works with config */
static void
check_mod(struct config_file* cfg, struct module_func_block* fb)
{
	struct module_env env;
	memset(&env, 0, sizeof(env));
	env.cfg = cfg;
	env.scratch = regional_create();
	env.scratch_buffer = sldns_buffer_new(BUFSIZ);
	if(!env.scratch || !env.scratch_buffer)
		fatal_exit("out of memory");
	if(!edns_known_options_init(&env))
		fatal_exit("out of memory");
	if(fb->startup && !(*fb->startup)(&env, 0))
		fatal_exit("bad config during startup for %s module", fb->name);
	if(!(*fb->init)(&env, 0))
		fatal_exit("bad config during init for %s module", fb->name);
	(*fb->deinit)(&env, 0);
	if(fb->destartup)
		(*fb->destartup)(&env, 0);
	sldns_buffer_free(env.scratch_buffer);
	regional_destroy(env.scratch);
	edns_known_options_delete(&env);
}

/** true if addr is a localhost address, 127.0.0.1 or ::1 (with maybe "@port"
 * after it) */
static int
str_addr_is_localhost(const char* a)
{
	if(strncmp(a, "127.", 4) == 0) return 1;
	if(strncmp(a, "::1", 3) == 0) return 1;
	return 0;
}

/** check do-not-query-localhost */
static void
donotquerylocalhostcheck(struct config_file* cfg)
{
	if(cfg->donotquery_localhost) {
		struct config_stub* p;
		struct config_strlist* s;
		for(p=cfg->forwards; p; p=p->next) {
			for(s=p->addrs; s; s=s->next) {
				if(str_addr_is_localhost(s->str)) {
					fprintf(stderr, "unbound-checkconf: warning: forward-addr: '%s' is specified for forward-zone: '%s', but do-not-query-localhost: yes means that the address will not be used for lookups.\n",
						s->str, p->name);
				}
			}
		}
		for(p=cfg->stubs; p; p=p->next) {
			for(s=p->addrs; s; s=s->next) {
				if(str_addr_is_localhost(s->str)) {
					fprintf(stderr, "unbound-checkconf: warning: stub-addr: '%s' is specified for stub-zone: '%s', but do-not-query-localhost: yes means that the address will not be used for lookups.\n",
						s->str, p->name);
				}
			}
		}
	}
}

/** check localzones */
static void
localzonechecks(struct config_file* cfg)
{
	struct local_zones* zs;
	if(!(zs = local_zones_create()))
		fatal_exit("out of memory");
	if(!local_zones_apply_cfg(zs, cfg))
		fatal_exit("failed local-zone, local-data configuration");
	local_zones_delete(zs);
}

/** checks for acl and views */
static void
acl_view_tag_checks(struct config_file* cfg, struct views* views)
{
	int d;
	struct sockaddr_storage a;
	socklen_t alen;
	struct config_str2list* acl;
	struct config_str3list* s3;
	struct config_strbytelist* sb;

	/* acl_view */
	for(acl=cfg->acl_view; acl; acl = acl->next) {
		struct view* v;
		if(!netblockstrtoaddr(acl->str, UNBOUND_DNS_PORT, &a, &alen,
			&d)) {
			fatal_exit("cannot parse access-control-view "
				"address %s %s", acl->str, acl->str2);
		}
		v = views_find_view(views, acl->str2, 0);
		if(!v) {
			fatal_exit("cannot find view for "
				"access-control-view: %s %s",
				acl->str, acl->str2);
		}
		lock_rw_unlock(&v->lock);
	}

	/* acl_tags */
	for(sb=cfg->acl_tags; sb; sb = sb->next) {
		if(!netblockstrtoaddr(sb->str, UNBOUND_DNS_PORT, &a, &alen,
			&d)) {
			fatal_exit("cannot parse access-control-tags "
				"address %s", sb->str);
		}
	}

	/* acl_tag_actions */
	for(s3=cfg->acl_tag_actions; s3; s3 = s3->next) {
		enum localzone_type t;
		if(!netblockstrtoaddr(s3->str, UNBOUND_DNS_PORT, &a, &alen,
			&d)) {
			fatal_exit("cannot parse access-control-tag-actions "
				"address %s %s %s",
				s3->str, s3->str2, s3->str3);
		}
		if(find_tag_id(cfg, s3->str2) == -1) {
			fatal_exit("cannot parse tag %s (define-tag it), "
				"for access-control-tag-actions: %s %s %s",
				s3->str2, s3->str, s3->str2, s3->str3);
		}
		if(!local_zone_str2type(s3->str3, &t)) {
			fatal_exit("cannot parse access control action type %s"
				" for access-control-tag-actions: %s %s %s",
				s3->str3, s3->str, s3->str2, s3->str3);
		}
	}

	/* acl_tag_datas */
	for(s3=cfg->acl_tag_datas; s3; s3 = s3->next) {
		char buf[65536];
		uint8_t rr[LDNS_RR_BUF_SIZE];
		size_t len = sizeof(rr);
		int res;
		if(!netblockstrtoaddr(s3->str, UNBOUND_DNS_PORT, &a, &alen,
			&d)) {
			fatal_exit("cannot parse access-control-tag-datas address %s %s '%s'",
				s3->str, s3->str2, s3->str3);
		}
		if(find_tag_id(cfg, s3->str2) == -1) {
			fatal_exit("cannot parse tag %s (define-tag it), "
				"for access-control-tag-datas: %s %s '%s'",
				s3->str2, s3->str, s3->str2, s3->str3);
		}
		/* '.' is sufficient for validation, and it makes the call to
		 * sldns_wirerr_get_type() simpler below. */
		snprintf(buf, sizeof(buf), "%s %s", ".", s3->str3);
		res = sldns_str2wire_rr_buf(buf, rr, &len, NULL, 3600, NULL,
			0, NULL, 0);
		if(res != 0) {
			fatal_exit("cannot parse rr data [char %d] parse error %s, for access-control-tag-datas: %s %s '%s'",
				(int)LDNS_WIREPARSE_OFFSET(res)-2,
				sldns_get_errorstr_parse(res),
				s3->str, s3->str2, s3->str3);
		}
	}
}

/** check view and response-ip configuration */
static void
view_and_respipchecks(struct config_file* cfg)
{
	struct views* views = NULL;
	struct respip_set* respip = NULL;
	int ignored = 0;
	if(!(views = views_create()))
		fatal_exit("Could not create views: out of memory");
	if(!(respip = respip_set_create()))
		fatal_exit("Could not create respip set: out of memory");
	if(!views_apply_cfg(views, cfg))
		fatal_exit("Could not set up views");
	if(!respip_global_apply_cfg(respip, cfg))
		fatal_exit("Could not setup respip set");
	if(!respip_views_apply_cfg(views, cfg, &ignored))
		fatal_exit("Could not setup per-view respip sets");
	acl_view_tag_checks(cfg, views);
	views_delete(views);
	respip_set_delete(respip);
}

/** emit warnings for IP in hosts */
static void
warn_hosts(const char* typ, struct config_stub* list)
{
	struct sockaddr_storage a;
	socklen_t alen;
	struct config_stub* s;
	struct config_strlist* h;
	for(s=list; s; s=s->next) {
		for(h=s->hosts; h; h=h->next) {
			if(extstrtoaddr(h->str, &a, &alen, UNBOUND_DNS_PORT)) {
				fprintf(stderr, "unbound-checkconf: warning:"
				  " %s %s: \"%s\" is an IP%s address, "
				  "and when looked up as a host name "
				  "during use may not resolve.\n",
				  s->name, typ, h->str,
				  addr_is_ip6(&a, alen)?"6":"4");
			}
		}
	}
}

/** check interface strings */
static void
interfacechecks(struct config_file* cfg)
{
	int d;
	struct sockaddr_storage a;
	socklen_t alen;
	int i, j, i2, j2;
	char*** resif = NULL;
	int* num_resif = NULL;

	if(cfg->num_ifs != 0) {
		resif = (char***)calloc(cfg->num_ifs, sizeof(char**));
		if(!resif) fatal_exit("malloc failure");
		num_resif = (int*)calloc(cfg->num_ifs, sizeof(int));
		if(!num_resif) fatal_exit("malloc failure");
	}
	for(i=0; i<cfg->num_ifs; i++) {
		/* search for duplicates in IP or ifname arguments */
		for(i2=0; i2<i; i2++) {
			if(strcmp(cfg->ifs[i], cfg->ifs[i2]) == 0) {
				fatal_exit("interface: %s present twice, "
					"cannot bind same ports twice.",
					cfg->ifs[i]);
			}
		}
		if(!resolve_interface_names(&cfg->ifs[i], 1, NULL, &resif[i],
			&num_resif[i])) {
			fatal_exit("could not resolve interface names, for %s",
				cfg->ifs[i]);
		}
		/* check for port combinations that are not supported */
		if(if_is_pp2(resif[i][0], cfg->port, cfg->proxy_protocol_port)) {
			if(if_is_dnscrypt(resif[i][0], cfg->port,
				cfg->dnscrypt_port)) {
				fatal_exit("PROXYv2 and DNSCrypt combination not "
					"supported!");
			} else if(if_is_https(resif[i][0], cfg->port,
				cfg->https_port)) {
				fatal_exit("PROXYv2 and DoH combination not "
					"supported!");
			} else if(if_is_quic(resif[i][0], cfg->port,
				cfg->quic_port)) {
				fatal_exit("PROXYv2 and DoQ combination not "
					"supported!");
			}
		}
		/* search for duplicates in the returned addresses */
		for(j=0; j<num_resif[i]; j++) {
			if(!extstrtoaddr(resif[i][j], &a, &alen, cfg->port)) {
				if(strcmp(cfg->ifs[i], resif[i][j]) != 0)
					fatal_exit("cannot parse interface address '%s' from the interface specified as '%s'",
						resif[i][j], cfg->ifs[i]);
				else
					fatal_exit("cannot parse interface specified as '%s'",
						cfg->ifs[i]);
			}
			for(i2=0; i2<i; i2++) {
				for(j2=0; j2<num_resif[i2]; j2++) {
					if(strcmp(resif[i][j], resif[i2][j2])
						== 0) {
						char info1[1024], info2[1024];
						if(strcmp(cfg->ifs[i], resif[i][j]) != 0)
							snprintf(info1, sizeof(info1), "address %s from interface: %s", resif[i][j], cfg->ifs[i]);
						else	snprintf(info1, sizeof(info1), "interface: %s", cfg->ifs[i]);
						if(strcmp(cfg->ifs[i2], resif[i2][j2]) != 0)
							snprintf(info2, sizeof(info2), "address %s from interface: %s", resif[i2][j2], cfg->ifs[i2]);
						else	snprintf(info2, sizeof(info2), "interface: %s", cfg->ifs[i2]);
						fatal_exit("%s present twice, cannot bind the same ports twice. The first entry is %s and the second is %s", resif[i][j], info2, info1);
					}
				}
			}
		}
	}

	for(i=0; i<cfg->num_ifs; i++) {
		config_del_strarray(resif[i], num_resif[i]);
	}
	free(resif);
	free(num_resif);

	for(i=0; i<cfg->num_out_ifs; i++) {
		if(!ipstrtoaddr(cfg->out_ifs[i], UNBOUND_DNS_PORT, &a, &alen) &&
		   !netblockstrtoaddr(cfg->out_ifs[i], UNBOUND_DNS_PORT, &a, &alen, &d)) {
			fatal_exit("cannot parse outgoing-interface "
				"specified as '%s'", cfg->out_ifs[i]);
		}
		for(j=0; j<cfg->num_out_ifs; j++) {
			if(i!=j && strcmp(cfg->out_ifs[i], cfg->out_ifs[j])==0)
				fatal_exit("outgoing-interface: %s present "
					"twice, cannot bind same ports twice.",
					cfg->out_ifs[i]);
		}
	}
}

/** check interface-automatic-ports */
static void
ifautomaticportschecks(char* ifautomaticports)
{
	char* now = ifautomaticports;
	while(now && *now) {
		char* after;
		int extraport;
		while(isspace((unsigned char)*now))
			now++;
		if(!*now)
			break;
		after = now;
		extraport = (int)strtol(now, &after, 10);
		if(extraport < 0 || extraport > 65535)
			fatal_exit("interface-automatic-ports: port out of range at position %d in '%s'", (int)(now-ifautomaticports)+1, ifautomaticports);
		if(extraport == 0 && now == after)
			fatal_exit("interface-automatic-ports: parse error at position %d in '%s'", (int)(now-ifautomaticports)+1, ifautomaticports);
		now = after;
	}
}

/** check acl ips */
static void
aclchecks(struct config_file* cfg)
{
	int d;
	struct sockaddr_storage a;
	socklen_t alen;
	struct config_str2list* acl;
	for(acl=cfg->acls; acl; acl = acl->next) {
		if(!netblockstrtoaddr(acl->str, UNBOUND_DNS_PORT, &a, &alen,
			&d)) {
			fatal_exit("cannot parse access control address %s %s",
				acl->str, acl->str2);
		}
	}
}

/** check tcp connection limit ips */
static void
tcpconnlimitchecks(struct config_file* cfg)
{
	int d;
	struct sockaddr_storage a;
	socklen_t alen;
	struct config_str2list* tcl;
	for(tcl=cfg->tcp_connection_limits; tcl; tcl = tcl->next) {
		if(!netblockstrtoaddr(tcl->str, UNBOUND_DNS_PORT, &a, &alen,
			&d)) {
			fatal_exit("cannot parse tcp connection limit address %s %s",
				tcl->str, tcl->str2);
		}
	}
}

/** true if fname is a file */
static int
is_file(const char* fname)
{
	struct stat buf;
	if(stat(fname, &buf) < 0) {
		if(errno==EACCES) {
			printf("warning: no search permission for one of the directories in path: %s\n", fname);
			return 1;
		}
		perror(fname);
		return 0;
	}
	if(S_ISDIR(buf.st_mode)) {
		printf("%s is not a file\n", fname);
		return 0;
	}
	return 1;
}

/** true if fname is a directory */
static int
is_dir(const char* fname)
{
	struct stat buf;
	if(stat(fname, &buf) < 0) {
		if(errno==EACCES) {
			printf("warning: no search permission for one of the directories in path: %s\n", fname);
			return 1;
		}
		perror(fname);
		return 0;
	}
	if(!(S_ISDIR(buf.st_mode))) {
		printf("%s is not a directory\n", fname);
		return 0;
	}
	return 1;
}

/** get base dir of a fname */
static char*
basedir(char* fname)
{
	char* rev;
	if(!fname) fatal_exit("out of memory");
	rev = strrchr(fname, '/');
	if(!rev) return NULL;
	if(fname == rev) return NULL;
	rev[0] = 0;
	return fname;
}

/** check chroot for a file string */
static void
check_chroot_string(const char* desc, char** ss,
	const char* chrootdir, struct config_file* cfg)
{
	char* str = *ss;
	if(str && str[0]) {
		*ss = fname_after_chroot(str, cfg, 1);
		if(!*ss) fatal_exit("out of memory");
		if(!is_file(*ss)) {
			if(chrootdir && chrootdir[0])
				fatal_exit("%s: \"%s\" does not exist in "
					"chrootdir %s", desc, str, chrootdir);
			else
				fatal_exit("%s: \"%s\" does not exist",
					desc, str);
		}
		/* put in a new full path for continued checking */
		free(str);
	}
}

/** check file list, every file must be inside the chroot location */
static void
check_chroot_filelist(const char* desc, struct config_strlist* list,
	const char* chrootdir, struct config_file* cfg)
{
	struct config_strlist* p;
	for(p=list; p; p=p->next) {
		check_chroot_string(desc, &p->str, chrootdir, cfg);
	}
}

/** check file list, with wildcard processing */
static void
check_chroot_filelist_wild(const char* desc, struct config_strlist* list,
	const char* chrootdir, struct config_file* cfg)
{
	struct config_strlist* p;
	for(p=list; p; p=p->next) {
#ifdef HAVE_GLOB
		if(strchr(p->str, '*') || strchr(p->str, '[') ||
			strchr(p->str, '?') || strchr(p->str, '{') ||
			strchr(p->str, '~')) {
			char* s = p->str;
			/* adjust whole pattern for chroot and check later */
			p->str = fname_after_chroot(p->str, cfg, 1);
			free(s);
		} else
#endif /* HAVE_GLOB */
			check_chroot_string(desc, &p->str, chrootdir, cfg);
	}
}

#ifdef CLIENT_SUBNET
/** check ECS configuration */
static void
ecs_conf_checks(struct config_file* cfg)
{
	struct ecs_whitelist* whitelist = NULL;
	if(!(whitelist = ecs_whitelist_create()))
		fatal_exit("Could not create ednssubnet whitelist: out of memory");
        if(!ecs_whitelist_apply_cfg(whitelist, cfg))
		fatal_exit("Could not setup ednssubnet whitelist");
	ecs_whitelist_delete(whitelist);
}
#endif /* CLIENT_SUBNET */

/** check that the modules exist, are compiled in */
static void
check_modules_exist(const char* module_conf)
{
	const char** names = module_list_avail();
	const char* s = module_conf;
	while(*s) {
		int i = 0;
		int is_ok = 0;
		while(*s && isspace((unsigned char)*s))
			s++;
		if(!*s) break;
		while(names[i]) {
			if(strncmp(names[i], s, strlen(names[i])) == 0) {
				is_ok = 1;
				break;
			}
			i++;
		}
		if(is_ok == 0) {
			char n[64];
			size_t j;
			n[0]=0;
			n[sizeof(n)-1]=0;
			for(j=0; j<sizeof(n)-1; j++) {
				if(!s[j] || isspace((unsigned char)s[j])) {
					n[j] = 0;
					break;
				}
				n[j] = s[j];
			}
			fatal_exit("module_conf lists module '%s' but that "
				"module is not available.", n);
		}
		s += strlen(names[i]);
	}
}

/** check configuration for errors */
static void
morechecks(struct config_file* cfg)
{
	warn_hosts("stub-host", cfg->stubs);
	warn_hosts("forward-host", cfg->forwards);
	interfacechecks(cfg);
	ifautomaticportschecks(cfg->if_automatic_ports);
	aclchecks(cfg);
	tcpconnlimitchecks(cfg);

	if(cfg->verbosity < 0)
		fatal_exit("verbosity value < 0");
	if(cfg->num_threads <= 0 || cfg->num_threads > 10000)
		fatal_exit("num_threads value weird");
	if(!cfg->do_ip4 && !cfg->do_ip6)
		fatal_exit("ip4 and ip6 are both disabled, pointless");
	if(!cfg->do_ip4 && cfg->prefer_ip4)
		fatal_exit("cannot prefer and disable ip4, pointless");
	if(!cfg->do_ip6 && cfg->prefer_ip6)
		fatal_exit("cannot prefer and disable ip6, pointless");
	if(!cfg->do_udp && !cfg->do_tcp)
		fatal_exit("udp and tcp are both disabled, pointless");
	if(cfg->edns_buffer_size > cfg->msg_buffer_size)
		fatal_exit("edns-buffer-size larger than msg-buffer-size, "
			"answers will not fit in processing buffer");
#ifdef UB_ON_WINDOWS
	w_config_adjust_directory(cfg);
#endif
	if(cfg->chrootdir && cfg->chrootdir[0] &&
		cfg->chrootdir[strlen(cfg->chrootdir)-1] == '/')
		fatal_exit("chootdir %s has trailing slash '/' please remove.",
			cfg->chrootdir);
	if(cfg->chrootdir && cfg->chrootdir[0] &&
		!is_dir(cfg->chrootdir)) {
		fatal_exit("bad chroot directory");
	}
	if(cfg->directory && cfg->directory[0]) {
		char* ad = fname_after_chroot(cfg->directory, cfg, 0);
		if(!ad) fatal_exit("out of memory");
		if(!is_dir(ad)) fatal_exit("bad chdir directory");
		free(ad);
	}
	if( (cfg->chrootdir && cfg->chrootdir[0]) ||
	    (cfg->directory && cfg->directory[0])) {
		if(cfg->pidfile && cfg->pidfile[0]) {
			char* ad = (cfg->pidfile[0]=='/')?strdup(cfg->pidfile):
				fname_after_chroot(cfg->pidfile, cfg, 1);
			char* bd = basedir(ad);
			if(bd && !is_dir(bd))
				fatal_exit("pidfile directory does not exist");
			free(ad);
		}
		if(cfg->logfile && cfg->logfile[0]) {
			char* ad = fname_after_chroot(cfg->logfile, cfg, 1);
			char* bd = basedir(ad);
			if(bd && !is_dir(bd))
				fatal_exit("logfile directory does not exist");
			free(ad);
		}
	}

	check_chroot_filelist("file with root-hints",
		cfg->root_hints, cfg->chrootdir, cfg);
	check_chroot_filelist("trust-anchor-file",
		cfg->trust_anchor_file_list, cfg->chrootdir, cfg);
	check_chroot_filelist("auto-trust-anchor-file",
		cfg->auto_trust_anchor_file_list, cfg->chrootdir, cfg);
	check_chroot_filelist_wild("trusted-keys-file",
		cfg->trusted_keys_file_list, cfg->chrootdir, cfg);
	if(cfg->disable_edns_do && strstr(cfg->module_conf, "validator")
		&& (cfg->trust_anchor_file_list
		|| cfg->trust_anchor_list
		|| cfg->auto_trust_anchor_file_list
		|| cfg->trusted_keys_file_list)) {
		char* key = NULL;
		if(cfg->auto_trust_anchor_file_list)
			key = cfg->auto_trust_anchor_file_list->str;
		if(!key && cfg->trust_anchor_file_list)
			key = cfg->trust_anchor_file_list->str;
		if(!key && cfg->trust_anchor_list)
			key = cfg->trust_anchor_list->str;
		if(!key && cfg->trusted_keys_file_list)
			key = cfg->trusted_keys_file_list->str;
		if(!key) key = "";
		fatal_exit("disable-edns-do does not allow DNSSEC to work, but the validator module uses a trust anchor %s, turn off disable-edns-do or disable validation", key);
	}
#ifdef USE_IPSECMOD
	if(cfg->ipsecmod_enabled && strstr(cfg->module_conf, "ipsecmod")) {
		/* only check hook if enabled */
		check_chroot_string("ipsecmod-hook", &cfg->ipsecmod_hook,
			cfg->chrootdir, cfg);
	}
#endif
	/* remove chroot setting so that modules are not stripping pathnames */
	free(cfg->chrootdir);
	cfg->chrootdir = NULL;

	/* check that the modules listed in module_conf exist */
	check_modules_exist(cfg->module_conf);

	/* Respip is known to *not* work with dns64. */
	if(strcmp(cfg->module_conf, "iterator") != 0
		&& strcmp(cfg->module_conf, "validator iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 validator iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 iterator") != 0
		&& strcmp(cfg->module_conf, "respip iterator") != 0
		&& strcmp(cfg->module_conf, "respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "respip dns64 validator iterator") != 0
		&& strcmp(cfg->module_conf, "respip dns64 iterator") != 0
#ifdef WITH_PYTHONMODULE
		&& strcmp(cfg->module_conf, "python iterator") != 0
		&& strcmp(cfg->module_conf, "python respip iterator") != 0
		&& strcmp(cfg->module_conf, "python validator iterator") != 0
		&& strcmp(cfg->module_conf, "python respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "validator python iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 python iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 python validator iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 validator python iterator") != 0
		&& strcmp(cfg->module_conf, "python dns64 iterator") != 0
		&& strcmp(cfg->module_conf, "python dns64 validator iterator") != 0
#endif
#ifdef WITH_DYNLIBMODULE
		&& strcmp(cfg->module_conf, "dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dynlib dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "python dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "python dynlib dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "python dynlib dynlib dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib respip iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dynlib dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "python dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "python dynlib dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "python dynlib dynlib dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "validator dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 validator dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dns64 iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dns64 validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dns64 cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib dns64 validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 dynlib cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 dynlib validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib respip cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib respip validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "cachedb dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "respip cachedb dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "validator cachedb dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "respip validator cachedb dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "validator dynlib cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "respip validator dynlib cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib respip subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib respip subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache validator dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache validator dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib ipsecmod iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib ipsecmod respip iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod dynlib respip iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib ipsecmod validator iterator") != 0
		&& strcmp(cfg->module_conf, "dynlib ipsecmod respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod dynlib validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod dynlib respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod validator dynlib iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod respip validator dynlib iterator") != 0
#endif
#ifdef USE_CACHEDB
		&& strcmp(cfg->module_conf, "validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "respip validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "respip cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 cachedb iterator") != 0
#endif
#if defined(WITH_PYTHONMODULE) && defined(USE_CACHEDB)
		&& strcmp(cfg->module_conf, "python dns64 cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "python dns64 validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 python cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 python validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "python cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "python respip cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "python validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "python respip validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "cachedb python iterator") != 0
		&& strcmp(cfg->module_conf, "respip cachedb python iterator") != 0
		&& strcmp(cfg->module_conf, "validator cachedb python iterator") != 0
		&& strcmp(cfg->module_conf, "respip validator cachedb python iterator") != 0
		&& strcmp(cfg->module_conf, "validator python cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "respip validator python cachedb iterator") != 0
#endif
#if defined(CLIENT_SUBNET) && defined(USE_CACHEDB)
		&& strcmp(cfg->module_conf, "respip subnetcache validator cachedb iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache validator cachedb iterator") != 0
#endif
#ifdef CLIENT_SUBNET
		&& strcmp(cfg->module_conf, "subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 subnetcache respip iterator") != 0
		&& strcmp(cfg->module_conf, "dns64 subnetcache respip validator iterator") != 0
#endif
#if defined(WITH_PYTHONMODULE) && defined(CLIENT_SUBNET)
		&& strcmp(cfg->module_conf, "python subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "python respip subnetcache iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache python iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache python iterator") != 0
		&& strcmp(cfg->module_conf, "python subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "python respip subnetcache validator iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache python validator iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache python validator iterator") != 0
		&& strcmp(cfg->module_conf, "subnetcache validator python iterator") != 0
		&& strcmp(cfg->module_conf, "respip subnetcache validator python iterator") != 0
#endif
#ifdef USE_IPSECMOD
		&& strcmp(cfg->module_conf, "ipsecmod iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod respip iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod respip validator iterator") != 0
#endif
#if defined(WITH_PYTHONMODULE) && defined(USE_IPSECMOD)
		&& strcmp(cfg->module_conf, "python ipsecmod iterator") != 0
		&& strcmp(cfg->module_conf, "python ipsecmod respip iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod python iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod python respip iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "python ipsecmod validator iterator") != 0
		&& strcmp(cfg->module_conf, "python ipsecmod respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod python validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod python respip validator iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod validator python iterator") != 0
		&& strcmp(cfg->module_conf, "ipsecmod respip validator python iterator") != 0
#endif
#ifdef USE_IPSET
		&& strcmp(cfg->module_conf, "validator ipset iterator") != 0
		&& strcmp(cfg->module_conf, "validator ipset respip iterator") != 0
		&& strcmp(cfg->module_conf, "ipset iterator") != 0
		&& strcmp(cfg->module_conf, "ipset respip iterator") != 0
#endif
		) {
		fatal_exit("module conf '%s' is not known to work",
			cfg->module_conf);
	}

#ifdef HAVE_GETPWNAM
	if(cfg->username && cfg->username[0]) {
		if(getpwnam(cfg->username) == NULL)
			fatal_exit("user '%s' does not exist.", cfg->username);
#  ifdef HAVE_ENDPWENT
		endpwent();
#  endif
	}

	if (pledge("stdio rpath", NULL) == -1)
		fatal_exit("Could not pledge");

#endif
	if(cfg->remote_control_enable && options_remote_is_address(cfg)
		&& cfg->control_use_cert) {
		check_chroot_string("server-key-file", &cfg->server_key_file,
			cfg->chrootdir, cfg);
		check_chroot_string("server-cert-file", &cfg->server_cert_file,
			cfg->chrootdir, cfg);
		if(!is_file(cfg->control_key_file))
			fatal_exit("control-key-file: \"%s\" does not exist",
				cfg->control_key_file);
		if(!is_file(cfg->control_cert_file))
			fatal_exit("control-cert-file: \"%s\" does not exist",
				cfg->control_cert_file);
	}

	donotquerylocalhostcheck(cfg);
	localzonechecks(cfg);
	view_and_respipchecks(cfg);
#ifdef CLIENT_SUBNET
	ecs_conf_checks(cfg);
#endif
}

/** check forwards */
static void
check_fwd(struct config_file* cfg)
{
	struct iter_forwards* fwd = forwards_create();
	if(!fwd || !forwards_apply_cfg(fwd, cfg)) {
		fatal_exit("Could not set forward zones");
	}
	forwards_delete(fwd);
}

/** check hints */
static void
check_hints(struct config_file* cfg)
{
	struct iter_hints* hints = hints_create();
	if(!hints || !hints_apply_cfg(hints, cfg)) {
		fatal_exit("Could not set root or stub hints");
	}
	hints_delete(hints);
}

/** check auth zones */
static void
check_auth(struct config_file* cfg)
{
	int is_rpz = 0;
	struct auth_zones* az = auth_zones_create();
	if(!az || !auth_zones_apply_cfg(az, cfg, 0, &is_rpz, NULL, NULL)) {
		fatal_exit("Could not setup authority zones");
	}
	auth_zones_delete(az);
}

/** check config file */
static void
checkconf(const char* cfgfile, const char* opt, int final, int quiet)
{
	char oldwd[4096];
	struct config_file* cfg = config_create();
	if(!cfg)
		fatal_exit("out of memory");
	oldwd[0] = 0;
	if(!getcwd(oldwd, sizeof(oldwd))) {
		log_err("cannot getcwd: %s", strerror(errno));
		oldwd[0] = 0;
	}
	if(!config_read(cfg, cfgfile, NULL)) {
		/* config_read prints messages to stderr */
		config_delete(cfg);
		exit(1);
	}
	if(oldwd[0] && chdir(oldwd) == -1)
		log_err("cannot chdir(%s): %s", oldwd, strerror(errno));
	if(opt) {
		print_option(cfg, opt, final);
		config_delete(cfg);
		return;
	}
	morechecks(cfg);
	check_mod(cfg, iter_get_funcblock());
	check_mod(cfg, val_get_funcblock());
#ifdef WITH_PYTHONMODULE
	if(strstr(cfg->module_conf, "python"))
		check_mod(cfg, pythonmod_get_funcblock());
#endif
	check_fwd(cfg);
	check_hints(cfg);
	check_auth(cfg);
	if(!quiet) { printf("unbound-checkconf: no errors in %s\n", cfgfile); }
	config_delete(cfg);
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** Main routine for checkconf */
int main(int argc, char* argv[])
{
	int c;
	int final = 0;
	int quiet = 0;
	const char* f;
	const char* opt = NULL;
	const char* cfgfile = CONFIGFILE;
	checklock_start();
	log_ident_set("unbound-checkconf");
	log_init(NULL, 0, NULL);
#ifdef USE_WINSOCK
	/* use registry config file in preference to compiletime location */
	if(!(cfgfile=w_lookup_reg_str("Software\\Unbound", "ConfigFile")))
		cfgfile = CONFIGFILE;
#endif /* USE_WINSOCK */
	/* parse the options */
	while( (c=getopt(argc, argv, "fhqo:")) != -1) {
		switch(c) {
		case 'f':
			final = 1;
			break;
		case 'o':
			opt = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		case '?':
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if(argc != 0 && argc != 1)
		usage();
	if(argc == 1)
		f = argv[0];
	else	f = cfgfile;

	if (pledge("stdio rpath dns getpw", NULL) == -1)
		fatal_exit("Could not pledge");

	checkconf(f, opt, final, quiet);
	checklock_stop();
	return 0;
}
