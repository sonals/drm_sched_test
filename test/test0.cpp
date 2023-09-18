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

static const int LEN = 128;

static void usage(const char *cmd)
{
	std::cout << "Usage " << cmd << " [-n <dev_node>]\n";
	throw std::invalid_argument("");
}

struct syncobj {
	const unsigned _fd;
	const std::string _nodeName;
	unsigned int handle;
	syncobj(int fd, const std::string &nodeName) : _fd(fd), _nodeName(nodeName) {
		int result = drmSyncobjCreate(_fd, 0, &handle);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
	}
	~syncobj() {
		int result = drmSyncobjDestroy(_fd, handle);
		if (result < 0) {
			// Cannot throw in the destructor, so print out the error :-(
			const std::runtime_error eobj = std::system_error(errno, std::generic_category(),
								    _nodeName);
			std::cerr << eobj.what() << std::endl;
		}
	}
	void reset() {
		int result = drmSyncobjReset(_fd, &handle, 1);
		if (result < 0)
			throw std::system_error(errno, std::generic_category(), _nodeName);
	}
};

struct raii {
	std::string _nodeName;
	int _fd;
	raii(const int _node) {
		_fd = drmOpenRender(_node);
		if (_fd < 0)
			throw std::system_error(errno, std::generic_category(), "render");
		char *name = drmGetDeviceNameFromFd2(_fd);
		_nodeName = name;
		drmFree(name);
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
	syncobj syncobjCreate() const {
		return syncobj(_fd, _nodeName);
	}
};

void run(const int node, bool release = true)
{
	const raii f(node);
	f.showVersion();
	syncobj sobj(f.syncobjCreate());
}

static void runAll(const unsigned minor = 128)
{
	std::cout << "Start auto job cleanup test..." << std::endl;
	run(minor, false);
	std::cout << "Finished auto job cleanup test" << std::endl;
	std::cout << "Start regular job test..." << std::endl;
	run(minor);
	std::cout << "Finished regular job test..." << std::endl;
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
