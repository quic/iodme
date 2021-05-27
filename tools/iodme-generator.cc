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
#include <arpa/inet.h>
#include <netdb.h>

#include <boost/program_options.hpp>

#include <fstream>

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
#include "iodme/queue.hpp"
#include "iodme/pump.hpp"
#include "iodme/nettx.hpp"

////////
using iodme::buffer;
using iodme::queue;

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
	int sk = socket(PF_INET, SOCK_STREAM, 0);
	if (sk < 0) {
		hogl::post(area, area->ERROR, "failed to create socket: %s(%d).",
			   strerror(errno), errno);
		return false;
	}

	unsigned int sndbuf = optmap["frame-size"].as<unsigned int>() * 2;
	if (setsockopt(sk, SOL_SOCKET, SO_SNDBUFFORCE, &sndbuf, sizeof(sndbuf)) < 0) {
		hogl::post(area, area->WARN, "Failed to set socket sndbuf depth: %s(%d).",
			   strerror(errno), errno);
	}
	hogl::post(area, area->INFO, "socket: snd-buffer %u", sndbuf);

	std::string host = optmap["sink-host"].as<std::string>();
	std::string port = optmap["sink-port"].as<std::string>();
	struct addrinfo *ai;
	if (getaddrinfo(host.c_str(), port.c_str(), 0, &ai) < 0) {
		hogl::post(area, area->ERROR, "Failed to resolve destination address %s: %s(%d).",
			   host.c_str(), strerror(errno), errno);
		return false;
	}

	int r = connect(sk, ai->ai_addr, ai->ai_addrlen);
	int r_errno = errno;

	freeaddrinfo(ai);

	if (r < 0) {
		hogl::post(area, area->ERROR, "Failed to connect the socket: %s(%d).", strerror(r_errno), r_errno);
		return false;
	}

	auto d_queue = std::make_unique<iodme::queue>();
	auto d_nettx = std::make_unique<iodme::nettx>(sk, *d_queue);
	auto d_pump  = std::make_unique<iodme::pump>(
			optmap["frame-size"].as<unsigned int>(),
			1000000000.0 / optmap["frame-rate"].as<float>(),
			*d_queue);

	d_nettx->start();
	if (d_nettx->failed() || !d_nettx->running()) {
		hogl::post(area, area->ERROR, "data-nettx failed/stopped");
		return false;
	}

	d_pump->start();
	if (d_pump->failed() || !d_pump->running()) {
		hogl::post(area, area->ERROR, "data-pump failed/stopped");
		return false;
	}

	while (!killed) {
		if (!d_nettx->running() || !d_pump->running()) {
			break;
		}
		iodme::thread::do_nanosleep(250*1000*1000);
	}

	return 0;
}

static std::vector<std::string> log_mask;
int main(int argc, char *argv[])
{
	// **** Parse command line arguments ****
	po::options_description optdesc("Data generator test app");
	optdesc.add_options()
		("help", "Print this message")
		("log-output", po::value<std::string>()->default_value("-"), "Log output file name or - for stdout")
		("log-format", po::value<std::string>()->default_value("timespec,timedelta,area,section"), "Log output format")
		("log-mask",   po::value<std::vector<std::string> >(&log_mask)->composing(), "Log mask. Multiple masks can be specified.")
		("timesource,T", po::value<std::string>()->default_value("realtime"),"Timesource (clockid: realtime, monotonic)")
		("sink-port,P",  po::value<std::string>()->default_value("15740"),  "Sink TCP port to use")
		("sink-host,A",  po::value<std::string>(), "Sink hostname (IP address or hostname)")
		("frame-size,s", po::value<unsigned int>()->default_value(4 * 1024 * 1024), "Size of the data frames to generate")
		("frame-rate,r", po::value<float>()->default_value(30), "Frame rate in FPS")
		("name,n",  po::value<std::string>(), "Name of data stream");

	po::store(po::parse_command_line(argc, argv, optdesc), optmap);
	po::notify(optmap);

	// ** All argument values (including the defaults) are now storred in 'optmap' **

	if (optmap.count("help")) {
		std::cout << optdesc << std::endl;
		exit(1);
	}

	if (!optmap.count("sink-host")) {
		std::cout << optdesc << std::endl;
		std::cout << "*** Sink hostname must be specified" << std::endl;
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

	area = hogl::add_area("IODME-GENERATOR");

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
