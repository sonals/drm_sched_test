/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@amd.com>
 */

#include <iostream>
#include <string>

#include "sched_test.h"
#include "common.h"

static void usage(const char *cmd)
{
	std::cout << "Usage " << cmd << " [-n <dev_node>]\n";
	throw std::invalid_argument("");
}


void run(const int node, bool release = true)
{
	const schedtest::raii f(node);
	f.showVersion();
	schedtest::syncobj soutobja(f.createSyncobj());
	schedtest::syncobj soutobjb(f.createSyncobj());
	drm_sched_test_submit submitb = {0, soutobjb(), SCHED_TSTQ_B};
	f.callIoctl(DRM_IOCTL_SCHED_TEST_SUBMIT, &submitb);
	drm_sched_test_submit submita = {soutobjb(), soutobja(), SCHED_TSTQ_A};
	f.callIoctl(DRM_IOCTL_SCHED_TEST_SUBMIT, &submita);
	if (release)
		soutobja.wait();
}

static void runAll(const unsigned minor)
{
	std::cout << "Start regular job test..." << std::endl;
	run(minor);
	std::cout << "Finished regular job test..." << std::endl;
	std::cout << "Start auto job cleanup test..." << std::endl;
	run(minor, false);
	std::cout << "Finished auto job cleanup test" << std::endl;
}

int main(int argc, char *argv[])
{
	try {
		unsigned minor = 128;
		char c = '\0';
		while ((c = getopt (argc, argv, "n:")) != -1) {
			switch (c) {
			case 'n':
				minor = std::atoi(optarg);
				break;
			case '?':
			default:
				usage(argv[0]);
			}
		}
		if (optind < argc) {
			usage(argv[0]);
		}

		runAll(minor);

	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
