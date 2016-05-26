/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Tor Jeremiassen <tor.jeremiassen@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>

#include "perf.h"
#include "thread_map.h"
#include "thread.h"
#include "thread-stack.h"
#include "callchain.h"
#include "auxtrace.h"
#include "evlist.h"
#include "machine.h"
#include "util.h"
#include "util/intlist.h"
#include "color.h"
#include "cs-etm.h"
#include "cs-etm-decoder/cs-etm-decoder.h"
#include "debug.h"

#include <stdlib.h>

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)
#define MAX_TIMESTAMP (~0ULL)

struct cs_etm_auxtrace {
        struct auxtrace         auxtrace;
        struct auxtrace_queues  queues;
        struct auxtrace_heap    heap;
        u64                    **metadata;
        u32                     auxtrace_type;
        struct perf_session    *session;
        struct machine         *machine;
        struct perf_evsel      *switch_evsel;
        struct thread          *unknown_thread;
        uint32_t                num_cpu;
        bool                    timeless_decoding;
        bool                    sampling_mode;
        bool                    snapshot_mode;
        bool                    data_queued;
        bool                    sync_switch;
        bool                    synth_needs_swap;
        int                     have_sched_switch;

        bool                    sample_instructions;
        u64                     instructions_sample_type;
        u64                     instructions_sample_period;
        u64                     instructions_id;
        struct itrace_synth_opts synth_opts;
        unsigned                pmu_type;
};

struct cs_etm_queue {
        struct cs_etm_auxtrace *etm;
        unsigned                queue_nr;
        struct auxtrace_buffer *buffer;
        const struct           cs_etm_state *state;
        struct ip_callchain    *chain;
        union perf_event       *event_buf;
        bool                    on_heap;
        bool                    step_through_buffers;
        bool                    use_buffer_pid_tid;
        pid_t                   pid, tid;
        int                     cpu;
        struct thread          *thread;
        u64                     time;
        u64                     timestamp;
        bool                    stop;
        struct cs_etm_decoder  *decoder;
        u64                     offset;
        bool                    eot;
        bool                    kernel_mapped;
};

static int cs_etm__get_trace(struct cs_etm_buffer *buff, struct cs_etm_queue *etmq);
static int cs_etm__update_queues(struct cs_etm_auxtrace *);
static int cs_etm__process_queues(struct cs_etm_auxtrace *, u64);
static int cs_etm__process_timeless_queues(struct cs_etm_auxtrace *, pid_t, u64);
static uint32_t cs_etm__mem_access(struct cs_etm_queue *, uint64_t , size_t , uint8_t *);

static void cs_etm__packet_dump(const char *pkt_string)
{
        const char *color = PERF_COLOR_BLUE;

        color_fprintf(stdout,color, "  %s\n", pkt_string);
        fflush(stdout);
}

static void cs_etm__dump_event(struct cs_etm_auxtrace *etm,
                              struct auxtrace_buffer *buffer)
{
        const char *color = PERF_COLOR_BLUE;
        struct cs_etm_decoder_params d_params;
        struct cs_etm_trace_params *t_params;
        struct cs_etm_decoder *decoder;
        size_t buffer_used = 0;
        size_t i;

        fprintf(stdout,"\n");
        color_fprintf(stdout, color,
                     ". ... CoreSight ETM Trace data: size %zu bytes\n",
                     buffer->size);

        t_params = zalloc(sizeof(struct cs_etm_trace_params) * etm->num_cpu);
        for (i = 0; i < etm->num_cpu; ++i) {
                t_params[i].protocol = CS_ETM_PROTO_ETMV4i;
                t_params[i].reg_idr0 = etm->metadata[i][CS_ETMV4_TRCIDR0];
                t_params[i].reg_idr1 = etm->metadata[i][CS_ETMV4_TRCIDR1];
                t_params[i].reg_idr2 = etm->metadata[i][CS_ETMV4_TRCIDR2];
                t_params[i].reg_idr8 = etm->metadata[i][CS_ETMV4_TRCIDR8];
                t_params[i].reg_configr = etm->metadata[i][CS_ETMV4_TRCCONFIGR];
                t_params[i].reg_traceidr = etm->metadata[i][CS_ETMV4_TRCTRACEIDR];
  //[CS_ETMV4_TRCAUTHSTATUS] = "   TRCAUTHSTATUS                  %"PRIx64"\n",
        }
        d_params.packet_printer = cs_etm__packet_dump;
        d_params.operation = CS_ETM_OPERATION_PRINT;
        d_params.formatted = true;
        d_params.fsyncs = false;
        d_params.hsyncs = false;
        d_params.frame_aligned = true;

        decoder = cs_etm_decoder__new(etm->num_cpu,&d_params, t_params);

        zfree(&t_params);

        if (decoder == NULL) {
                return; 
        }
        do {
            size_t consumed;
            cs_etm_decoder__process_data_block(decoder,buffer->offset,&(((uint8_t *)buffer->data)[buffer_used]),buffer->size - buffer_used, &consumed);
            buffer_used += consumed;
        } while(buffer_used < buffer->size);
        cs_etm_decoder__free(decoder);
}
                              
static int cs_etm__flush_events(struct perf_session *session, struct perf_tool *tool){
        struct cs_etm_auxtrace *etm = container_of(session->auxtrace,
                                                   struct cs_etm_auxtrace,
                                                   auxtrace);

        int ret;

        if (dump_trace)
                return 0;

        if (!tool->ordered_events)
                return -EINVAL;

        ret = cs_etm__update_queues(etm);

        if (ret < 0) 
                return ret;

        if (etm->timeless_decoding)
                return cs_etm__process_timeless_queues(etm,-1,MAX_TIMESTAMP - 1);

        return cs_etm__process_queues(etm, MAX_TIMESTAMP);
}

static void  cs_etm__set_pid_tid_cpu(struct cs_etm_auxtrace *etm,
                                    struct auxtrace_queue *queue)
{
        struct cs_etm_queue *etmq = queue->priv;

