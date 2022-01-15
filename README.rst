============================
Linux DRM Scheduler Selftest
============================

Linux DRM sched_test Driver
***************************

*sched_test* is a simple DRM driver which exposes a RENDER interface with ioctls
to submit (DRM_IOCTL_SCHED_TEST_SUBMIT) a dummy task and wait
(DRM_IOCTL_SCHED_TEST_WAIT) for the completion of the sumitted dummy task. The
driver uses a dedicated kernel thread to emulate a real HW queue. The DRM
scheduler instantiated by this driver submits jobs to this emulated HW queue and
waits for completion notification from the emulated HW thread. The emulated HW
thread treats the sumitted dummy task as a NOP and tries to immediately complete
the task by notifying the scheduler of completion.

Building the driver
-------------------

::

 cd drm_sched_test
 make


Test Applications
*****************

There are currently two tests: test1 and test2

Building the Test Applications
------------------------------

::

 cd drm_sched_test/test
 make

Running the Test Applications
-----------------------------

::

 cd drm_sched_test
 make run

Benchmarking the Scheduler
--------------------------

The tests print throughput numbers as IOPS. Run the tests with large iteration
loops like this

::

 cd drm_sched_test
 make
 ./test1 -c 1000000
 ./test2 -c 1000000
