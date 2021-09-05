#ifndef _DRM_SCHED_TEST_H_
#define _DRM_SCHED_TEST_H_

#ifndef __KERNEL__
#include <libdrm/drm.h>
#else
#include <uapi/drm/drm.h>
#endif /* !__KERNEL__ */

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_SCHED_TEST_SUBMIT                     0x00
#define DRM_SCHED_TEST_WAIT                       0x01

struct drm_sched_test_submit {
	int fence; /* out */
};

struct drm_sched_test_wait {
	int fence; /* in */
	signed long timeout;
};

#define DRM_IOCTL_SCHED_TEST_SUBMIT           DRM_IOWR(DRM_COMMAND_BASE + DRM_SCHED_TEST_SUBMIT, struct drm_sched_test_submit)
#define DRM_IOCTL_SCHED_TEST_WAIT             DRM_IOWR(DRM_COMMAND_BASE + DRM_SCHED_TEST_WAIT, struct drm_sched_test_wait)


#if defined(__cplusplus)
}
#endif

#endif
