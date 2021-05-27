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

#ifndef IODME_NETRX_HPP
#define IODME_NETRX_HPP

#define _GNU_SOURCE 1

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <hogl/post.hpp>
#include <string>

#include <iodme/buffer.hpp>
#include <iodme/queue.hpp>
#include <iodme/thread.hpp>

namespace iodme {

class netrx : public iodme::thread {
private:
	std::string _name;
	int         _sk;
	iodme::queue& _in_q;
	iodme::queue& _out_q;

	void loop();

	void kill()
	{
		close(_sk);
		iodme::thread::kill();
	}

	void new_frame(iodme::buffer& b, uint64_t& seqno)
	{
		size_t n = _name.copy(b.meta->name, sizeof(b.meta->name));
		b.meta->name[n] ='\0';
		b.meta->seqno = seqno++;

		hogl::post(_area, _area->INFO, "new-frame: base %p capacity %u seqno %llu",
				b.base, b.capacity, b.meta->seqno);
	}

public:
	netrx(const std::string& name, int in_sk, iodme::queue &in_q, iodme::queue &out_q) :
		thread(std::string("IODME-NETRX") + std::to_string(in_sk)),
		_name(name),
		_sk(in_sk),
		_in_q(in_q),
		_out_q(out_q)
	{}

	~netrx()
	{
		close(_sk);
	}
};

} // namespace iodme

#endif // IODME_NETRX_HPP
