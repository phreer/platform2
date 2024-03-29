/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "crosid.h"

#define HELP_MSG                                                                       \
	"Usage: %s [options...]\n"                                                     \
	"\n"                                                                           \
	"Options:\n"                                                                   \
	"  -h, --help         Show this help message and exit\n"                       \
	"  -v, --verbose      Print debug messages to stderr that can help diagnose\n" \
	"                     identity probe errors\n"                                 \
	"  --sysroot=SYSROOT  Specify an alternative root directory for testing\n"

static void print_help(const char *prog_name)
{
	fprintf(stderr, HELP_MSG, prog_name);
}

enum long_only_options {
	OPT_SYSROOT = 0x100,
};

int main(int argc, char *argv[])
{
	bool help_requested = false;
	int opt;
	int log_level = 0;
	struct option long_opts[] = {
		{
			.name = "help",
			.val = 'h',
		},
		{
			.name = "verbose",
			.val = 'v',
		},
		{
			.name = "sysroot",
			.has_arg = required_argument,
			.val = OPT_SYSROOT,
		},
	};
	struct crosid_probed_device_data device_data;
	int matched_config_index;

	while ((opt = getopt_long(argc, argv, ":hv", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'h':
			help_requested = true;
			break;
		case 'v':
			log_level++;
			break;
		case OPT_SYSROOT:
			crosid_set_sysroot(optarg);
			break;
		default:
			crosid_log(LOG_ERR, "Unknown argument: %s\n",
				   argv[optind - 1]);
			print_help(argv[0]);
			return 1;
		}
	}

	crosid_set_log_level(log_level);

	if (optind < argc) {
		crosid_log(LOG_ERR, "Unknown argument: %s\n", argv[optind]);
		print_help(argv[0]);
		return 1;
	}

	if (help_requested) {
		print_help(argv[0]);
		return 0;
	}

	if (crosid_probe(&device_data) < 0)
		return 1;

	matched_config_index = crosid_match(&device_data);
	crosid_print_vars(stdout, &device_data, matched_config_index);
	crosid_probe_free(&device_data);
	return matched_config_index < 0;
}
