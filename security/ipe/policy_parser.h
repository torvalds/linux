/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */
#ifndef _IPE_POLICY_PARSER_H
#define _IPE_POLICY_PARSER_H

int ipe_parse_policy(struct ipe_policy *p);
void ipe_free_parsed_policy(struct ipe_parsed_policy *p);

#endif /* _IPE_POLICY_PARSER_H */
