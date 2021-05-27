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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <hogl/post.hpp>

#include "iodme/netrx.hpp"

namespace iodme {

void netrx::loop()
{
	hogl::post(_area, _area->INFO, "start data stream %s loop", _name);

	uint64_t seqno = 0;
	iodme::buffer b;

	while (!_killed) {
		// Get new buffer if we don't have any
		if (!b.base) {
			if (!_in_q.pop(b)) {
				// wait for buffers to be available
				hogl::post(_area, _area->DEBUG, "waiting for buffer");
				iodme::thread::do_nanosleep(1000000);
				continue;
			}
			new_frame(b, seqno);
		}

		hogl::post(_area, _area->DEBUG, "calling recv: sk %d buff-room %u", _sk, b.room());

		// Receive into the tail of the buffer
		ssize_t r = recv(_sk, b.end(), b.room(), 0);
		int r_errno = errno;

		if (r < 0) {
			hogl::post(_area, _area->ERROR, "recv failed. %s(%d)", strerror(r_errno), r_errno);
			_failed = true;
			break;
		}

		if (!r) {
			hogl::post(_area, _area->INFO, "client closed connection");
			break;
		}

		b.put(r);

		hogl::post(_area, _area->DEBUG, "recv: %u bytes -- buffer: size %u, room %u", r, b.size, b.room());

		if (!b.room()) {
			// No more room in the buffer, send it down the pipe
			hogl::post(_area, _area->WARN, "ran out of buffer space, potential stall");
			_out_q.push(b); b.reset();
			continue;
		}

		// See if it's time to get a new buffer
		// FIXME: enforce frame boundaries
		if (b.room() < (b.capacity / 8)) {
			iodme::buffer nb;
			if (_in_q.pop(nb)) {
				// Cool. Got a new buffer. 
				// Send the old one off and use new.
				_out_q.push(b);
				b = nb;
				new_frame(b, seqno);
			}
		}
	}

	// Flush the last buffer (if needed)
	if (b.size)
		_out_q.push(b);
}

} // namespace iodme
