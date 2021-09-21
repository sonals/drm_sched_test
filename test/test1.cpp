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
#include <stdexcept>
#include <memory>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>

#include "sched_test.h"

// g++ -I ../uapi/ test1.cpp

static const int LEN = 128;

int run(const char *nodeName, unsigned count, bool release = true)
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

	int result = ioctlLambda(DRM_IOCTL_VERSION, &version);
	std::cout << version.name << std::endl;
	std::cout << version.desc << std::endl;

	std::vector<drm_sched_test_submit> submitCmds(count);
	for (int i = 0; i < count; i++) {
		submitCmds[i].fence = 0;
		result = ioctlLambda(DRM_IOCTL_SCHED_TEST_SUBMIT, &submitCmds[i]);
		std::cout << (release ? "R-" : "A-");
		std::cout << "submit[" << i << "]  fence: " << submitCmds[i].fence << std::endl;
	}

	if (release) {
		for (int i = 0; i < count; i++) {
			drm_sched_test_wait wait = {submitCmds[i].fence, 100};
			result = ioctlLambda(DRM_IOCTL_SCHED_TEST_WAIT, &wait);
			std::cout << (release ? "R-" : "A-");
			std::cout << "wait[" << i << "] result: " << result << std::endl;
		}
	}

	close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	int result = 0;
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

		/*
		 * In the following case application submits work and then tries to exit, hence
		 * forcing the driver to harvest all the unfinished work. This test alone works
		 * fine even with 200K loop
		 */
		result = run(nodeName, count, false);
		/*
		 * Adding this wait when both tests are enabled prevents the crash we see with 100K
		 * loop, otherwise there is panic in the second run.
		 */
		std::this_thread::sleep_for(std::chrono::milliseconds(20000));
		/*
		 * In the following case, application calls wait on each work submitted. This test
		 * alone works fine even with 200K loop
		 */
		result = run(nodeName, count);
		std::cout << "result = " << result << std::endl;
	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
	}
	return result;
}
