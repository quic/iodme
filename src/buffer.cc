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
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include <new>

#include "iodme/buffer.hpp"

namespace iodme {

// Needed for older ubuntu that do not have memfd in the glibc
#ifndef MFD_HUGETLB
#define MFD_HUGETLB	0x0004U
#endif
int memfd_create(const char *name, unsigned int flags)
{
	return syscall(SYS_memfd_create, name, 0);
}

void buffer::free()
{
	delete this->meta;

	if (this->base)
		(void) munmap(this->base, this->capacity);

	if (this->fd != -1)
		close(this->fd);

	this->reset();
}

static bool failed_alloc(buffer &b)
{
	b.free();
	return false;
}

bool buffer::alloc(size_t size, unsigned int flags, const char *name)
{
	this->reset();

	if ((flags & MEMFD) && name) {
		unsigned int mfd_flags = 0;
		if (flags & HUGEPAGE)
			mfd_flags |= MFD_HUGETLB;

		this->fd = memfd_create(name, mfd_flags);
		if (this->fd == -1)
			return failed_alloc(*this);

		if (ftruncate(this->fd, size) == -1)
			return failed_alloc(*this);
	}

	unsigned int mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE;
	if ((flags & HUGEPAGE))
		mmap_flags |= MAP_HUGETLB;

	this->base = (uint8_t *) mmap(NULL, size, PROT_READ | PROT_WRITE, mmap_flags, this->fd, 0);
	if (this->base == MAP_FAILED)
		return failed_alloc(*this);

	this->capacity = size;
	return true;
}

bool buffer::add_metadata(metadata &m)
{
	this->meta = new (std::nothrow) metadata(m);
	return this->meta != 0;
}

} // namespace iodme