        if ((queue->tid == -1) || (etm->have_sched_switch)) {
                etmq->tid = machine__get_current_tid(etm->machine, etmq->cpu);
                thread__zput(etmq->thread);
        }

        if ((!etmq->thread) && (etmq->tid != -1)) {
                etmq->thread = machine__find_thread(etm->machine,-1,etmq->tid);
        }

        if (etmq->thread) {
                etmq->pid = etmq->thread->pid_;
                if (queue->cpu == -1) {
                        etmq->cpu = etmq->thread->cpu;
                }
        }
}

static void cs_etm__free_queue(void *priv)
{
        struct cs_etm_queue *etmq = priv;

        if (!etmq)
                return;

        thread__zput(etmq->thread);
        cs_etm_decoder__free(etmq->decoder);
        zfree(&etmq->event_buf);
        zfree(&etmq->chain);
        free(etmq);
}

static void cs_etm__free_events(struct perf_session *session)
{
        struct cs_etm_auxtrace *aux = container_of(session->auxtrace,
                                                   struct cs_etm_auxtrace,
                                                   auxtrace);

        struct auxtrace_queues *queues = &(aux->queues);

        unsigned i;

        for (i = 0; i < queues->nr_queues; ++i) {
                cs_etm__free_queue(queues->queue_array[i].priv);
                queues->queue_array[i].priv = 0;
        }

        auxtrace_queues__free(queues);

}

static void cs_etm__free(struct perf_session *session)
{

        size_t i;
        struct int_node *inode, *tmp;
        struct cs_etm_auxtrace *aux = container_of(session->auxtrace,
                                                   struct cs_etm_auxtrace,
                                                   auxtrace);
        auxtrace_heap__free(&aux->heap);
        cs_etm__free_events(session);
        session->auxtrace = NULL;

        /* First remove all traceID/CPU# nodes from the RB tree */
        intlist__for_each_safe(inode, tmp, traceid_list)
                intlist__remove(traceid_list, inode);
        /* Then the RB tree itself */
        intlist__delete(traceid_list);

        //thread__delete(aux->unknown_thread);
        for (i = 0; i < aux->num_cpu; ++i) {
                zfree(&aux->metadata[i]);
        }
        zfree(&aux->metadata);
        free(aux);
}

static void cs_etm__use_buffer_pid_tid(struct cs_etm_queue *etmq,
                                      struct auxtrace_queue *queue,
                                      struct auxtrace_buffer *buffer)
{
        if ((queue->cpu == -1) && (buffer->cpu != -1)) {
                etmq->cpu = buffer->cpu;
        }

        etmq->pid = buffer->pid;
        etmq->tid = buffer->tid;

        thread__zput(etmq->thread);

        if (etmq->tid != -1) {
                if (etmq->pid != -1) {
                        etmq->thread = machine__findnew_thread(etmq->etm->machine,
                                                               etmq->pid,
                                                               etmq->tid);
                } else {
                        etmq->thread = machine__findnew_thread(etmq->etm->machine,
                                                               -1,
                                                               etmq->tid);
                }
        }
}


static int cs_etm__get_trace(struct cs_etm_buffer *buff, struct cs_etm_queue *etmq)
{
        struct auxtrace_buffer *aux_buffer = etmq->buffer;
        struct auxtrace_buffer *old_buffer = aux_buffer;
        struct auxtrace_queue *queue;

        if (etmq->stop) {
                buff->len = 0;
                return 0;
        }

        queue = &etmq->etm->queues.queue_array[etmq->queue_nr];

        aux_buffer = auxtrace_buffer__next(queue,aux_buffer);

        if (!aux_buffer) {
                if (old_buffer) {
                        auxtrace_buffer__drop_data(old_buffer);
                }
                buff->len = 0;
                return 0;
        }

        etmq->buffer = aux_buffer;

        if (!aux_buffer->data) {
                int fd = perf_data_file__fd(etmq->etm->session->file);

                aux_buffer->data = auxtrace_buffer__get_data(aux_buffer, fd);
                if (!aux_buffer->data)
                        return -ENOMEM;
        }

        if (old_buffer)
                auxtrace_buffer__drop_data(old_buffer);

        if (aux_buffer->use_data) {
                buff->offset = aux_buffer->offset;
                buff->len = aux_buffer->use_size;
                buff->buf = aux_buffer->use_data;
        } else {
                buff->offset = aux_buffer->offset;
                buff->len = aux_buffer->size;
                buff->buf = aux_buffer->data;
        }
        /*
        buff->offset = 0;
        buff->len = sizeof(cstrace);
        buff->buf = cstrace;
        */

        buff->ref_timestamp = aux_buffer->reference;

        if (etmq->use_buffer_pid_tid && 
            ((etmq->pid != aux_buffer->pid) || 
             (etmq->tid != aux_buffer->tid))) {
                cs_etm__use_buffer_pid_tid(etmq,queue,aux_buffer);
        }

        if (etmq->step_through_buffers)
                etmq->stop = true;

        return buff->len;
}

