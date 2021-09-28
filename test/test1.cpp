/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2021 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#include <drm/drm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>


#include <iostream>
#include <cerrno>
#include <system_error>
#include <memory>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>

#include "sched_test.h"

static const int LEN = 128;

void run(const char *nodeName, int count, bool release = true)
{
	int fd = open(nodeName, O_RDWR);

	if (fd < 0)
		throw std::system_error(errno, std::generic_category(), nodeName);

	auto ioctlLambda = [fd, nodeName](auto code, auto data) {
				   int result = ioctl(fd, code, data);
				   if (result < 0) {
					   close(fd);
					   throw std::system_error(errno, std::generic_category(), nodeName);
				   }
				   return result;
			   };

	drm_version version;
	std::memset(&version, 0, sizeof(version));
	std::unique_ptr<char[]> name(new char[LEN]);
	std::unique_ptr<char[]> desc(new char[LEN]);
	std::unique_ptr<char[]> date(new char[LEN]);

	version.name = name.get();
	version.name_len = LEN;
	version.desc = desc.get();
	version.desc_len = LEN;
	version.date = date.get();
	version.date_len = LEN;

	ioctlLambda(DRM_IOCTL_VERSION, &version);
	std::cout << version.name << std::endl;
	std::cout << version.desc << std::endl;

	std::vector<drm_sched_test_submit> submitCmds(count);
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < count; i++) {
		submitCmds[i].fence = 0;
		ioctlLambda(DRM_IOCTL_SCHED_TEST_SUBMIT, &submitCmds[i]);
	}

	if (release) {
		for (int i = 0; i < count; i++) {
			drm_sched_test_wait wait = {submitCmds[i].fence, 100};
			ioctlLambda(DRM_IOCTL_SCHED_TEST_WAIT, &wait);
		}
	}
	auto end = std::chrono::high_resolution_clock::now();
	double delay = (std::chrono::duration_cast<std::chrono::microseconds>(end - start)).count();
	double iops = ((double)count * 1000000.0)/delay;
	iops /= 1000;
	std::cout << "IOPS: " << iops << " K/s" << std::endl;
	close(fd);
}

int main(int argc, char *argv[])
{
	static const char *nodeName = "/dev/dri/renderD128";
	try {
		int count = 10;
		char c = '\0';
		while ((c = getopt (argc, argv, "n:c:")) != -1) {
			switch (c) {
			case 'n':
				nodeName = optarg;
				break;
			case 'c':
				count = std::atoi(optarg);
				break;
			case '?':
			default:
				std::cout << "Usage " << argv[0] << " [-n <dev_node>] [-c <loop_count>]\n";
				throw std::invalid_argument("");
			}
		}
		if (optind < argc) {
			std::cout << "Usage " << argv[0] << " [-n <dev_node>] [-c <loop_count>]\n";
			throw std::invalid_argument("");
		}

		std::cout << "Start auto job cleanup test..." << std::endl;
		run(nodeName, count, false);
		std::cout << "Finished auto job cleanup test" << std::endl;
		std::cout << "Start regular job test..." << std::endl;
		run(nodeName, count);
		std::cout << "Finished regular job test..." << std::endl;
	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
