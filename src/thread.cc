//  Copyright (c) 2021, Qualcomm Innovation Center, Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  3. Neither the name of the copyright holder nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
//  SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <hogl/post.hpp>
#include <hogl/flush.hpp>
#include <hogl/ring.hpp>
#include <hogl/tls.hpp>
#include <hogl/platform.hpp>

#include "iodme/thread.hpp"

namespace iodme {

thread::thread(const std::string &name) :
	_name(name),
	_failed(false),
	_running(false),
	_killed(false),
	_thread_created(false)
{
	_area = hogl::add_area(_name.c_str());
}

bool thread::start()
{
	_running = true;

	int err = pthread_create(&_thread, NULL, entry, (void *) this);
	if (err) {
		hogl::post(_area, _area->ERROR, "failed to create thread. %d\n", err);
		_failed  = true;
		_running = false;
		return false;
	}

	_thread_created = true;
	return true;
}

thread::~thread()
{
	_killed = true;
	if (_thread_created)
		pthread_join(_thread, NULL);
}

void *thread::entry(void *_self)
{
	thread *self = (thread *) _self;

	hogl::platform::set_thread_title(self->_area->name());

	hogl::ringbuf::options ring_opts = { .capacity = 1024 * 8, .prio = 100, .flags = 0, .record_tailroom = 128 };
	hogl::tls tls(self->_name.c_str(), ring_opts);

	hogl::post(self->_area, self->_area->DEBUG, "thread entry: ring %p", tls.ring());

	// Run the loop
	self->loop();

	self->_running = false;

	return 0;
}

} // namespace iodme