static struct cs_etm_queue *cs_etm__alloc_queue(struct cs_etm_auxtrace *etm,
                                               unsigned int queue_nr)
{
        struct cs_etm_decoder_params d_params;
        struct cs_etm_trace_params   *t_params;
        struct cs_etm_queue *etmq;
        size_t i;

        etmq = zalloc(sizeof(struct cs_etm_queue));
        if (!etmq)
                return NULL;

        if (etm->synth_opts.callchain) {
                size_t sz = sizeof(struct ip_callchain);

                sz += etm->synth_opts.callchain_sz * sizeof(u64);
                etmq->chain = zalloc(sz);
                if (!etmq->chain)
                        goto out_free;
        } else {
                etmq->chain = NULL;
        }

        etmq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
        if (!etmq->event_buf)
                goto out_free;

        etmq->etm = etm;
        etmq->queue_nr = queue_nr;
        etmq->pid = -1;
        etmq->tid = -1;
        etmq->cpu = -1;
        etmq->stop = false;
        etmq->kernel_mapped = false;

        t_params = zalloc(sizeof(struct cs_etm_trace_params)*etm->num_cpu);

        for (i = 0; i < etm->num_cpu; ++i) {
                t_params[i].reg_idr0 = etm->metadata[i][CS_ETMV4_TRCIDR0];
                t_params[i].reg_idr1 = etm->metadata[i][CS_ETMV4_TRCIDR1];
                t_params[i].reg_idr2 = etm->metadata[i][CS_ETMV4_TRCIDR2];
                t_params[i].reg_idr8 = etm->metadata[i][CS_ETMV4_TRCIDR8];
                t_params[i].reg_configr = etm->metadata[i][CS_ETMV4_TRCCONFIGR];
                t_params[i].reg_traceidr = etm->metadata[i][CS_ETMV4_TRCTRACEIDR];
                t_params[i].protocol = CS_ETM_PROTO_ETMV4i;
        }
        d_params.packet_printer = cs_etm__packet_dump;
        d_params.operation = CS_ETM_OPERATION_DECODE;    
        d_params.formatted = true;
        d_params.fsyncs = false;
        d_params.hsyncs = false;
        d_params.frame_aligned = true;
        d_params.data = etmq;

        etmq->decoder = cs_etm_decoder__new(etm->num_cpu,&d_params,t_params);


        zfree(&t_params);

        if (!etmq->decoder)
                goto out_free;

        etmq->offset = 0;
        etmq->eot = false;

        return etmq;

out_free:
        zfree(&etmq->event_buf);
        zfree(&etmq->chain);
        free(etmq);
        return NULL;
}

static int cs_etm__setup_queue(struct cs_etm_auxtrace *etm, 
                              struct auxtrace_queue *queue,
                              unsigned int queue_nr)
{
        struct cs_etm_queue *etmq = queue->priv;

        if (list_empty(&(queue->head))) 
                return 0;

        if (etmq == NULL) {
                etmq = cs_etm__alloc_queue(etm,queue_nr);

                if (etmq == NULL) {
                        return -ENOMEM;
                }

                queue->priv = etmq;

                if (queue->cpu != -1) {
                        etmq->cpu = queue->cpu;
                }

                etmq->tid = queue->tid;

                if (etm->sampling_mode) {
                        if (etm->timeless_decoding)
                                etmq->step_through_buffers = true;
                        if (etm->timeless_decoding || !etm->have_sched_switch)
                                etmq->use_buffer_pid_tid = true;
                }
        }
        
        if (!etmq->on_heap && 
            (!etm->sync_switch)) {
                const struct cs_etm_state *state;
                int ret = 0;

                if (etm->timeless_decoding)
                        return ret;

                //cs_etm__log("queue %u getting timestamp\n",queue_nr);
                //cs_etm__log("queue %u decoding cpu %d pid %d tid %d\n",
                           //queue_nr, etmq->cpu, etmq->pid, etmq->tid);
                (void) state;
                return ret;
                /*
                while (1) {
                        state = cs_etm_decoder__decode(etmq->decoder);
                        if (state->err) {
                                if (state->err == CS_ETM_ERR_NODATA) {
                                        //cs_etm__log("queue %u has no timestamp\n",
                                                   //queue_nr);
                                        return 0;
                                }
                                continue;
                        }
                        if (state->timestamp)
                                break;
                }

                etmq->timestamp = state->timestamp;
                //cs_etm__log("queue %u timestamp 0x%"PRIx64 "\n",
                           //queue_nr, etmq->timestamp);
                etmq->state = state;
                etmq->have_sample = true;
                //cs_etm__sample_flags(etmq);
                ret = auxtrace_heap__add(&etm->heap, queue_nr, etmq->timestamp);
                if (ret)
                        return ret;
                etmq->on_heap = true;
                */
        }
        
        return 0;
}


static int cs_etm__setup_queues(struct cs_etm_auxtrace *etm)
{
        unsigned int i;
        int ret;

        for (i = 0; i < etm->queues.nr_queues; i++) {
                ret = cs_etm__setup_queue(etm, &(etm->queues.queue_array[i]),i);
                if (ret)
                        return ret;
        }
        return 0;
}

#if 0
struct cs_etm_cache_entry {
        struct auxtrace_cache_entry     entry;
        uint64_t                        icount;
        uint64_t                        bcount;
};

static size_t cs_etm__cache_divisor(void)
{
        static size_t d = 64;

        return d;
}

static size_t cs_etm__cache_size(struct dso *dso,
                                struct machine *machine)
{
        off_t size;

        size = dso__data_size(dso,machine);
        size /= cs_etm__cache_divisor();

        if (size < 1000) 
                return 10;

        if (size > (1 << 21)) 
                return 21;

        return 32 - __builtin_clz(size);
}

static struct auxtrace_cache *cs_etm__cache(struct dso *dso,
                                           struct machine *machine)
{
        struct auxtrace_cache *c;
        size_t bits;

        if (dso->auxtrace_cache)
                return dso->auxtrace_cache;

        bits = cs_etm__cache_size(dso,machine);

        c = auxtrace_cache__new(bits, sizeof(struct cs_etm_cache_entry), 200);

        dso->auxtrace_cache = c;

        return c;
}

static int cs_etm__cache_add(struct dso *dso, struct machine *machine,
                            uint64_t offset, uint64_t icount, uint64_t bcount)
{
        struct auxtrace_cache *c = cs_etm__cache(dso, machine);
        struct cs_etm_cache_entry *e;
        int err;

        if (!c)
                return -ENOMEM;

        e = auxtrace_cache__alloc_entry(c);
        if (!e)
                return -ENOMEM;

        e->icount = icount;
        e->bcount = bcount;

        err = auxtrace_cache__add(c, offset, &e->entry);

        if (err)
                auxtrace_cache__free_entry(c, e);

        return err;
}

