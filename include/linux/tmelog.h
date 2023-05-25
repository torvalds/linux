/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _TMELOG_H_
#define _TMELOG_H_

/**
 *  Send data to Tmefw via tmecom interface
 *
 *  @param [in]   buf          Buffer passed by user.
 *  @param [in]   tme_buf_size Size of passed buffer.
 *  @param [out]  buf_size     Size written by Tme FW to passed buffer.
 *
 *  @return  0 if successful, error code otherwise.
 */
int tmelog_process_request(uint32_t buf, uint32_t tme_buf_size, uint32_t *buf_size);

#endif  /*_TMELOG_H_ */
