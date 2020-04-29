/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _Q6_PCM_ROUTING_H
#define _Q6_PCM_ROUTING_H

int q6routing_stream_open(int fedai_id, int perf_mode,
			   int stream_id, int stream_type);
void q6routing_stream_close(int fedai_id, int stream_type);

#endif /*_Q6_PCM_ROUTING_H */