static struct cs_etm_cache_entry *cs_etm__cache_lookup(struct dso *dso,
                                                      struct machine *machine,
                                                      uint64_t offset)
{
        struct auxtrace_cache *c = cs_etm__cache(dso, machine);

        if (!c)
                return NULL;

        return auxtrace_cache__lookup(dso->auxtrace_cache, offset);
}
#endif

static int cs_etm__synth_instruction_sample(struct cs_etm_queue *etmq,
                                           struct cs_etm_packet *packet)
{
        int ret = 0;
        struct cs_etm_auxtrace *etm = etmq->etm;
        union perf_event *event = etmq->event_buf;
        struct perf_sample sample = {.ip = 0,};
        uint64_t start_addr = packet->start_addr;
        uint64_t end_addr = packet->end_addr;

        event->sample.header.type = PERF_RECORD_SAMPLE;
        event->sample.header.misc = PERF_RECORD_MISC_USER;
        event->sample.header.size = sizeof(struct perf_event_header);


        sample.ip = start_addr;
        sample.pid = etmq->pid;
        sample.tid = etmq->tid;
        sample.addr = end_addr;
        sample.id = etmq->etm->instructions_id;
        sample.stream_id = etmq->etm->instructions_id;
        sample.period = (end_addr - start_addr) >> 2; 
        sample.cpu = packet->cpu;
        sample.flags = 0; // etmq->flags;
        sample.insn_len = 1; // etmq->insn_len;

        //etmq->last_insn_cnt = etmq->state->tot_insn_cnt;

#if 0
        {
                struct   addr_location al;
                uint64_t offset;
                struct   thread *thread;
                struct   machine *machine = etmq->etm->machine;
                uint8_t  cpumode;
                struct   cs_etm_cache_entry *e;
                uint8_t  buf[256];
                size_t   bufsz;

                thread = etmq->thread;

                if (!thread) {
                        thread = etmq->etm->unknown_thread;
                }

                if (start_addr > 0xffffffc000000000UL) {
                        cpumode = PERF_RECORD_MISC_KERNEL;
                } else {
                        cpumode = PERF_RECORD_MISC_USER;
                }

                thread__find_addr_map(thread, cpumode, MAP__FUNCTION, start_addr,&al);
                if (!al.map || !al.map->dso) {
                        goto endTest;
                }
                if (al.map->dso->data.status == DSO_DATA_STATUS_ERROR &&
                    dso__data_status_seen(al.map->dso,DSO_DATA_STATUS_SEEN_ITRACE)) {
                        goto endTest;
                }

                offset = al.map->map_ip(al.map,start_addr);


                e = cs_etm__cache_lookup(al.map->dso, machine, offset);

                if (e) {
                  (void) e;
                } else {
                        int len;
                        map__load(al.map, machine->symbol_filter);

                        bufsz = sizeof(buf);
                        len = dso__data_read_offset(al.map->dso, machine,
                                                    offset, buf, bufsz);

                        if (len <= 0) {
                                goto endTest;
                        }

                        cs_etm__cache_add(al.map->dso, machine, offset, (end_addr - start_addr) >> 2, end_addr - start_addr);

                }
endTest:
                (void) offset;
        }
#endif

        ret = perf_session__deliver_synth_event(etm->session,event, &sample);

        if (ret) {
                pr_err("CS ETM Trace: failed to deliver instruction event, error %d\n", ret);

        }
        return ret;
}

struct cs_etm_synth {
        struct perf_tool dummy_tool;
        struct perf_session *session;
};


static int cs_etm__event_synth(struct perf_tool *tool,
                              union perf_event *event,
                              struct perf_sample *sample,
                              struct machine *machine)
{
        struct cs_etm_synth *cs_etm_synth =
                      container_of(tool, struct cs_etm_synth, dummy_tool);

        (void) sample;
        (void) machine;

        return perf_session__deliver_synth_event(cs_etm_synth->session, event, NULL);

}


static int cs_etm__synth_event(struct perf_session *session,
                              struct perf_event_attr *attr, u64 id)
{
        struct cs_etm_synth cs_etm_synth;

        memset(&cs_etm_synth, 0, sizeof(struct cs_etm_synth));
        cs_etm_synth.session = session;

        return perf_event__synthesize_attr(&cs_etm_synth.dummy_tool, attr, 1,
                                           &id, cs_etm__event_synth);
}

static int cs_etm__synth_events(struct cs_etm_auxtrace *etm, 
                               struct perf_session *session)
{
        struct perf_evlist *evlist = session->evlist;
        struct perf_evsel *evsel;
        struct perf_event_attr attr;
        bool found = false;
        u64 id;
        int err;

        evlist__for_each(evlist, evsel) {

                if (evsel->attr.type == etm->pmu_type) {
                        found = true;
                        break;
                }
        }

        if (!found) {
                pr_debug("There are no selected events with Core Sight Trace data\n");
                return 0;
        }

        memset(&attr, 0, sizeof(struct perf_event_attr));
        attr.size = sizeof(struct perf_event_attr);
        attr.type = PERF_TYPE_HARDWARE;
        attr.sample_type = evsel->attr.sample_type & PERF_SAMPLE_MASK;
        attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
                            PERF_SAMPLE_PERIOD;
        if (etm->timeless_decoding) 
                attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
        else
                attr.sample_type |= PERF_SAMPLE_TIME;

        attr.exclude_user = evsel->attr.exclude_user;
        attr.exclude_kernel = evsel->attr.exclude_kernel;
        attr.exclude_hv = evsel->attr.exclude_hv;
        attr.exclude_host = evsel->attr.exclude_host;
        attr.exclude_guest = evsel->attr.exclude_guest;
        attr.sample_id_all = evsel->attr.sample_id_all;
        attr.read_format = evsel->attr.read_format;

        id = evsel->id[0] + 1000000000;

        if (!id)
                id = 1;

