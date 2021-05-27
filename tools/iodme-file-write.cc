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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include <iostream>
#include <chrono>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/count.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/statistics/stats.hpp>

#include "iodme/mover.hpp"

namespace bacc = boost::accumulators;

// Needed for older ubuntu that do not have memfd in the glibc
#ifndef MFD_HUGETLB
#define MFD_HUGETLB		0x0004U
#endif
int memfd_create(const char *name, unsigned int flags)
{
	return syscall(SYS_memfd_create, name, 0);
}

int main(int argc, char **argv) 
{
	size_t buf_size = 1ULL * 1024 * 1024 * 1024;
	bool use_splice = false;
	bool use_memfd  = false;

	unsigned int mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE;
	unsigned int open_flags = O_CREAT | O_TRUNC | O_WRONLY;

	std::string ofile("/disk/speed-test/vmsplice-out");

	for (int i=1; i < argc; i++) {
		std::string arg(argv[i]);

		if (arg == "use_memfd")  { use_memfd = true; continue; }
		if (arg == "use_splice") { use_splice = true; continue; }
		if (arg == "use_hugepages") { mmap_flags |= MAP_HUGETLB;   continue; }
		if (arg == "use_directio") { open_flags |= O_DIRECT;   continue; }
		if (arg == "1GB") { buf_size = 1ULL * 1024 * 1024 * 1024;  continue; }
		if (arg == "2GB") { buf_size = 2ULL * 1024 * 1024 * 1024;  continue; }

		ofile = arg;
	}

	// Allocate main buffer
	int buf_fd = -1;

	if (use_memfd) {
		buf_fd = memfd_create("file-write-tests.memfd", MFD_HUGETLB);
		if (buf_fd == -1) {
			std::cerr << "memfd open failed: " << strerror(errno) << '\n';
			exit(1);
		}

		if (ftruncate(buf_fd, buf_size) == -1) {
			std::cerr << "memfd open failed: " << strerror(errno) << '\n';
			exit(1);
		}
	}

	uint8_t *buf = (uint8_t *) mmap(NULL, buf_size, PROT_READ | PROT_WRITE, mmap_flags, buf_fd, 0);
	if (buf == MAP_FAILED) {
		std::cerr << "mmap failed: " << strerror(errno) << '\n';
		exit(1);
	}

        std::cout << "buffer size: " << buf_size << " bytes\n";

	iodme::mover s;
	if (s.failed())
		exit(1);

	bacc::accumulator_set< double, bacc::stats<
            bacc::tag::min,
            bacc::tag::max,
            bacc::tag::mean,
            bacc::tag::variance > > stats;

	for (unsigned int i=0; i < 20; i++) {
		int fd = open(ofile.c_str(), open_flags, 0666);
		if (fd < 0) {
			std::cerr << "open " << ofile << " failed: " << strerror(errno) << '\n';
			exit(1);
		}

		memset(buf, 0, buf_size);

		struct iovec iov;
		iov.iov_base = buf;
		iov.iov_len  = buf_size;

		auto start = std::chrono::high_resolution_clock::now();
		if (use_memfd) {
			bool r = s.do_write(fd, buf_fd, buf_size);
			if (!r) {
				std::cerr << "splice/write failed: " << strerror(s.last_errno()) << '\n';
				exit(1);
			}
		} else if (use_splice) {
			bool r = s.do_write(fd, &iov, 1);
			if (!r) {
				std::cerr << "splice/write failed: " << strerror(s.last_errno()) << '\n';
				exit(1);
			}
		} else {
			int r = writev(fd, &iov, 1);
			if (r < 0) {
				std::cerr << "write failed: " << strerror(errno) << '\n';
				exit(1);
			}
		}
        	auto end = std::chrono::high_resolution_clock::now();

        	std::chrono::duration<double> diff = end - start;
		stats(diff.count());

		fsync(fd);
		posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
		close(fd);
	}

        std::cout << "stats: "
	       	<< " min " << bacc::min(stats)
	       	<< " max " << bacc::max(stats)
	       	<< " avg " << bacc::mean(stats)
		<< " sec per write/splice\n";

	(void) munmap(buf, buf_size);
	close(buf_fd);
}
