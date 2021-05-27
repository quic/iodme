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

#ifndef IODME_BUFFER_HPP
#define IODME_BUFFER_HPP

#define _GNU_SOURCE 1

#include <stdint.h>
#include <errno.h>

namespace iodme {

struct buffer {
	struct metadata {
		uint64_t seqno;
		char     name[128];
	};

	uint8_t*  base;
	uint32_t  capacity;
	uint32_t  size;
	int       fd; // -1 normal buffer, otherwise memfd
	metadata* meta;

	void reset()
	{
		this->base = 0;
		this->size = 0;
		this->capacity = 0;
		this->fd = -1;
		this->meta = 0;
	}

	void clear()
	{
		this->size = 0;
	}

	buffer()
	{
		this->reset();
	}

	uint8_t *begin() const { return base; }
	uint8_t *end()   const { return base + size; }
	uint32_t room()  const { return capacity - size; }

	// Put N bytes into buffer.
	// Caller is supposed to check for available room first.
	void put(uint32_t n) { size += n; }

	enum Flags {
		HUGEPAGE = (1<<0),
		MEMFD    = (1<<1)
	};

	bool alloc(size_t size, unsigned int flags = 0, const char *filename = 0);
	void free();
	bool add_metadata(metadata& m);
};

} // namespace iodme

#endif // IODME_BUFFER_HPP
