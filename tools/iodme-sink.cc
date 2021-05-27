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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <boost/program_options.hpp>

#include <hogl/format-basic.hpp>
#include <hogl/format-raw.hpp>
#include <hogl/output-stdout.hpp>
#include <hogl/output-stderr.hpp>
#include <hogl/output-plainfile.hpp>
#include <hogl/output-pipe.hpp>
#include <hogl/engine.hpp>
#include <hogl/area.hpp>
#include <hogl/mask.hpp>
#include <hogl/post.hpp>
#include <hogl/flush.hpp>
#include <hogl/ring.hpp>
#include <hogl/platform.hpp>

#include "iodme/timesource.hpp"
#include "iodme/buffer.hpp"
#include "iodme/netrx.hpp"
#include "iodme/file-writer.hpp"

////////
namespace po = boost::program_options;

static const hogl::area *area = nullptr;
static po::variables_map optmap;
static volatile bool killed = false;

// Catch most signal to terminate gracefully.
static inline void sig_handler(int signum)
{
	killed = true;
}

static inline void catch_signals()
{
	struct sigaction sa = {{0}};
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static void setup_rt_sched()
{
	// Need to be higher than most threads in the system
	struct sched_param sp = { 0 };
	sp.sched_priority = 90;
	if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
		hogl::post(area, area->WARN, "failed to set scheduling policy and priority: %s(%d).",
			strerror(errno), errno);

	if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0)
		hogl::post(area, area->WARN, "failed to lock process memory: %s(%d).",
			strerror(errno), errno);
}

static bool run()
{
	// Setup RT scheduling and lock ourselves in memory to minimize latencies.
	setup_rt_sched();

	// Connect to the sink
	int sk = socket(PF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sk < 0) {
		hogl::post(area, area->ERROR, "failed to create socket: %s(%d).",
			   strerror(errno), errno);
		return false;
	}

	int reuse = 1;
	(void) setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

#ifdef SO_REUSEPORT
	setsockopt(sk, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse));
#endif

	std::string port = optmap["sink-port"].as<std::string>();
	struct addrinfo *ai;
	if (getaddrinfo("0.0.0.0", port.c_str(), 0, &ai) < 0) {
		hogl::post(area, area->ERROR, "Failed to resolve bind address: %s(%d).",
			   strerror(errno), errno);
		return false;
	}

	int r = bind(sk, ai->ai_addr, ai->ai_addrlen);
	int r_errno = errno;

	freeaddrinfo(ai);

	if (r < 0) {
		hogl::post(area, area->ERROR, "Failed to bind the socket: %s(%d).", strerror(r_errno), r_errno);
		return false;;
	}

	if (listen(sk, 64) < 0) {
		hogl::post(area, area->ERROR, "Failed to listen on socket: %s(%d).", strerror(errno), errno);
		return false;;
	}

	iodme::queue cb_q; // Clean buffers
	iodme::queue db_q; // Dirty buffers

	std::vector<std::unique_ptr<iodme::netrx>>  netrxs;
	std::vector<std::unique_ptr<iodme::file_writer>> writers;

	uint32_t buff_size = optmap["buff-size"].as<unsigned int>() * 1024 * 1024; // MB to bytes
	unsigned int buff_count = optmap["buff-count"].as<unsigned int>();

	unsigned int buff_flags = 0;
	if (optmap.count("hugepages")) buff_flags |= iodme::buffer::HUGEPAGE;
	if (optmap.count("memfd"))     buff_flags |= iodme::buffer::MEMFD;

	// Pre-allocate clean buffers
	for (unsigned int i = 0; i < buff_count; i++) {
		std::string name("data-buffer-");
		name += std::to_string(i);

		iodme::buffer::metadata m = { 0 };
		iodme::buffer b;
		if (!b.alloc(buff_size, buff_flags, name.c_str()) || !b.add_metadata(m)) {
			hogl::post(area, area->ERROR, "failed to pre-allocate buffer-%u size %u : %s(%d).", i, buff_size, strerror(errno), errno);
			return false;
		}

		hogl::post(area, area->INFO, "pre-alloc: base %p capacity %u", b.base, b.capacity);
		cb_q.push(b);
	}

	// Start writer threads
	unsigned int wrt_flags = 0;
	if (optmap.count("directio")) wrt_flags |= iodme::file_writer::DIRECTIO;
	if (optmap.count("splice"))   wrt_flags |= iodme::file_writer::SPLICE;

	unsigned int wrt_count = optmap["writer-threads"].as<unsigned int>();
	for (unsigned int i = 0; i < wrt_count; i++) {
		std::string name("DATA-WRITER");
		name += std::to_string(i);

		auto dw = std::make_unique<iodme::file_writer>(
				name,
				optmap["output-dir"].as<std::string>(),
				db_q, cb_q, wrt_flags);
		dw->start();
		writers.push_back(std::move(dw));
	}

	hogl::post(area, area->INFO, "waiting for connections");

	while (!killed) {
		struct sockaddr_in addr;
		socklen_t alen = sizeof(addr);
		int nsk = accept(sk, (struct sockaddr *) &addr, &alen);
		if (nsk < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// Cleanup netrx threads that terminated
				for (auto it = netrxs.begin(); it != netrxs.end(); ) {
					if (!(*it)->running()) {
					    it = netrxs.erase(it);
					} else {
					    ++it;
					}
				}

				iodme::thread::do_nanosleep(10*1000*1000);
				continue;
			}

			hogl::post(area, area->ERROR, "accept failed: %s(%d).", strerror(errno), errno);
			return false;;
		}

		hogl::post(area, area->INFO, "new connection: sk %d", nsk);

		unsigned int rcvbuf = 256 * 1024; // figure out buffer size from handshake
		if (setsockopt(nsk, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof(rcvbuf)) < 0) {
			hogl::post(area, area->WARN, "Failed to set socket rcvbuf depth: %s(%d).",
				   strerror(errno), errno);
		}

		// New connection / stream
		// FIXME: set name from client handshake
		auto dn = std::make_unique<iodme::netrx>(
				std::string("data-stream-") + std::to_string(nsk),
				nsk, cb_q, db_q);
		dn->start();
		netrxs.push_back(std::move(dn));
	}

	// Release clean buffers
	iodme::buffer b;
	while (cb_q.pop(b)) {
		b.free();
	}

	return 0;
}

