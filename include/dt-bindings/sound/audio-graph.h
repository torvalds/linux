/* SPDX-License-Identifier: GPL-2.0
 *
 * audio-graph.h
 *
 * Copyright (c) 2024 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef __AUDIO_GRAPH_H
#define __AUDIO_GRAPH_H

/*
 * used in
 *	link-trigger-order
 *	link-trigger-order-start
 *	link-trigger-order-stop
 *
 * default is
 *	link-trigger-order = <SND_SOC_TRIGGER_LINK
 *			      SND_SOC_TRIGGER_COMPONENT
 *			      SND_SOC_TRIGGER_DAI>;
 */
#define SND_SOC_TRIGGER_LINK		0
#define SND_SOC_TRIGGER_COMPONENT	1
#define SND_SOC_TRIGGER_DAI		2
#define SND_SOC_TRIGGER_SIZE		3	/* shoud be last */

#endif /* __AUDIO_GRAPH_H */
