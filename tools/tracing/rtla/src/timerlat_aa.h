// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

int timerlat_aa_init(struct osnoise_tool *tool, int dump_task);
void timerlat_aa_destroy(void);

void timerlat_auto_analysis(int irq_thresh, int thread_thresh);