        if (etm->synth_opts.instructions) {
                attr.config = PERF_COUNT_HW_INSTRUCTIONS;
                attr.sample_period = etm->synth_opts.period;
                etm->instructions_sample_period = attr.sample_period;
                err = cs_etm__synth_event(session, &attr, id);

                if (err) {
                        pr_err("%s: failed to synthesize 'instructions' event type\n",
                               __func__);
                        return err;
                }
                etm->sample_instructions = true;
                etm->instructions_sample_type = attr.sample_type;
                etm->instructions_id = id;
                id += 1;
        }

        etm->synth_needs_swap = evsel->needs_swap;
        return 0;
}

static int cs_etm__sample(struct cs_etm_queue *etmq)
{
        //const struct cs_etm_state *state = etmq->state;
        struct cs_etm_packet packet;
        //struct cs_etm_auxtrace *etm = etmq->etm;
        int err;

        err = cs_etm_decoder__get_packet(etmq->decoder,&packet);
        // if there is no sample, it returns err = -1, no real error

        if (!err && packet.sample_type & CS_ETM_RANGE) {
                err = cs_etm__synth_instruction_sample(etmq,&packet);
                if (err)
                        return err;
        }
        return 0;
}

static int cs_etm__run_decoder(struct cs_etm_queue *etmq, u64 *timestamp)
{
        struct cs_etm_buffer buffer;
        size_t buffer_used;
        int err = 0;

	/* Go through each buffer in the queue and decode them one by one */
more:
        buffer_used = 0;
        memset(&buffer, 0, sizeof(buffer));
        err = cs_etm__get_trace(&buffer,etmq);
        if (err <= 0)
                return err;

        do {
            size_t processed = 0;
            etmq->state = cs_etm_decoder__process_data_block(etmq->decoder,
                                               etmq->offset,
                                               &buffer.buf[buffer_used],
                                               buffer.len-buffer_used,
                                               &processed);
            err = etmq->state->err;
            etmq->offset += processed;
            buffer_used += processed;
            if (!err)
                cs_etm__sample(etmq);
        } while (!etmq->eot && (buffer.len > buffer_used));
goto more;

        (void) timestamp;

        return err;
}

static int cs_etm__update_queues(struct cs_etm_auxtrace *etm)
{
  if (etm->queues.new_data) {
        etm->queues.new_data = false;
        return cs_etm__setup_queues(etm);
  }
  return 0;
}

static int cs_etm__process_queues(struct cs_etm_auxtrace *etm, u64 timestamp)
{
        unsigned int queue_nr;
        u64 ts;
        int ret;

        while (1) {
                struct auxtrace_queue *queue;
                struct cs_etm_queue *etmq;
        
                if (!etm->heap.heap_cnt)
                        return 0;
        
                if (etm->heap.heap_array[0].ordinal >= timestamp)
                        return 0;
        
                queue_nr = etm->heap.heap_array[0].queue_nr;
                queue = &etm->queues.queue_array[queue_nr];
                etmq = queue->priv;
        
                //cs_etm__log("queue %u processing 0x%" PRIx64 " to 0x%" PRIx64 "\n",
                           //queue_nr, etm->heap.heap_array[0].ordinal,
                           //timestamp);

                auxtrace_heap__pop(&etm->heap);

                if (etm->heap.heap_cnt) {
                        ts = etm->heap.heap_array[0].ordinal + 1;
                        if (ts > timestamp)
                                ts = timestamp;
                } else {
                        ts = timestamp;
                }

                cs_etm__set_pid_tid_cpu(etm, queue);

                ret = cs_etm__run_decoder(etmq, &ts);

                if (ret < 0) {
                        auxtrace_heap__add(&etm->heap, queue_nr, ts);
                        return ret;
                }

                if (!ret) {
                        ret = auxtrace_heap__add(&etm->heap, queue_nr, ts);
                        if (ret < 0)
                                return ret;
                } else {
                        etmq->on_heap = false;
                }
        }
        return 0;
}

static int cs_etm__process_timeless_queues(struct cs_etm_auxtrace *etm,
                                          pid_t tid,
                                          u64 time_)
{
        struct auxtrace_queues *queues = &etm->queues;
        unsigned int i;
        u64 ts = 0;
        
        for (i = 0; i < queues->nr_queues; ++i) {
                struct auxtrace_queue *queue = &(etm->queues.queue_array[i]);
                struct cs_etm_queue *etmq = queue->priv;

                if (etmq && ((tid == -1) || (etmq->tid == tid))) {
                        etmq->time = time_;
                        cs_etm__set_pid_tid_cpu(etm, queue);
                        cs_etm__run_decoder(etmq,&ts);

                }
        }
        return 0;
}

static struct cs_etm_queue *cs_etm__cpu_to_etmq(struct cs_etm_auxtrace *etm, 
                                               int cpu)
{
        unsigned q,j;

        if (etm->queues.nr_queues == 0)
                return NULL;

	if (cpu < 0)
		q = 0;
        else if ((unsigned) cpu >= etm->queues.nr_queues)
                q = etm->queues.nr_queues - 1;
        else 
                q = cpu;

        if (etm->queues.queue_array[q].cpu == cpu)
                return etm->queues.queue_array[q].priv;

        for (j = 0; q > 0; j++) {
                if (etm->queues.queue_array[--q].cpu == cpu)
                        return etm->queues.queue_array[q].priv;
        }

        for (; j < etm->queues.nr_queues; j++) {
                if (etm->queues.queue_array[j].cpu == cpu)
                        return etm->queues.queue_array[j].priv;

        }

        return NULL;
}

