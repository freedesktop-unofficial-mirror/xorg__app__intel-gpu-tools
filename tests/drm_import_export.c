/*
 * Copyright © 2014 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <libdrm/drm.h>
#include <libdrm/i915_drm.h>
#include <xf86drm.h>
#include <intel_bufmgr.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "igt_core.h"
#include "drmtest.h"

int fd;
drm_intel_bufmgr *bufmgr;
int fd1;
drm_intel_bufmgr *bufmgr1;

bool use_flink;

static void new_buffers(void)
{
	unsigned int *buf1;
	drm_intel_bo *bo1, *bo2;


	bo1 = drm_intel_bo_alloc(bufmgr, "buf1",16384, 4096);
	igt_assert(bo1);
	drm_intel_bo_map(bo1, 1);
	bo2 = drm_intel_bo_alloc(bufmgr, "buf2", 16384, 4096);
	igt_assert(bo2);
	drm_intel_bo_map(bo2, 1);

	buf1 = (unsigned int *)bo1->virtual;
	igt_assert(buf1);
	memset(buf1, 0, 16384);
	buf1[4000]=0x05000000;

	drm_intel_bo_exec(bo1, 16384, NULL, 0,0);
	drm_intel_bo_wait_rendering(bo1);

	drm_intel_bo_unmap( bo1 );
	drm_intel_bo_unreference(bo1);

	drm_intel_bo_unmap( bo2 );
	drm_intel_bo_unreference(bo2);
}

static void test_surfaces(drm_intel_bo *bo_shared)
{
	drm_intel_bo * bo;
	int loop=2;

	while(loop--) {
		if (use_flink) {
			uint32_t name;
			drm_intel_bo_flink(bo_shared, &name);
			bo = drm_intel_bo_gem_create_from_name(bufmgr,
							       "shared resource",
							       name);
		} else {
			int prime_fd;

			drm_intel_bo_gem_export_to_prime(bo_shared, &prime_fd);
			bo = drm_intel_bo_gem_create_from_prime(bufmgr,
								prime_fd, 4096);
			close(prime_fd);
		}

		igt_assert(bo);
		new_buffers();
		drm_intel_bo_unreference(bo);
	}
}

static void start_test(void)
{
	int i;

	for (i=0; i < 16384; i++)
	{
		drm_intel_bo * bo_shared;

		bo_shared = drm_intel_bo_alloc(bufmgr1, "buf-shared",16384, 4096);
		test_surfaces(bo_shared);
		drm_intel_bo_unreference(bo_shared);
	}
}

static void * test_thread(void * par)
{
#ifdef __linux__
	igt_debug("start %ld\n", syscall(SYS_gettid));
#else
	igt_debug("start %ld\n", (long) pthread_self());
#endif
	start_test();

	return NULL;
}

pthread_t test_thread_id1;
pthread_t test_thread_id2;
pthread_t test_thread_id3;
pthread_t test_thread_id4;

igt_main {
	igt_fixture {
		fd1 = drm_open_any();
		igt_assert(fd1 >= 0);
		bufmgr1 = drm_intel_bufmgr_gem_init(fd1, 8 *1024);
		igt_assert(bufmgr1);

		drm_intel_bufmgr_gem_enable_reuse(bufmgr1);

		fd = drm_open_any();
		igt_assert(fd >= 0);
		bufmgr = drm_intel_bufmgr_gem_init(fd, 8 *1024);
		igt_assert(bufmgr);

		drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	}

	igt_subtest("flink") {
		use_flink = true;

		pthread_create(&test_thread_id1, NULL, test_thread, NULL);
		pthread_create(&test_thread_id2, NULL, test_thread, NULL);
		pthread_create(&test_thread_id3, NULL, test_thread, NULL);
		pthread_create(&test_thread_id4, NULL, test_thread, NULL);

		pthread_join(test_thread_id1, NULL);
		pthread_join(test_thread_id2, NULL);
		pthread_join(test_thread_id3, NULL);
		pthread_join(test_thread_id4, NULL);
	}

	igt_subtest("prime") {
		use_flink = false;

		pthread_create(&test_thread_id1, NULL, test_thread, NULL);
		pthread_create(&test_thread_id2, NULL, test_thread, NULL);
		pthread_create(&test_thread_id3, NULL, test_thread, NULL);
		pthread_create(&test_thread_id4, NULL, test_thread, NULL);

		pthread_join(test_thread_id1, NULL);
		pthread_join(test_thread_id2, NULL);
		pthread_join(test_thread_id3, NULL);
		pthread_join(test_thread_id4, NULL);
	}
}
