/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,  Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_PIDCTRL_H_
#define _SYS_PIDCTRL_H_

/*
 * Proportional Integral Derivative controller.
 *
 * This controller is intended to replace a multitude of threshold based
 * daemon regulation systems.  These systems produce sharp sawtooths of
 * activity which can cause latency spikes and other undesireable bursty
 * behavior.  The PID controller adapts to changing load conditions and
 * adjusts the work done by the daemon to keep a smoother output.
 *
 * The setpoint can be thought of as a single watermark that the controller
 * is always trying to reach.  Compared to a high water/low water type
 * algorithm the pid controller is dynamically deciding the low water and
 * regulating to the high water.  The setpoint should be high enough that
 * the controller and daemon have time to observe the rise in value and
 * respond to it, else the resource may be exhausted.  More frequent wakeups
 * permit higher setpoints and less underutilized resources.
 *
 * The controller has been optimised for simplicity of math making it quite
 * inexpensive to execute.  There is no floating point and so the gains must
 * be the inverse of whole integers.
 *
 * Failing to measure and tune the gain parameters can result in wild
 * oscillations in output.  It is strongly encouraged that controllers are
 * tested and tuned under a wide variety of workloads before gain values are
 * picked.  Some reasonable defaults are provided below.
 */

struct pidctrl {
	/* Saved control variables. */
	int	pc_error;		/* Current error. */
	int	pc_olderror;		/* Saved error for derivative. */
	int	pc_integral;		/* Integral accumulator. */
	int	pc_derivative;		/* Change from last error. */
	int	pc_input;		/* Last input. */
	int	pc_output;		/* Last output. */
	int	pc_ticks;		/* Last sampling time. */
	/* configuration options, runtime tunable via sysctl */
	int	pc_setpoint;		/* Desired level */
	int	pc_interval;		/* Update interval in ticks. */
	int	pc_bound;		/* Integral wind-up limit. */
	int	pc_Kpd;			/* Proportional gain divisor. */
	int	pc_Kid;			/* Integral gain divisor. */
	int	pc_Kdd;			/* Derivative gain divisor. */
};

/*
 * Reasonable default divisors.
 *
 * Actual gains are 1/divisor.  Gains interact in complex ways with the
 * setpoint and interval.  Measurement under multiple loads should be
 * taken to ensure adequate stability and rise time.
 */
#define	PIDCTRL_KPD	3		/* Default proportional divisor. */
#define	PIDCTRL_KID	4		/* Default integral divisor. */
#define	PIDCTRL_KDD	8		/* Default derivative divisor. */
#define	PIDCTRL_BOUND	4		/* Bound factor, setpoint multiple. */

struct sysctl_oid_list;

void	pidctrl_init(struct pidctrl *pc, int interval, int setpoint,
	    int bound, int Kpd, int Kid, int Kdd);
void	pidctrl_init_sysctl(struct pidctrl *pc, struct sysctl_oid_list *parent);

/*
 * This is the classic PID controller where the interval is clamped to
 * [-bound, bound] and the output may be negative.  This should be used
 * in continuous control loops that can adjust a process variable in
 * either direction.  This is a descrete time controller and should
 * only be called once per-interval or the derivative term will be
 * inaccurate.
 */
int	pidctrl_classic(struct pidctrl *pc, int input);

/*
 * This controler is intended for consumer type daemons that can only
 * regulate in a positive direction, that is to say, they can not exert
 * positive pressure on the process variable or input.  They can only
 * reduce it by doing work.  As such the integral is bound between [0, bound]
 * and the output is similarly a positive value reflecting the units of
 * work necessary to be completed in the current interval to eliminate error.
 *
 * It is a descrete time controller but can be invoked more than once in a
 * given time interval for ease of client implementation.  This should only
 * be done in overload situations or the controller may not produce a stable
 * output.  Calling it less frequently when there is no work to be done will
 * increase the rise time but should otherwise be harmless.
 */
int	pidctrl_daemon(struct pidctrl *pc, int input);

#endif	/* !_SYS_PIDCTRL_H_ */