static uint32_t cs_etm__mem_access(struct cs_etm_queue *etmq, uint64_t address, size_t size, uint8_t *buffer)
{
        struct   addr_location al;
        uint64_t offset;
        struct   thread *thread;
        struct   machine *machine;
        uint8_t  cpumode;
        int len;

        if (etmq == NULL)
                return -1;

        machine = etmq->etm->machine;
        thread = etmq->thread;
        if (address > 0xffffffc000000000UL) {
                cpumode = PERF_RECORD_MISC_KERNEL;
        } else {
                cpumode = PERF_RECORD_MISC_USER;
        }

        thread__find_addr_map(thread, cpumode, MAP__FUNCTION, address,&al);

        if (!al.map || !al.map->dso) {
                return 0;
        }

        if (al.map->dso->data.status == DSO_DATA_STATUS_ERROR &&
            dso__data_status_seen(al.map->dso,DSO_DATA_STATUS_SEEN_ITRACE)) {
                return 0;
        }

        offset = al.map->map_ip(al.map,address);

        map__load(al.map, machine->symbol_filter);

        len = dso__data_read_offset(al.map->dso, machine,
                                    offset, buffer, size);

        if (len <= 0) {
                return 0;
        }

        return len;
}

static bool check_need_swap(int file_endian)
{
	const int data = 1;
	u8 *check = (u8 *)&data;
	int host_endian;

	if (check[0] == 1)
		host_endian = ELFDATA2LSB;
	else
		host_endian = ELFDATA2MSB;

	return host_endian != file_endian;
}

static int cs_etm__read_elf_info(const char *fname, uint64_t *foffset, uint64_t *fstart, uint64_t *fsize)
{
	FILE *fp;
        u8 e_ident[EI_NIDENT];
	int ret = -1;
	bool need_swap = false;
	size_t buf_size;
	void *buf;
	int i;

	fp = fopen(fname, "r");
	if (fp == NULL)
		return -1;

	if (fread(e_ident, sizeof(e_ident), 1, fp) != 1)
		goto out;

	if (memcmp(e_ident, ELFMAG, SELFMAG) ||
	    e_ident[EI_VERSION] != EV_CURRENT)
		goto out;

	need_swap = check_need_swap(e_ident[EI_DATA]);

	/* for simplicity */
	fseek(fp, 0, SEEK_SET);

	if (e_ident[EI_CLASS] == ELFCLASS32) {
		Elf32_Ehdr ehdr;
		Elf32_Phdr *phdr;

		if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1)
			goto out;

		if (need_swap) {
			ehdr.e_phoff = bswap_32(ehdr.e_phoff);
			ehdr.e_phentsize = bswap_16(ehdr.e_phentsize);
			ehdr.e_phnum = bswap_16(ehdr.e_phnum);
		}

		buf_size = ehdr.e_phentsize * ehdr.e_phnum;
		buf = malloc(buf_size);
		if (buf == NULL)
			goto out;

		fseek(fp, ehdr.e_phoff, SEEK_SET);
		if (fread(buf, buf_size, 1, fp) != 1)
			goto out_free;

		for (i = 0, phdr = buf; i < ehdr.e_phnum; i++, phdr++) {

			if (need_swap) {
				phdr->p_type = bswap_32(phdr->p_type);
				phdr->p_offset = bswap_32(phdr->p_offset);
				phdr->p_filesz = bswap_32(phdr->p_filesz);
			}

			if (phdr->p_type != PT_LOAD)
				continue;

                        *foffset = phdr->p_offset;
                        *fstart = phdr->p_vaddr;
                        *fsize = phdr->p_filesz;
                        ret = 0;
                        break;
		}
	} else {
		Elf64_Ehdr ehdr;
		Elf64_Phdr *phdr;

		if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1)
			goto out;

		if (need_swap) {
			ehdr.e_phoff = bswap_64(ehdr.e_phoff);
			ehdr.e_phentsize = bswap_16(ehdr.e_phentsize);
			ehdr.e_phnum = bswap_16(ehdr.e_phnum);
		}

		buf_size = ehdr.e_phentsize * ehdr.e_phnum;
		buf = malloc(buf_size);
		if (buf == NULL)
			goto out;

		fseek(fp, ehdr.e_phoff, SEEK_SET);
		if (fread(buf, buf_size, 1, fp) != 1)
			goto out_free;

		for (i = 0, phdr = buf; i < ehdr.e_phnum; i++, phdr++) {

			if (need_swap) {
				phdr->p_type = bswap_32(phdr->p_type);
				phdr->p_offset = bswap_64(phdr->p_offset);
				phdr->p_filesz = bswap_64(phdr->p_filesz);
			}

			if (phdr->p_type != PT_LOAD)
				continue;

                        *foffset = phdr->p_offset;
                        *fstart = phdr->p_vaddr;
                        *fsize = phdr->p_filesz;
                        ret = 0;
                        break;
		}
	}
out_free:
	free(buf);
out:
	fclose(fp);
	return ret;
}

static int cs_etm__process_event(struct perf_session *session,
                                union perf_event *event,
                                struct perf_sample *sample,
                                struct perf_tool *tool)
{
        struct cs_etm_auxtrace *etm = container_of(session->auxtrace,
                                                   struct cs_etm_auxtrace,
                                                   auxtrace);

        u64 timestamp;
        int err = 0;

        if (dump_trace) 
                return 0;

        if (!tool->ordered_events) {
                pr_err("CoreSight ETM Trace requires ordered events\n");
                return -EINVAL;
        }

        if (sample->time && (sample->time != (u64)-1))
                timestamp = sample->time;
        else
                timestamp = 0;

        if (timestamp || etm->timeless_decoding) {
                err = cs_etm__update_queues(etm);
                if (err)
                        return err;

        }

