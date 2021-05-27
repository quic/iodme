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

#ifndef IODME_FILE_WRITER_HPP
#define IODME_FILE_WRITER_HPP

#define _GNU_SOURCE 1

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <iodme/queue.hpp>
#include <iodme/thread.hpp>
#include <iodme/mover.hpp>

namespace iodme {

class file_writer : public iodme::thread {
private:
	std::string   _odir;
	iodme::queue& _in_q;
	iodme::queue& _out_q;
	unsigned int  _flags;

	// O_DIRECT requires multiple of block size (most devices use 512)
	const unsigned int directio_block = 512;

	void loop();

	bool do_write(iodme::mover& dme, iodme::buffer& b);

public:
	enum Flags {
		DIRECTIO = (1<<0),
		SPLICE   = (1<<1)
	};

	file_writer(const std::string& name, const std::string& odir, iodme::queue &in_q, iodme::queue &out_q, unsigned int flags = 0) :
		thread(name),
		_odir(odir),
		_in_q(in_q),
		_out_q(out_q),
		_flags(flags)
	{}
};

} // namespace iodme

#endif // IODME_FILE_WRITER_HPP