static std::vector<std::string> log_mask;
int main(int argc, char *argv[])
{
	// **** Parse command line arguments ****
	po::options_description optdesc("Data sink test app");
	optdesc.add_options()
		("help", "Print this message")
		("log-output", po::value<std::string>()->default_value("-"), "Log output file name or - for stdout")
		("log-format", po::value<std::string>()->default_value("timespec,timedelta,area,section"), "Log output format")
		("log-mask",   po::value<std::vector<std::string> >(&log_mask)->composing(), "Log mask. Multiple masks can be specified.")
		("output-dir,D", po::value<std::string>()->default_value("/tmp"), "Output directory for received data streams")
		("timesource,T", po::value<std::string>()->default_value("realtime"), "Timesource (clockid: realtime, monotonic)")
		("sink-port,P",  po::value<std::string>()->default_value("15740"),  "Sink TCP port to use")
		("buff-size,B",  po::value<unsigned int>()->default_value(1024),  "Buffer size in MB")
		("buff-count,C", po::value<unsigned int>()->default_value(2), "Number of buffers to allocate")
		("writer-threads,W", po::value<unsigned int>()->default_value(2), "Number of writer threads")
		("hugepages", "Use hugepages for IO buffers")
		("directio",  "Use directio for output files")
		("memfd",     "Use memfd for IO buffers")
		("splice",    "Use (vm)splice to avoid copies when possible");

	po::store(po::parse_command_line(argc, argv, optdesc), optmap);
	po::notify(optmap);

	// ** All argument values (including the defaults) are now storred in 'optmap' **

	if (optmap.count("help")) {
		std::cout << optdesc << std::endl;
		exit(1);
	}

	iodme::timesource *my_clock;
	if (optmap["timesource"].as<std::string>() == "realtime")
		my_clock = new iodme::timesource(CLOCK_REALTIME);
	else if (optmap["timesource"].as<std::string>() == "monotonic")
		my_clock = new iodme::timesource(CLOCK_MONOTONIC);
	else {
		std::cerr << "Unsupported timesource/clockid " << optmap["timesource"].as<std::string>() << std::endl;
		exit(1);
	}

	catch_signals();

	hogl::format *lf;
	hogl::output *lo;

	lf = new hogl::format_basic(optmap["log-format"].as<std::string>().c_str());
	if (optmap["log-output"].as<std::string>() == "-")
		lo = new hogl::output_stdout(*lf, 64 * 1024);
	else if (optmap["log-output"].as<std::string>()[0] == '|')
		lo = new hogl::output_pipe(optmap["log-output"].as<std::string>().c_str(), *lf, 64 * 1024);
	else
		lo = new hogl::output_plainfile(optmap["log-output"].as<std::string>().c_str(), *lf, 64 * 1024);

	hogl::engine::options eng_opts = hogl::engine::default_options;
	for (auto &m : log_mask)
		eng_opts.default_mask << m;

	hogl::activate(*lo, eng_opts);

	hogl::change_timesource(my_clock);

	// *****
	// HOGL engine is running now. Avoid exit()ing from the process
	// without going through hogl shutdown sequence below.

	area = hogl::add_area("IODME-SINK");

	hogl::ringbuf::options ring_opts = { capacity: 1024 * 8, prio: 100, flags: 0, record_tailroom: 128 };
	hogl::tls *tls = new hogl::tls("MAIN-THREAD", ring_opts);

	run();

	delete tls;

	hogl::deactivate();

	delete lo;
	delete lf;
	delete my_clock;

	return 0;
}