        if (event->header.type == PERF_RECORD_MMAP2) {
                struct dso *dso;
                int cpu;
                struct cs_etm_queue *etmq;

                cpu = sample->cpu;

                etmq = cs_etm__cpu_to_etmq(etm,cpu);

                if (!etmq) {
                        return -1;
                }

                dso = dsos__find(&(etm->machine->dsos),event->mmap2.filename,false);
                if (NULL != dso) {
                        err = cs_etm_decoder__add_mem_access_cb(
                            etmq->decoder,
                            event->mmap2.start, 
                            event->mmap2.len, 
                            cs_etm__mem_access);
                }

                if ((symbol_conf.vmlinux_name != NULL) && (!etmq->kernel_mapped)) {
                        uint64_t foffset;
                        uint64_t fstart;
                        uint64_t fsize;

                        err = cs_etm__read_elf_info(symbol_conf.vmlinux_name,
                                                      &foffset,&fstart,&fsize);

                        if (!err) {
                                cs_etm_decoder__add_bin_file(
                                        etmq->decoder,
                                        foffset,
                                        fstart,
                                        fsize & ~0x1ULL,
                                        symbol_conf.vmlinux_name);

                                etmq->kernel_mapped = true;
                        }
                }

        }

        if (etm->timeless_decoding) {
                if (event->header.type == PERF_RECORD_EXIT) {
                        err = cs_etm__process_timeless_queues(etm,
                                                             event->fork.tid,
                                                             sample->time);
                }
        } else if (timestamp) {
                err = cs_etm__process_queues(etm, timestamp);
        }

        //cs_etm__log("event %s (%u): cpu %d time%"PRIu64" tsc %#"PRIx64"\n",
                   //perf_event__name(event->header.type), event->header.type,
                   //sample->cpu, sample->time, timestamp);
        return err;
}

static int cs_etm__process_auxtrace_event(struct perf_session *session,
                                  union perf_event *event,
                                  struct perf_tool *tool)
{
        struct cs_etm_auxtrace *etm = container_of(session->auxtrace,
                                                   struct cs_etm_auxtrace,
                                                   auxtrace);

        (void) tool;

        if (!etm->data_queued) {
                struct auxtrace_buffer *buffer;
                off_t  data_offset;
                int fd = perf_data_file__fd(session->file);
                bool is_pipe = perf_data_file__is_pipe(session->file);
                int err;

                if (is_pipe) {
                        data_offset = 0;
                } else {
                        data_offset = lseek(fd, 0, SEEK_CUR);
                        if (data_offset == -1) {
                                return -errno;
                        }
                }

                err = auxtrace_queues__add_event(&etm->queues,
                                                 session,
                                                 event,
                                                 data_offset,
                                                 &buffer);
                if (err)
                        return err;

                if (dump_trace)
                {
                        if (auxtrace_buffer__get_data(buffer,fd)) {
                                cs_etm__dump_event(etm,buffer);
                                auxtrace_buffer__put_data(buffer);
                        }
                }
        } 

        return 0;

}

static const char * const cs_etm_global_header_fmts[] = {
  [CS_HEADER_VERSION_0]    = "   Header version                 %"PRIx64"\n",
  [CS_PMU_TYPE_CPUS]       = "   PMU type/num cpus              %"PRIx64"\n",
  [CS_ETM_SNAPSHOT]        = "   Snapshot                       %"PRIx64"\n",
};

static const char * const cs_etm_priv_fmts[] = {
  [CS_ETM_MAGIC]           = "   Magic number                   %"PRIx64"\n",
  [CS_ETM_CPU]             = "   CPU                            %"PRIx64"\n",
  [CS_ETM_ETMCR]           = "   ETMCR                          %"PRIx64"\n",
  [CS_ETM_ETMTRACEIDR]     = "   ETMTRACEIDR                    %"PRIx64"\n",
  [CS_ETM_ETMCCER]         = "   ETMCCER                        %"PRIx64"\n",
  [CS_ETM_ETMIDR]          = "   ETMIDR                         %"PRIx64"\n",
};

static const char * const cs_etmv4_priv_fmts[] = {
  [CS_ETM_MAGIC]           = "   Magic number                   %"PRIx64"\n",
  [CS_ETM_CPU]             = "   CPU                            %"PRIx64"\n",
  [CS_ETMV4_TRCCONFIGR]    = "   TRCCONFIGR                     %"PRIx64"\n",
  [CS_ETMV4_TRCTRACEIDR]   = "   TRCTRACEIDR                    %"PRIx64"\n",
  [CS_ETMV4_TRCIDR0]       = "   TRCIDR0                        %"PRIx64"\n",
  [CS_ETMV4_TRCIDR1]       = "   TRCIDR1                        %"PRIx64"\n",
  [CS_ETMV4_TRCIDR2]       = "   TRCIDR2                        %"PRIx64"\n",
  [CS_ETMV4_TRCIDR8]       = "   TRCIDR8                        %"PRIx64"\n",
  [CS_ETMV4_TRCAUTHSTATUS] = "   TRCAUTHSTATUS                  %"PRIx64"\n",
};

static void cs_etm__print_auxtrace_info(u64 *val, size_t num)
{
        unsigned i,j,cpu;

        for (i = 0, cpu = 0; cpu < num; ++cpu) {

                if (val[i] == __perf_cs_etmv3_magic) {
                        for (j = 0; j < CS_ETM_PRIV_MAX; ++j, ++i) {
                                fprintf(stdout,cs_etm_priv_fmts[j],val[i]);
                        }
                } else if (val[i] == __perf_cs_etmv4_magic) {
                        for (j = 0; j < CS_ETMV4_PRIV_MAX; ++j, ++i) {
                                fprintf(stdout,cs_etmv4_priv_fmts[j],val[i]);
                        }
                } else {
                        // failure.. return
                        return;
                }
        }
}

int cs_etm__process_auxtrace_info(union perf_event *event,
                                 struct perf_session *session)
{
        struct auxtrace_info_event *auxtrace_info = &(event->auxtrace_info);
        size_t event_header_size = sizeof(struct perf_event_header);
        size_t info_header_size = 8;
        size_t total_size = auxtrace_info->header.size;
        size_t priv_size = 0;
        size_t num_cpu;
        struct cs_etm_auxtrace *etm = 0;
        int err = 0, idx = -1;
        u64 *ptr;
        u64 *hdr = NULL;
        u64 **metadata = NULL;
        size_t i,j,k;
        unsigned pmu_type;
        struct int_node *inode;

