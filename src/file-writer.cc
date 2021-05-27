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
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <hogl/area.hpp>
#include <hogl/mask.hpp>
#include <hogl/post.hpp>

#include "iodme/mover.hpp"
#include "iodme/file-writer.hpp"

#include <string>

namespace iodme {

bool file_writer::do_write(iodme::mover& dme, buffer& b)
{
	unsigned int open_flags = O_CREAT | O_TRUNC | O_WRONLY |
				(_flags & DIRECTIO ? O_DIRECT : 0);

	std::string ofile(_odir);
		ofile += '/';
		ofile += b.meta->name;
		ofile += '.';
		char seqno_str[128] {0};
		std::sprintf(seqno_str, "%06lu", b.meta->seqno);
		ofile += seqno_str;

	// See if we need to pad the data for O_DIRECT
	uint32_t pad = 0;
	if ((open_flags & O_DIRECT)) {
		pad = b.size % directio_block;
		if (pad) {
			pad = directio_block - pad;
			if (pad > b.room()) {
				// This is unlikely since all our buffers are multiple of 1KB
				// So either we don't need the pad or we always have room for it.
				hogl::post(_area, _area->WARN, "no room for direct-io pad; doing regular-io %s: %s(%d).",
					ofile, strerror(errno), errno);
				open_flags &= ~O_DIRECT;
			} else {
				memset(b.end(), 0, pad);
				b.put(pad);
			}
		}
	}

	hogl::post(_area, _area->DEBUG, "open-start %s size %llu pad %u", ofile, b.size, pad);

	int fd = open(ofile.c_str(), open_flags, 0666);
	if (fd < 0) {
		hogl::post(_area, _area->ERROR, "failed open output file %s: %s(%d).",
				ofile, strerror(errno), errno);
		return false;
	}

	struct iovec iov;
	iov.iov_base = b.base;
	iov.iov_len  = b.size;

	hogl::post(_area, _area->DEBUG, "write-start %s", ofile);

	bool w = true;
	int  w_errno;
	if (b.fd != -1) {
		w = dme.do_write(fd, b.fd, b.size);
		w_errno = errno;
	} else if (_flags & SPLICE) {
		w = dme.do_write(fd, &iov, 1);
		w_errno = errno;
	} else {
		ssize_t n = writev(fd, &iov, 1);
		w_errno = errno;
		if (n != b.size) {
			if (n != -1) {
				hogl::post(_area, _area->ERROR, "partial write: %u -> %d", b.size, n);
				w_errno = EIO;
			}
			w = false;
		}
	}

	hogl::post(_area, _area->DEBUG, "write-end %s", ofile);

	// Sync and drop cached pages
	fsync(fd);
	posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

	// Drop the pad (if any)
	if (pad)
		pad = ftruncate(fd, b.size - pad);
	close(fd);

	hogl::post(_area, _area->DEBUG, "close-end %s", ofile);

	if (!w) {
		hogl::post(_area, _area->ERROR, "write failed: %s(%d) : removing %s", strerror(w_errno), w_errno, ofile);
		unlink(ofile.c_str());
	}

	return w;
}

void file_writer::loop()
{
	hogl::post(_area, _area->INFO, "data writer loop");

	iodme::mover dme;
	if (dme.failed()) {
		hogl::post(_area, _area->ERROR, "data mover engine failed to init");
		return;
	}

	buffer b;

	while (!_killed) {
		// Get new buffer if we don't have any
		if (!_in_q.pop(b)) {
			// wait for buffers to be available
			iodme::thread::do_nanosleep(100000);
			continue;
		}

		hogl::post(_area, _area->INFO, "in-buff: base %p size %u room %u capacity %u seqno %llu name %s",
				b.base, b.size, b.room(), b.capacity, b.meta->seqno, b.meta->name);

		do_write(dme, b);

		// Return for reuse
		b.clear();
		_out_q.push(b);
	}
}

} // namespace iodme
