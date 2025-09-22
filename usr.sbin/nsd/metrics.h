/*
 * metrics.h -- prometheus metrics endpoint
 *
 * Copyright (c) 2001-2025, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef DAEMON_METRICS_H
#define DAEMON_METRICS_H

struct xfrd_state;
struct nsd_options;
struct daemon_metrics;
struct evbuffer;

#ifdef BIND8_STATS
struct nsdst;
#endif /* BIND8_STATS */

/* the metrics daemon needs little backlog */
#define TCP_BACKLOG_METRICS 16 /* listen() tcp backlog */

/**
 * Create new metrics endpoint for the daemon.
 * @param cfg: config.
 * @return new state, or NULL on failure.
 */
struct daemon_metrics* daemon_metrics_create(struct nsd_options* cfg);

/**
 * Delete metrics daemon and close HTTP listeners.
 * @param m: daemon to delete.
 */
void daemon_metrics_delete(struct daemon_metrics* m);

/**
 * Close metrics HTTP listener ports.
 * Does not delete the object itself.
 * @param m: state to close.
 */
void daemon_metrics_close(struct daemon_metrics* m);

/**
 * Open and create HTTP listeners for metrics daemon.
 * @param m: metrics state that contains list of accept sockets.
 * @param cfg: config options.
 * @return false on failure.
 */
int daemon_metrics_open_ports(struct daemon_metrics* m,
	struct nsd_options* cfg);

/**
 * Setup HTTP listener.
 * @param m: state
 * @param xfrd: the process that hosts the daemon.
 *	m's HTTP listener is attached to its event base.
 */
void daemon_metrics_attach(struct daemon_metrics* m, struct xfrd_state* xfrd);

#ifdef BIND8_STATS
/**
 * Print stats as prometheus metrics to HTTP buffer
 * @param buf: the HTTP buffer to write to
 * @param xfrd: the process that hosts the daemon.
 * @param now: current time
 * @param clear: whether to reset the stats time
 * @param st: the stats
 * @param zonestats: the zonestats
 * @param rc_stats_time: pointer to the remote-control stats_time member
 *   to correctly print the elapsed time since last stats reset
 */
void metrics_print_stats(struct evbuffer *buf, struct xfrd_state *xfrd,
                         struct timeval *now, int clear, struct nsdst *st,
                         struct nsdst **zonestats,
                         struct timeval *rc_stats_time);

#ifdef USE_ZONE_STATS
/**
 * Print zonestat metrics for a single zonestats object
 * @param buf: the HTTP buffer to write to
 * @param name: the zonestats name
 * @param zst: the stats to print
 */
void metrics_zonestat_print_one(struct evbuffer *buf, char *name,
                                struct nsdst *zst);
#endif /* USE_ZONE_STATS */

#endif /*BIND8_STATS*/

#endif /* DAEMON_METRICS_H */
