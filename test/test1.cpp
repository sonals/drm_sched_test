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

#include "sched_test.h"

// g++ -I ../uapi/ test1.cpp -ldrm

static const int LEN = 128;

template <long unsigned int code, typename rec> int ioctlRun(int fd, rec *data, const char *nodeName)
{
	int result = ioctl(fd, code, data);
	if (result < 0) {
		close(fd);
		throw std::system_error(errno, std::generic_category(), nodeName);
	}
	return result;
}

int run(const char *nodeName, unsigned count, bool release = true)
{
	int fd = open(nodeName, O_RDWR);

	if (fd < 0)
		throw std::system_error(errno, std::generic_category(), nodeName);

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

	int result = ioctlRun<DRM_IOCTL_VERSION, drm_version>(fd, &version, nodeName);
	std::cout << version.name << std::endl;

	std::vector<drm_sched_test_submit> submitCmds(count);
	for (int i = 0; i < count; i++) {
		submitCmds[i].fence = 0;
		result = ioctlRun<DRM_IOCTL_SCHED_TEST_SUBMIT, drm_sched_test_submit>(fd, &submitCmds[i], nodeName);
		std::cout << "submit[" << count << "]  fence: " << submitCmds[i].fence << std::endl;
	}

	if (release) {
		for (int i = 0; i < count; i++) {
			drm_sched_test_wait wait = {submitCmds[i].fence, 100};
			result = ioctlRun<DRM_IOCTL_SCHED_TEST_WAIT, drm_sched_test_wait>(fd, &wait, nodeName);
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
		result = run(nodeName, 2, false);
		result = run(nodeName, 2);
		std::cout << "result = " << result << std::endl;
	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
	}
	return result;
}
