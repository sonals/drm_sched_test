/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2021-2022 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#include <drm/drm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <spawn.h>

#include <iostream>
#include <system_error>
#include <memory>
#include <cstring>
#include <utility>
#include <vector>
#include <list>
#include <chrono>
#include <thread>

#include "sched_test.h"
#include "common.h"

void run(const int node, int count, bool release = true)
{
	/*
	 * Runs two loops: The first loop submits all jobs; the second loop waits for each submitted job
	 */

	const schedtest::raii f(node);
	f.showVersion();
	std::vector<std::pair<drm_sched_test_submit, schedtest::syncobj>> submitCmds;

	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < count; i++) {
		// Alternate between the two queues
		sched_test_queue qu = (i & 0x1) ? SCHED_TSTQ_B : SCHED_TSTQ_A;
		schedtest::syncobj soutobj(f.createSyncobj());
		drm_sched_test_submit submit = {0, soutobj(), qu};
		submitCmds.push_back(std::make_pair(std::move(submit), std::move(soutobj)));
		f.callIoctl(DRM_IOCTL_SCHED_TEST_SUBMIT, &submitCmds.back().first);
	}

	if (release) {
		// Reap all the submissions at the end
		std::vector<std::pair<drm_sched_test_submit, schedtest::syncobj>>::const_iterator i = submitCmds.begin();
		std::vector<std::pair<drm_sched_test_submit, schedtest::syncobj>>::const_iterator e = submitCmds.end();
		for (; i != e; ++i) {
			i->second.wait();
		}
	}
	// Compute the throughput
	auto end = std::chrono::high_resolution_clock::now();
	double delay = (std::chrono::duration_cast<std::chrono::microseconds>(end - start)).count();
	double iops = ((double)count * 1000000.0)/delay;
	iops /= 1000;
	std::cout << "IOPS: " << iops << " K/s" << std::endl;
}

static void usage(const char *cmd)
{
	std::cout << "Usage " << cmd << " [-n <dev_node>] [-c <loop_count>] [-j <jobs>]\n";
	throw std::invalid_argument("");
}

static void runAll(const int minor, int count)
{
	std::cout << "Thread ID " << std::this_thread::get_id() << std::endl;
	std::cout << "Start auto job cleanup test..." << std::endl;
	run(minor, count, false);
	std::cout << "Finished auto job cleanup test" << std::endl;
	std::cout << "Start regular job test..." << std::endl;
	run(minor, count);
	std::cout << "Finished regular job test..." << std::endl;
}

static void runJobs(const int minor, int count, int jobs, const std::string &cmd)
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
	const std::string nodeName = std::to_string(minor);

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
		unsigned int minor = 128;
		int jobs = 1;
		int count = 200;
		char c = '\0';
		while ((c = getopt (argc, argv, "n:c:j:")) != -1) {
			switch (c) {
			case 'n':
				minor = std::atoi(optarg);
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
			runAll(minor, count);
		}
		else {
			runJobs(minor, count, jobs, argv[0]);
		}

	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
