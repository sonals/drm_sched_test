#include <fcntl.h>
#include <xf86drm.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <cerrno>
#include <system_error>
#include <memory>
#include <cstring>
#include <vector>
#include <list>
#include <chrono>
#include <thread>

#include "sched_test.h"

static void usage(const char *cmd)
{
	std::cout << "Usage " << cmd << " [-n <dev_node>]\n";
	throw std::invalid_argument("");
}

class syncobj {
	static const long long delay = 10000ll;
	const unsigned _fd;
	const std::string _nodeName;
	unsigned int _handle;
public:
	syncobj(int fd, const std::string &nodeName) : _fd(fd), _nodeName(nodeName) {
		int result = drmSyncobjCreate(_fd, 0, &_handle);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
	}
	~syncobj() {
		int result = drmSyncobjDestroy(_fd, _handle);
		if (result < 0) {
			// Cannot throw in the destructor, so print out the error :-(
			const std::runtime_error eobj = std::system_error(errno, std::generic_category(),
								    _nodeName);
			std::cerr << eobj.what() << std::endl;
		}
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
			result = drmSyncobjWait(_fd, handles, 1, delay, 0, nullptr);
			if (result == -ETIME)
				continue;
			if (result <= 0)
				break;
		}
		std::cout << "Total wait " << (c * delay) / 1000 << " us\n";
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
		std::cout << _nodeName << std::endl;
		drmVersion *version = drmGetVersion(_fd);
		std::cout << version->name << std::endl;
		std::cout << version->desc << std::endl;
		drmFreeVersion(version);
	}
	syncobj createSyncobj() const {
		return syncobj(_fd, _nodeName);
	}
};

void run(const int node, bool release = true)
{
	const raii f(node);
	f.showVersion();
	syncobj soutobja(f.createSyncobj());
	syncobj soutobjb(f.createSyncobj());
	drm_sched_test_submit submitb = {0, soutobjb(), SCHED_TSTQ_B};
	f.callIoctl(DRM_IOCTL_SCHED_TEST_SUBMIT, &submitb);
	drm_sched_test_submit submita = {soutobjb(), soutobja(), SCHED_TSTQ_A};
	f.callIoctl(DRM_IOCTL_SCHED_TEST_SUBMIT, &submita);
	if (release)
		soutobja.wait();
}

static void runAll(const unsigned minor = 128)
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
		std::string nodeName = "/dev/dri/renderD128";
		char c = '\0';
		while ((c = getopt (argc, argv, "n:")) != -1) {
			switch (c) {
			case 'n':
				nodeName = optarg;
				break;
			case '?':
			default:
				usage(argv[0]);
			}
		}
		if (optind < argc) {
			usage(argv[0]);
		}

		runAll(128);

	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
