/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2021 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#include <drm/drm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <spawn.h>

#include <iostream>
#include <cerrno>
#include <cstdlib>
#include <system_error>
#include <stdexcept>
#include <memory>
#include <list>
#include <cstring>
#include <chrono>

#include "sched_test.h"

static const int LEN = 128;

void run(const std::string &nodeName, int count)
{
	int fd = open(nodeName.c_str(), O_RDWR);

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

	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < count; i++) {
		drm_sched_test_submit submit = {
			.in = {
				.qu = SCHED_TSTQ_A
			}
		};
		ioctlLambda(DRM_IOCTL_SCHED_TEST_SUBMIT, &submit);
		drm_sched_test_wait wait = {submit.out.fence, 100};
		ioctlLambda(DRM_IOCTL_SCHED_TEST_WAIT, &wait);
	}
	auto end = std::chrono::high_resolution_clock::now();
	double delay = (std::chrono::duration_cast<std::chrono::microseconds>(end - start)).count();
	double iops = ((double)count * 1000000.0)/delay;
	iops /= 1000;
	std::cout << "IOPS: " << iops << " K/s" << std::endl;

	close(fd);
}

static void runJobs(const std::string &nodeName, int count, int jobs, const std::string &cmd)
{
	auto checkLambda = [cmd](int result) {
				   if (result)
					   throw std::system_error(result, std::generic_category(), cmd);
			   };

	const std::string cstr(std::to_string(count));
	posix_spawn_file_actions_t file_actions;
	int result = posix_spawn_file_actions_init(&file_actions);
	checkLambda(result);

	result = posix_spawn_file_actions_addclose(&file_actions,
						   STDIN_FILENO);
	checkLambda(result);

	char * const cargv[6] = {strdup(cmd.c_str()), strdup("-n"), strdup(nodeName.c_str()), strdup("-c"),
				 strdup(cstr.c_str()), 0};
	std::list<pid_t> pids;
	for (int i = 1; i <= jobs; i++) {
		pid_t pid;
		result = posix_spawn(&pid, cmd.c_str(), &file_actions, 0, cargv, 0);
		checkLambda(result);
		std::cout << "Child process[" << i << "]: " << pid << std::endl;
		pids.push_back(pid);
	}
	for (std::list<pid_t>::iterator i = pids.begin(); i != pids.end();) {
		int status;
		result = waitpid(*i, &status, WUNTRACED | WCONTINUED);
		checkLambda((result == *i) ? 0 : -1);
		if (WIFEXITED(status) || WIFSIGNALED(status)) {
			i = pids.erase(i);
			continue;
		}
		++i;
	}
}

static void usage(const char *cmd)
{
	std::cout << "Usage " << cmd << " [-n <dev_node>] [-c <loop_count>] [-j <jobs>]\n";
	throw std::invalid_argument("");
}

int main(int argc, char *argv[])
{
	try {
		std::string nodeName = "/dev/dri/renderD128";
		int count = 100;
		int jobs = 1;
		char c = '\0';
		while ((c = getopt (argc, argv, "n:c:j:")) != -1) {
			switch (c) {
			case 'n':
				nodeName = optarg;
				break;
			case 'c':
				count = std::atoi(optarg);
				break;
			case 'j':
				jobs = std::atoi(optarg);
				break;
			case '?':
			default:
				usage(argv[0]);
			}
		}
		if (optind < argc) {
			usage(argv[0]);
		}
		if (jobs == 1) {
			run(nodeName, count);
		}
		else {
			runJobs(nodeName, count, jobs, argv[0]);
		}
	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
