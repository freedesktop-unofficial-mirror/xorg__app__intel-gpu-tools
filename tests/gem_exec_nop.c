/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_io.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_VEBOX (4<<0)

const uint32_t batch[2] = {MI_BATCH_BUFFER_END};
int device;

static int sysfs_read(const char *name)
{
	char buf[4096];
	struct stat st;
	int sysfd;
	int len;

	if (fstat(device, &st))
		return -1;

	sprintf(buf, "/sys/class/drm/card%d/%s",
		(int)(st.st_rdev & 0x7f), name);
	sysfd = open(buf, O_RDONLY);
	if (sysfd < 0)
		return -1;

	len = read(sysfd, buf, sizeof(buf)-1);
	close(sysfd);
	if (len < 0)
		return -1;

	buf[len] = '\0';
	return atoi(buf);
}

static int sysfs_write(const char *name, int value)
{
	char buf[4096];
	struct stat st;
	int sysfd;
	int len;

	if (fstat(device, &st))
		return -1;

	sprintf(buf, "/sys/class/drm/card%d/%s",
		(int)(st.st_rdev & 0x7f), name);
	sysfd = open(buf, O_WRONLY);
	if (sysfd < 0)
		return -1;

	len = sprintf(buf, "%d", value);
	len = write(sysfd, buf, len);
	close(sysfd);

	if (len < 0)
		return len;

	return 0;
}

static int dcmp(const void *A, const void *B)
{
	double a = *(double *)A, b = *(double *)B;
	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

static double elapsed(const struct timeval *start,
		      const struct timeval *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec))/loop;
}

static void loop(int fd, uint32_t handle, unsigned ring_id, const char *ring_name)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 gem_exec[1];
	int count;

	gem_require_ring(fd, ring_id);
	igt_debug("RPS frequency range [%d, %d]\n",
		  sysfs_read("gt_min_freq_mhz"),
		  sysfs_read("gt_max_freq_mhz"));

	memset(&gem_exec, 0, sizeof(gem_exec));
	gem_exec[0].handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)gem_exec;
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf)) {
		execbuf.flags = ring_id;
		do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
	}
	gem_sync(fd, handle);

	for (count = 1; count <= SLOW_QUICK(1<<17, 1<<4); count <<= 1) {
		const int reps = 9;
		double t[reps], sum;
		int n;

		for (n = 0; n < reps; n++) {
			struct timeval start, end;
			int loops = count;
			gettimeofday(&start, NULL);
			while (loops--)
				do_ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
			gem_sync(fd, handle);
			gettimeofday(&end, NULL);
			t[n] = elapsed(&start, &end, count);
		}

		qsort(t, reps, sizeof(double), dcmp);
		sum = 0;
		for (n = 2; n < reps-2; n++)
			sum += t[n];
		sum /= reps - 4;

		igt_info("Time to exec x %d:		%7.3fµs (ring=%s)\n",
				count, sum, ring_name);
		fflush(stdout);
	}
}

static void set_auto_freq(void)
{
	int min = sysfs_read("gt_RPn_freq_mhz");
	int max = sysfs_read("gt_RP0_freq_mhz");
	if (max <= min)
		return;

	igt_debug("Setting min to %dMHz, and max to %dMHz\n", min, max);
	sysfs_write("gt_min_freq_mhz", min);
	sysfs_write("gt_max_freq_mhz", max);
}

static void set_min_freq(void)
{
	int min = sysfs_read("gt_RPn_freq_mhz");
	igt_require(min > 0);
	igt_debug("Setting min/max to %dMHz\n", min);
	igt_require(sysfs_write("gt_min_freq_mhz", min) == 0 &&
		    sysfs_write("gt_max_freq_mhz", min) == 0);
}

static void set_max_freq(void)
{
	int max = sysfs_read("gt_RP0_freq_mhz");
	igt_require(max > 0);
	igt_debug("Setting min/max to %dMHz\n", max);
	igt_require(sysfs_write("gt_max_freq_mhz", max) == 0 &&
		    sysfs_write("gt_min_freq_mhz", max) == 0);
}

igt_main
{
	const struct {
		const char *suffix;
		void (*func)(void);
	} rps[] = {
		{ "", set_auto_freq },
		{ "-min", set_min_freq },
		{ "-max", set_max_freq },
		{ NULL, NULL },
	}, *r;
	int min = -1, max = -1;
	uint32_t handle = 0;

	igt_fixture {
		device = drm_open_any();

		min = sysfs_read("gt_min_freq_mhz");
		max = sysfs_read("gt_max_freq_mhz");

		handle = gem_create(device, 4096);
		gem_write(device, handle, 0, batch, sizeof(batch));
	}

	for (r = rps; r->suffix; r++) {
		igt_fixture r->func();

		igt_subtest_f("render%s", r->suffix)
			loop(device, handle, I915_EXEC_RENDER, "render");

		igt_subtest_f("bsd%s", r->suffix)
			loop(device, handle, I915_EXEC_BSD, "bsd");

		igt_subtest_f("blt%s", r->suffix)
			loop(device, handle, I915_EXEC_BLT, "blt");

		igt_subtest_f("vebox%s", r->suffix)
			loop(device, handle, LOCAL_I915_EXEC_VEBOX, "vebox");
	}

	igt_fixture {
		gem_close(device, handle);

		if (min > 0)
			sysfs_write("gt_min_freq_mhz", min);
		if (max > 0)
			sysfs_write("gt_max_freq_mhz", max);
		close(device);
	}
}
