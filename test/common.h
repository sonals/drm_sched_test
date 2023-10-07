/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@amd.com>
 */

#ifndef _SCHED_TEST_TEST_COMMON_H_
#define _SCHED_TEST_TEST_COMMON_H_

#include <fcntl.h>
#include <xf86drm.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <cerrno>
#include <cstring>
#include <system_error>

namespace schedtest {

class syncobj {
	static const long long _delay = 10000ll;
	const unsigned _fd;
	const std::string _nodeName;
	unsigned int _handle;
public:
	syncobj() : _fd(0xffffffff), _handle(0) {
	}

	syncobj(int fd, const std::string &nodeName) : _fd(fd), _nodeName(nodeName) {
		int result = drmSyncobjCreate(_fd, 0, &_handle);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
	}
	~syncobj() {
		if (!_handle)
			return;
		int result = drmSyncobjDestroy(_fd, _handle);
		if (result < 0) {
			// Cannot throw in the destructor, so print out the error :-(
			const std::runtime_error eobj = std::system_error(errno, std::generic_category(),
								    _nodeName);
			std::cerr << eobj.what() << std::endl;
		}
	}
	syncobj(syncobj &&other) : _fd(other._fd),
				   _nodeName(std::move(other._nodeName)),
				   _handle(other._handle) {
		other._handle = 0;
	}
	void reset() {
		int result = drmSyncobjReset(_fd, &_handle, 1);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
	}
	void wait() const {
		unsigned int handles[4];
		handles[0] = _handle;
		int result, c;
		for (c = 1; c <= 1000; c++) {
			result = drmSyncobjWait(_fd, handles, 1, _delay, 0, nullptr);
			if (result == -ETIME)
				continue;
			if (result <= 0)
				break;
		}
		std::cout << "Total wait: " << (c * _delay) / 1000 << " us\n";
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
	}
	int operator()() const {
		return _handle;
	}
};

class raii {
	const int _fd;
	const std::string _nodeName;
public:
	raii(const int node) : _fd(drmOpenRender(node)),
			       _nodeName((_fd < 0) ? "" : drmGetDeviceNameFromFd2(_fd)) {
		if (_fd < 0)
			throw std::system_error(errno, std::generic_category(), "render");
		drmVersion *version = drmGetVersion(_fd);
		if (std::strcmp(version->name, "sched_test")) {
			drmFreeVersion(version);
			close(_fd);
			throw std::system_error(ENODEV, std::system_category(), "sched_test");
		}
	}
	~raii() {
		close(_fd);
	}
	int callIoctl(unsigned long code, void *data) const {
		int result = drmIoctl(_fd, code, data);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
		return result;
	}
	void showVersion() const {
		std::cout << "# (" << _nodeName << ", ";
		drmVersion *version = drmGetVersion(_fd);
		std::cout << version->name << ", ";
		std::cout << version->desc << ") #" << std::endl;
		drmFreeVersion(version);
	}
	syncobj createSyncobj() const {
		return syncobj(_fd, _nodeName);
	}
};

}
#endif
