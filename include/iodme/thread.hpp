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

#ifndef IODME_THREAD_HPP
#define IODME_THREAD_HPP

#define _GNU_SOURCE 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <hogl/area.hpp>
#include <hogl/post.hpp>
#include <hogl/flush.hpp>
#include <hogl/ring.hpp>
#include <hogl/tls.hpp>
#include <hogl/platform.hpp>

namespace iodme {

//////////////
class thread {
public:
	bool failed()  const { return _failed;  }
	bool running() const { return _running; }
	bool start();
	void kill() { _killed = true; }

	static inline void do_nanosleep(uint64_t nsec)
	{
		if (!nsec) return;
		struct timespec ts = {
			(unsigned int) nsec / 1000000000,
			(unsigned int) nsec % 1000000000
		};
		nanosleep(&ts, 0);
	}

protected:
	explicit thread(const std::string &name);
	virtual ~thread();
	hogl::area    *_area;
	std::string    _name;

	volatile bool _failed;
	volatile bool _running;
	volatile bool _killed;

	pthread_t     _thread;
	volatile bool _thread_created;

	static void *entry(void *_self);
	virtual void loop() = 0;
};

} // namespace iodme

#endif // IODME_THREAD_HPP
