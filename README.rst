============================
Linux DRM Scheduler Selftest
============================

Linux DRM Driver sched_test
***************************

*sched_test* is simple DRM driver which exposes a RENDER interface with ioctls to
submit (DRM_IOCTL_SCHED_TEST_SUBMIT) a dummy task and wait (DRM_IOCTL_SCHED_TEST_WAIT)
for completion. The driver uses dedicated kernel thread to emulate a real HW queue.
The DRM scheduler submits jobs to this emulated HW queue and waits for notification
from the HW thread. The emulated HW thread tries to immediately complete the task by
notifying the scheduler.

Building the driver
-------------------

::

 cd drm_sched_test
 make


Testing
*******

There are currently two tests: test1 and test2

Building the Test
-----------------

::
 cd drm_sched_test/test
 make

Running the Test
----------------

::
 cd drm_sched_test
 make run

Benchmarking the Scheduler
--------------------------

::
 cd drm_sched_test
 make
 ./test1 -c 1000000
 ./test2 -c 1000000
