/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

struct common_params *osnoise_top_parse_args(int argc, char **argv);
struct common_params *osnoise_hist_parse_args(int argc, char **argv);
struct common_params *timerlat_top_parse_args(int argc, char **argv);
struct common_params *timerlat_hist_parse_args(int argc, char **argv);

extern bool in_unit_test;
