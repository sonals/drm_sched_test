#include <drm/drm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>


#include <iostream>
#include <cerrno>
#include <system_error>
#include <stdexcept>
#include <memory>
#include <cstring>

#include "sched_test.h"

// g++ -I ../uapi/ test1.cpp -ldrm

static const int LEN = 128;

int run(const char *nodeName)
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

	int result = ioctl(fd, DRM_IOCTL_VERSION, &version);
	if (result < 0)
		throw std::system_error(errno, std::generic_category(), "version");

	std::cout << version.name << std::endl;

	drm_sched_test_submit submit0 = {1024};
	result = ioctl(fd, DRM_IOCTL_SCHED_TEST_SUBMIT, &submit0);
	if (result < 0)
		throw std::system_error(errno, std::generic_category(), nodeName);

	drm_sched_test_submit submit1 = {1024};
	result = ioctl(fd, DRM_IOCTL_SCHED_TEST_SUBMIT, &submit1);
	if (result < 0)
		throw std::system_error(errno, std::generic_category(), nodeName);

	return 0;
}

int main(int argc, char *argv[])
{
	int result = 0;
	static const char *nodeName = "/dev/dri/renderD128";
	try {
		result = run(nodeName);
		std::cout << "result = " << result << std::endl;
	} catch (std::exception &ex) {
		std::cout << ex.what() << std::endl;
	}
	return result;
}
