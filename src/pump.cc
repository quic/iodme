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
#include <errno.h>

#include <hogl/post.hpp>

#include "iodme/pump.hpp"

namespace iodme {

void pump::loop()
{
	hogl::post(_area, _area->INFO, "loop: frame-size %u interval-nsec %llu", _size, _interval_nsec);

	uint64_t seqno = 0;
	while (!_killed) {
		do_nanosleep(_interval_nsec);

		iodme::buffer::metadata m;
		m.seqno = seqno++;

		iodme::buffer b;
		if (!b.alloc(_size)) {
			hogl::post(_area, _area->WARN, "dropping frame %llu : malloc fail", m.seqno);
			continue;
		}
		if (!b.add_metadata(m)) {
			hogl::post(_area, _area->WARN, "dropping frame %llu : malloc(metadata) fail", m.seqno);
			continue;
		}

		// FIXME: populate chunk with pseudo-randon data
		memset(b.base, 0, b.capacity);
		b.size = b.capacity;

		if (!_q.push(b)) {
			b.free();
			hogl::post(_area, _area->WARN, "dropping frame %llu : full queue", m.seqno);
		}
	}
}

} // namespace iodme
