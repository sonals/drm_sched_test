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
#include <system_error>
#include <memory>
#include <cstring>
#include <vector>
#include <list>
#include <chrono>
#include <thread>

#include "sched_test.h"

static const int LEN = 128;

void run(const std::string &nodeName, int count, bool release = true)
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

	std::vector<drm_sched_test_submit> submitCmds(count);
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < count; i++) {
		sched_test_queue qu = (i & 0x1) ? SCHED_TSTQ_B : SCHED_TSTQ_A;
		submitCmds[i].in.qu = qu;
		submitCmds[i].in.in_fence = 0;
		ioctlLambda(DRM_IOCTL_SCHED_TEST_SUBMIT, &submitCmds[i]);
	}

	if (release) {
		for (int i = 0; i < count; i++) {
			drm_sched_test_wait wait = {
				.in = {
					.fence = submitCmds[i].out.fence,
					.timeout = 100
				}
			};
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

static void usage(const char *cmd)
{
	std::cout << "Usage " << cmd << " [-n <dev_node>] [-c <loop_count>] [-j <jobs>]\n";
	throw std::invalid_argument("");
}

static void runAll(const std::string &nodeName, int count)
{
	std::cout << "Start auto job cleanup test..." << std::endl;
	run(nodeName, count, false);
	std::cout << "Finished auto job cleanup test" << std::endl;
	std::cout << "Start regular job test..." << std::endl;
	run(nodeName, count);
	std::cout << "Finished regular job test..." << std::endl;
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

int main(int argc, char *argv[])
{
	try {
		std::string nodeName = "/dev/dri/renderD128";
		int jobs = 1;
		int count = 100;
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
			runAll(nodeName, count);
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
