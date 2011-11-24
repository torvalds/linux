#ifndef _STATE_TRACE_H
#define _STATE_TRACE_H

/* States */
#define TRACE_STATE_UNUSED 0xFF
#define TRACE_STATE_QUEUED_SEND_PACKET 0
#define TRACE_STATE_DEQUEUED_SEND_PACKET 1
#define TRACE_STATE_PROCESSED_SEND_PACKET 2
#define TRACE_STATE_SENDING_SEND_PACKET 3
#define TRACE_STATE_SENT_SEND_PACKET 4
#define TRACE_STATE_SEND_PACKET_COMPLETED 5
#define TRACE_STATE_SEND_CMD_COMPLETED 6
#define TRACE_STATE_DATA_CFM_RECEIVED 7
#define TRACE_STATE_INDICATE_SEND_COMPLETE 8
#define TRACE_STATE_RCV_PACKET_RECEIVED 9
#define TRACE_STATE_PROCESSED_RCV_PACKET 10
#define TRACE_STATE_RECEIVED_BAD_PACKET 11
#define TRACE_STATE_INDICATE_RCV_COMPLETE 12
#define TRACE_STATE_DATA_CFM_RECEIVED_A 13
#define TRACE_STATE_DATA_CFM_RECEIVED_B 14
#define TRACE_STATE_DATA_CFM_RECEIVED_C 15
#define TRACE_STATE_RCV_PACKET_RECEIVED_A 16
#define TRACE_STATE_RCV_PACKET_ABSORBED 17
#define TRACE_STATE_RCV_COMPLETION_EXIT 18
#define TRACE_STATE_TX_COMPLETION_ENTER 19
#define TRACE_STATE_TX_COMPLETION_EXIT 20
#define TRACE_STATE_HANDLE_SEND_ENTER 21
#define TRACE_STATE_HANDLE_SEND_EXIT 22
#define TRACE_STATE_SEND_PACKET_FROM_NDIS 23
#define TRACE_STATE_HANDLE_SEND_EXIT_FAIL 24
#define TRACE_STATE_PROCESS_PROTO_PKT 25
#define TRACE_STATE_INDICATE_SEND_PENDING 26

#define TRACE_LOG_SIZE 64

struct tl {
   char *log;
   char *current_p;
   int size;
   int  initialized;
};

extern struct tl trace_log;

struct th {
      uint32_t trans_ids[32];
      int idx;
};

#if defined(ENABLE_STATE_TRACE)

void init_state_trace(int s);
void deinit_state_trace(void);
void state_trace(char s);

void transid_hist_init(void);
void log_transid(uint32_t id);

#else /* !ENABLE_STATE_TRACE */

#define init_state_trace(s)
#define deinit_state_trace()
#define state_trace(s)
#define transid_hist_init()
#define log_transid(id)

#endif /* ENABLE_STATE_TRACE */

#endif /* _STATE_TRACE_H */
