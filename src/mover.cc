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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/sendfile.h>

#include <fstream>
#include <new>
#include <algorithm>

#include "iodme/mover.hpp"

namespace iodme {

// Get max pipe buffer size from sysctl
// FIXME: add error handling
unsigned int max_pipe_size()
{
	unsigned int s;
	std::ifstream in("/proc/sys/fs/pipe-max-size");
	in >> std::dec >> s;
	return s;
}

mover::mover() :
	_failed(true), 
	_errno(EBADFD)
{
	// Open the pipe used for splicing
	if (pipe(_pipe_fd) < 0) {
		_errno = errno;
		return;
	}

	// Increase size of the pipe buffer
	_pipe_size = max_pipe_size();
	fcntl(_pipe_fd[1], F_SETPIPE_SZ, _pipe_size);
	fcntl(_pipe_fd[0], F_SETPIPE_SZ, _pipe_size);

	_errno  = 0;
	_failed = false;
}

mover::~mover()
{
	close(_pipe_fd[0]);
	close(_pipe_fd[1]);
}

// returns new head of the iovec and updates count
static struct iovec* update_iov(size_t used, struct iovec* iov, unsigned int &iovcnt)
{
	while (used && iovcnt) {
		size_t u = std::min(iov->iov_len, used);
		iov->iov_base = (uint8_t *) iov->iov_base + u;
		iov->iov_len -= u;
		used -= u;
		if (iov->iov_len) return iov; // not yet consumed
		++iov; --iovcnt;
	}

	// consumed all elements
	return 0;
}

// Do write/splice using the pipe
bool mover::do_write(int fd, struct iovec *iov, unsigned int iovcnt)
{
	unsigned int flags = 0;

	while (iov) {
		ssize_t n, r;

		// Splice the buffer into the write-end of the pipe
		n = vmsplice(_pipe_fd[1], iov, iovcnt, flags);
		if (n < 0) {
			_errno = errno;
			return false;
		}

		iov = update_iov(n, iov, iovcnt);

		// Splice read-end of the pipe into the output fd
		r = splice(_pipe_fd[0], NULL, fd, NULL, n, SPLICE_F_MOVE);
		if (r < 0) {
			_errno = errno;
			return false;
		}
	}

	return true;
}

// Do direct write/splice without using pipe
bool mover::do_write(int fd, int in_fd, size_t len)
{
	ssize_t r;
	off_t in_off = 0;

	r = sendfile(fd, in_fd, &in_off, len);
	if (r < 0) {
		_errno = errno;
		return false;
	}

	return true;
}

} // namespace iodme

