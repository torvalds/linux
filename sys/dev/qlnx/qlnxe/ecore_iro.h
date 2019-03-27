/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __IRO_H__
#define __IRO_H__

/* Ystorm flow control mode. Use enum fw_flow_ctrl_mode */
#define YSTORM_FLOW_CONTROL_MODE_OFFSET                             (IRO[0].base)
#define YSTORM_FLOW_CONTROL_MODE_SIZE                               (IRO[0].size)
/* Tstorm port statistics */
#define TSTORM_PORT_STAT_OFFSET(port_id)                            (IRO[1].base + ((port_id) * IRO[1].m1))
#define TSTORM_PORT_STAT_SIZE                                       (IRO[1].size)
/* Tstorm ll2 port statistics */
#define TSTORM_LL2_PORT_STAT_OFFSET(port_id)                        (IRO[2].base + ((port_id) * IRO[2].m1))
#define TSTORM_LL2_PORT_STAT_SIZE                                   (IRO[2].size)
/* Ustorm VF-PF Channel ready flag */
#define USTORM_VF_PF_CHANNEL_READY_OFFSET(vf_id)                    (IRO[3].base + ((vf_id) * IRO[3].m1))
#define USTORM_VF_PF_CHANNEL_READY_SIZE                             (IRO[3].size)
/* Ustorm Final flr cleanup ack */
#define USTORM_FLR_FINAL_ACK_OFFSET(pf_id)                          (IRO[4].base + ((pf_id) * IRO[4].m1))
#define USTORM_FLR_FINAL_ACK_SIZE                                   (IRO[4].size)
/* Ustorm Event ring consumer */
#define USTORM_EQE_CONS_OFFSET(pf_id)                               (IRO[5].base + ((pf_id) * IRO[5].m1))
#define USTORM_EQE_CONS_SIZE                                        (IRO[5].size)
/* Ustorm eth queue zone */
#define USTORM_ETH_QUEUE_ZONE_OFFSET(queue_zone_id)                 (IRO[6].base + ((queue_zone_id) * IRO[6].m1))
#define USTORM_ETH_QUEUE_ZONE_SIZE                                  (IRO[6].size)
/* Ustorm Common Queue ring consumer */
#define USTORM_COMMON_QUEUE_CONS_OFFSET(queue_zone_id)              (IRO[7].base + ((queue_zone_id) * IRO[7].m1))
#define USTORM_COMMON_QUEUE_CONS_SIZE                               (IRO[7].size)
/* Xstorm Integration Test Data */
#define XSTORM_INTEG_TEST_DATA_OFFSET                               (IRO[8].base)
#define XSTORM_INTEG_TEST_DATA_SIZE                                 (IRO[8].size)
/* Ystorm Integration Test Data */
#define YSTORM_INTEG_TEST_DATA_OFFSET                               (IRO[9].base)
#define YSTORM_INTEG_TEST_DATA_SIZE                                 (IRO[9].size)
/* Pstorm Integration Test Data */
#define PSTORM_INTEG_TEST_DATA_OFFSET                               (IRO[10].base)
#define PSTORM_INTEG_TEST_DATA_SIZE                                 (IRO[10].size)
/* Tstorm Integration Test Data */
#define TSTORM_INTEG_TEST_DATA_OFFSET                               (IRO[11].base)
#define TSTORM_INTEG_TEST_DATA_SIZE                                 (IRO[11].size)
/* Mstorm Integration Test Data */
#define MSTORM_INTEG_TEST_DATA_OFFSET                               (IRO[12].base)
#define MSTORM_INTEG_TEST_DATA_SIZE                                 (IRO[12].size)
/* Ustorm Integration Test Data */
#define USTORM_INTEG_TEST_DATA_OFFSET                               (IRO[13].base)
#define USTORM_INTEG_TEST_DATA_SIZE                                 (IRO[13].size)
/* Tstorm producers */
#define TSTORM_LL2_RX_PRODS_OFFSET(core_rx_queue_id)                (IRO[14].base + ((core_rx_queue_id) * IRO[14].m1))
#define TSTORM_LL2_RX_PRODS_SIZE                                    (IRO[14].size)
/* Tstorm LightL2 queue statistics */
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_OFFSET(core_rx_queue_id)     (IRO[15].base + ((core_rx_queue_id) * IRO[15].m1))
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_SIZE                         (IRO[15].size)
/* Ustorm LiteL2 queue statistics */
#define CORE_LL2_USTORM_PER_QUEUE_STAT_OFFSET(core_rx_queue_id)     (IRO[16].base + ((core_rx_queue_id) * IRO[16].m1))
#define CORE_LL2_USTORM_PER_QUEUE_STAT_SIZE                         (IRO[16].size)
/* Pstorm LiteL2 queue statistics */
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_OFFSET(core_tx_stats_id)     (IRO[17].base + ((core_tx_stats_id) * IRO[17].m1))
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_SIZE                         (IRO[17].size)
/* Mstorm queue statistics */
#define MSTORM_QUEUE_STAT_OFFSET(stat_counter_id)                   (IRO[18].base + ((stat_counter_id) * IRO[18].m1))
#define MSTORM_QUEUE_STAT_SIZE                                      (IRO[18].size)
/* Mstorm ETH PF queues producers */
#define MSTORM_ETH_PF_PRODS_OFFSET(queue_id)                        (IRO[19].base + ((queue_id) * IRO[19].m1))
#define MSTORM_ETH_PF_PRODS_SIZE                                    (IRO[19].size)
/* Mstorm ETH VF queues producers offset in RAM. Used in default VF zone size mode. */
#define MSTORM_ETH_VF_PRODS_OFFSET(vf_id,vf_queue_id)               (IRO[20].base + ((vf_id) * IRO[20].m1) + ((vf_queue_id) * IRO[20].m2))
#define MSTORM_ETH_VF_PRODS_SIZE                                    (IRO[20].size)
/* TPA agregation timeout in us resolution (on ASIC) */
#define MSTORM_TPA_TIMEOUT_US_OFFSET                                (IRO[21].base)
#define MSTORM_TPA_TIMEOUT_US_SIZE                                  (IRO[21].size)
/* Mstorm pf statistics */
#define MSTORM_ETH_PF_STAT_OFFSET(pf_id)                            (IRO[22].base + ((pf_id) * IRO[22].m1))
#define MSTORM_ETH_PF_STAT_SIZE                                     (IRO[22].size)
/* Ustorm queue statistics */
#define USTORM_QUEUE_STAT_OFFSET(stat_counter_id)                   (IRO[23].base + ((stat_counter_id) * IRO[23].m1))
#define USTORM_QUEUE_STAT_SIZE                                      (IRO[23].size)
/* Ustorm pf statistics */
#define USTORM_ETH_PF_STAT_OFFSET(pf_id)                            (IRO[24].base + ((pf_id) * IRO[24].m1))
#define USTORM_ETH_PF_STAT_SIZE                                     (IRO[24].size)
/* Pstorm queue statistics */
#define PSTORM_QUEUE_STAT_OFFSET(stat_counter_id)                   (IRO[25].base + ((stat_counter_id) * IRO[25].m1))
#define PSTORM_QUEUE_STAT_SIZE                                      (IRO[25].size)
/* Pstorm pf statistics */
#define PSTORM_ETH_PF_STAT_OFFSET(pf_id)                            (IRO[26].base + ((pf_id) * IRO[26].m1))
#define PSTORM_ETH_PF_STAT_SIZE                                     (IRO[26].size)
/* Control frame's EthType configuration for TX control frame security */
#define PSTORM_CTL_FRAME_ETHTYPE_OFFSET(ethType_id)                 (IRO[27].base + ((ethType_id) * IRO[27].m1))
#define PSTORM_CTL_FRAME_ETHTYPE_SIZE                               (IRO[27].size)
/* Tstorm last parser message */
#define TSTORM_ETH_PRS_INPUT_OFFSET                                 (IRO[28].base)
#define TSTORM_ETH_PRS_INPUT_SIZE                                   (IRO[28].size)
/* Tstorm Eth limit Rx rate */
#define ETH_RX_RATE_LIMIT_OFFSET(pf_id)                             (IRO[29].base + ((pf_id) * IRO[29].m1))
#define ETH_RX_RATE_LIMIT_SIZE                                      (IRO[29].size)
/* Xstorm queue zone */
#define XSTORM_ETH_QUEUE_ZONE_OFFSET(queue_id)                      (IRO[30].base + ((queue_id) * IRO[30].m1))
#define XSTORM_ETH_QUEUE_ZONE_SIZE                                  (IRO[30].size)
/* Ystorm cqe producer */
#define YSTORM_TOE_CQ_PROD_OFFSET(rss_id)                           (IRO[31].base + ((rss_id) * IRO[31].m1))
#define YSTORM_TOE_CQ_PROD_SIZE                                     (IRO[31].size)
/* Ustorm cqe producer */
#define USTORM_TOE_CQ_PROD_OFFSET(rss_id)                           (IRO[32].base + ((rss_id) * IRO[32].m1))
#define USTORM_TOE_CQ_PROD_SIZE                                     (IRO[32].size)
/* Ustorm grq producer */
#define USTORM_TOE_GRQ_PROD_OFFSET(pf_id)                           (IRO[33].base + ((pf_id) * IRO[33].m1))
#define USTORM_TOE_GRQ_PROD_SIZE                                    (IRO[33].size)
/* Tstorm cmdq-cons of given command queue-id */
#define TSTORM_SCSI_CMDQ_CONS_OFFSET(cmdq_queue_id)                 (IRO[34].base + ((cmdq_queue_id) * IRO[34].m1))
#define TSTORM_SCSI_CMDQ_CONS_SIZE                                  (IRO[34].size)
/* Tstorm (reflects M-Storm) bdq-external-producer of given function ID, BDqueue-id */
#define TSTORM_SCSI_BDQ_EXT_PROD_OFFSET(func_id,bdq_id)             (IRO[35].base + ((func_id) * IRO[35].m1) + ((bdq_id) * IRO[35].m2))
#define TSTORM_SCSI_BDQ_EXT_PROD_SIZE                               (IRO[35].size)
/* Mstorm bdq-external-producer of given BDQ resource ID, BDqueue-id */
#define MSTORM_SCSI_BDQ_EXT_PROD_OFFSET(func_id,bdq_id)             (IRO[36].base + ((func_id) * IRO[36].m1) + ((bdq_id) * IRO[36].m2))
#define MSTORM_SCSI_BDQ_EXT_PROD_SIZE                               (IRO[36].size)
/* Tstorm iSCSI RX stats */
#define TSTORM_ISCSI_RX_STATS_OFFSET(pf_id)                         (IRO[37].base + ((pf_id) * IRO[37].m1))
#define TSTORM_ISCSI_RX_STATS_SIZE                                  (IRO[37].size)
/* Mstorm iSCSI RX stats */
#define MSTORM_ISCSI_RX_STATS_OFFSET(pf_id)                         (IRO[38].base + ((pf_id) * IRO[38].m1))
#define MSTORM_ISCSI_RX_STATS_SIZE                                  (IRO[38].size)
/* Ustorm iSCSI RX stats */
#define USTORM_ISCSI_RX_STATS_OFFSET(pf_id)                         (IRO[39].base + ((pf_id) * IRO[39].m1))
#define USTORM_ISCSI_RX_STATS_SIZE                                  (IRO[39].size)
/* Xstorm iSCSI TX stats */
#define XSTORM_ISCSI_TX_STATS_OFFSET(pf_id)                         (IRO[40].base + ((pf_id) * IRO[40].m1))
#define XSTORM_ISCSI_TX_STATS_SIZE                                  (IRO[40].size)
/* Ystorm iSCSI TX stats */
#define YSTORM_ISCSI_TX_STATS_OFFSET(pf_id)                         (IRO[41].base + ((pf_id) * IRO[41].m1))
#define YSTORM_ISCSI_TX_STATS_SIZE                                  (IRO[41].size)
/* Pstorm iSCSI TX stats */
#define PSTORM_ISCSI_TX_STATS_OFFSET(pf_id)                         (IRO[42].base + ((pf_id) * IRO[42].m1))
#define PSTORM_ISCSI_TX_STATS_SIZE                                  (IRO[42].size)
/* Tstorm FCoE RX stats */
#define TSTORM_FCOE_RX_STATS_OFFSET(pf_id)                          (IRO[43].base + ((pf_id) * IRO[43].m1))
#define TSTORM_FCOE_RX_STATS_SIZE                                   (IRO[43].size)
/* Pstorm FCoE TX stats */
#define PSTORM_FCOE_TX_STATS_OFFSET(pf_id)                          (IRO[44].base + ((pf_id) * IRO[44].m1))
#define PSTORM_FCOE_TX_STATS_SIZE                                   (IRO[44].size)
/* Pstorm RDMA queue statistics */
#define PSTORM_RDMA_QUEUE_STAT_OFFSET(rdma_stat_counter_id)         (IRO[45].base + ((rdma_stat_counter_id) * IRO[45].m1))
#define PSTORM_RDMA_QUEUE_STAT_SIZE                                 (IRO[45].size)
/* Tstorm RDMA queue statistics */
#define TSTORM_RDMA_QUEUE_STAT_OFFSET(rdma_stat_counter_id)         (IRO[46].base + ((rdma_stat_counter_id) * IRO[46].m1))
#define TSTORM_RDMA_QUEUE_STAT_SIZE                                 (IRO[46].size)
/* Xstorm iWARP rxmit stats */
#define XSTORM_IWARP_RXMIT_STATS_OFFSET(pf_id)                      (IRO[47].base + ((pf_id) * IRO[47].m1))
#define XSTORM_IWARP_RXMIT_STATS_SIZE                               (IRO[47].size)
/* Tstorm RoCE Event Statistics */
#define TSTORM_ROCE_EVENTS_STAT_OFFSET(roce_pf_id)                  (IRO[48].base + ((roce_pf_id) * IRO[48].m1))
#define TSTORM_ROCE_EVENTS_STAT_SIZE                                (IRO[48].size)
/* DCQCN Received Statistics */
#define YSTORM_ROCE_DCQCN_RECEIVED_STATS_OFFSET(roce_pf_id)         (IRO[49].base + ((roce_pf_id) * IRO[49].m1))
#define YSTORM_ROCE_DCQCN_RECEIVED_STATS_SIZE                       (IRO[49].size)
/* DCQCN Sent Statistics */
#define PSTORM_ROCE_DCQCN_SENT_STATS_OFFSET(roce_pf_id)             (IRO[50].base + ((roce_pf_id) * IRO[50].m1))
#define PSTORM_ROCE_DCQCN_SENT_STATS_SIZE                           (IRO[50].size)

#endif /* __IRO_H__ */