        /*
         * sizeof(auxtrace_info_event::type) +
         * sizeof(auxtrace_info_event::reserved) == 8
         */
        info_header_size = 8;

        if (total_size < (event_header_size + info_header_size))
                return -EINVAL;

        priv_size = total_size - event_header_size - info_header_size;

        // First the global part

        ptr = (u64 *) auxtrace_info->priv;
        if (ptr[0] == 0) {
                hdr = zalloc(sizeof(u64 *) * CS_HEADER_VERSION_0_MAX);
                if (hdr == NULL) {
                        return -EINVAL;
                }
                for (i = 0; i < CS_HEADER_VERSION_0_MAX; ++i) {
                        hdr[i] = ptr[i];
                }
                num_cpu = hdr[CS_PMU_TYPE_CPUS] & 0xffffffff;
                pmu_type = (unsigned) ((hdr[CS_PMU_TYPE_CPUS] >> 32) & 0xffffffff);
        } else {
                return -EINVAL;
        }

        /*
         * Create an RB tree for traceID-CPU# tuple.  Since the conversion has
         * to be made for each packet that gets decoded optimizing access in
         * anything other than a sequential array is worth doing.
         */
        traceid_list = intlist__new(NULL);
        if (!traceid_list)
                return -ENOMEM;

        metadata = zalloc(sizeof(u64 *) * num_cpu);
        if (!metadata) {
		err = -ENOMEM;
                goto err_free_traceid_list;
        }

        if (metadata == NULL) {
                return -EINVAL;
        }

        for (j = 0; j < num_cpu; ++j) {
                if (ptr[i] == __perf_cs_etmv3_magic) {
                        metadata[j] = zalloc(sizeof(u64)*CS_ETM_PRIV_MAX);
                        if (metadata == NULL)
                                return -EINVAL;
                        for (k = 0; k < CS_ETM_PRIV_MAX; k++) {
                                metadata[j][k] = ptr[i+k];
                        }

			/* The traceID is our handle */
			idx = metadata[j][CS_ETM_ETMIDR];
                        i += CS_ETM_PRIV_MAX;
                } else if (ptr[i] == __perf_cs_etmv4_magic) {
                        metadata[j] = zalloc(sizeof(u64)*CS_ETMV4_PRIV_MAX);
                        if (metadata == NULL)
                                return -EINVAL;
                        for (k = 0; k < CS_ETMV4_PRIV_MAX; k++) {
                                metadata[j][k] = ptr[i+k];
                        }

			/* The traceID is our handle */
			idx = metadata[j][CS_ETMV4_TRCTRACEIDR];
                        i += CS_ETMV4_PRIV_MAX;
                }

		/* Get an RB node for this CPU */
		inode = intlist__findnew(traceid_list, idx);

		/* Something went wrong, no need to continue */
		if (!inode) {
			err = PTR_ERR(inode);
			goto err_free_metadata;
		}

		/*
		 * The node for that CPU should not have been taken already.
		 * Backout if that's the case.
		 */
		if (inode->priv) {
			err = -EINVAL;
			goto err_free_metadata;
		}

		/* All good, associate the traceID with the CPU# */
		inode->priv = &metadata[j][CS_ETM_CPU];

        }

        if (i*8 != priv_size)
                return -EINVAL;

        if (dump_trace)
                cs_etm__print_auxtrace_info(auxtrace_info->priv,num_cpu);

        etm = zalloc(sizeof(struct cs_etm_auxtrace));

        etm->num_cpu = num_cpu;
        etm->pmu_type = pmu_type;
        etm->snapshot_mode = (hdr[CS_ETM_SNAPSHOT] != 0);

        if (!etm)
                return -ENOMEM;


        err = auxtrace_queues__init(&etm->queues);
        if (err)
                goto err_free;

        etm->unknown_thread = thread__new(999999999,999999999);
        if (etm->unknown_thread == NULL) {
                err = -ENOMEM;
                goto err_free_queues;
        }
        err = thread__set_comm(etm->unknown_thread, "unknown", 0);
        if (err) {
                goto err_delete_thread;
        }

        if (thread__init_map_groups(etm->unknown_thread,
                                    etm->machine)) {
                err = -ENOMEM;
                goto err_delete_thread;
        }

        etm->timeless_decoding = true;
        etm->sampling_mode = false;
        etm->metadata = metadata;
        etm->session = session;
        etm->machine = &session->machines.host;
        etm->auxtrace_type = auxtrace_info->type;

        etm->auxtrace.process_event = cs_etm__process_event;
        etm->auxtrace.process_auxtrace_event = cs_etm__process_auxtrace_event;
        etm->auxtrace.flush_events = cs_etm__flush_events;
        etm->auxtrace.free_events  = cs_etm__free_events;
        etm->auxtrace.free         = cs_etm__free;
        session->auxtrace = &(etm->auxtrace);

        if (dump_trace)
                return 0;

        if (session->itrace_synth_opts && session->itrace_synth_opts->set) {
                etm->synth_opts = *session->itrace_synth_opts;
        } else {
                itrace_synth_opts__set_default(&etm->synth_opts);
        }
        etm->synth_opts.branches = false;
        etm->synth_opts.callchain = false;
        etm->synth_opts.calls = false;
        etm->synth_opts.returns = false;

        err = cs_etm__synth_events(etm, session);
        if (err)
                goto err_delete_thread;

        err = auxtrace_queues__process_index(&etm->queues, session);
        if (err)
                goto err_delete_thread;

        etm->data_queued = etm->queues.populated;

        return 0;

err_delete_thread:
        thread__delete(etm->unknown_thread);
err_free_queues:
        auxtrace_queues__free(&etm->queues);
        session->auxtrace = NULL;
err_free:
        free(etm);
err_free_metadata:
	/* No need to check @metadata[j], free(NULL) is supported */
	for (j = 0; j < num_cpu; ++j)
		free(metadata[j]);
	free(metadata);
err_free_traceid_list:
	intlist__delete(traceid_list);

        return err;
}
