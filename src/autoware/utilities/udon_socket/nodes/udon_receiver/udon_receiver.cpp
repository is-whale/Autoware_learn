/*
 * Copyright 2015-2019 Autoware Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>

#include <thread>

#include <ros/ros.h>
#include <ros/console.h>

#include <udon_socket/udon.hpp>

namespace {

void recv_cmd(const sockaddr_in client_addr, int connect_fd, std::size_t bufsize)
{
	std::uint8_t *buf;

	char astr[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &client_addr.sin_addr, astr, sizeof(astr)) == nullptr) {
		ROS_ERROR_STREAM("inet_ntop: " << std::strerror(errno));
		goto close_connect_fd;
	}

	buf = new std::uint8_t[bufsize];

	while (true) {
		ssize_t nbytes = recv(connect_fd, buf, bufsize, 0);
		if (nbytes < 0) {
			ROS_ERROR_STREAM("recv: " << std::strerror(errno));
			goto delete_buf;
		} else if (nbytes == 0) {
			ROS_INFO_STREAM("disconnect " << astr << ":" << ntohs(client_addr.sin_port));
			goto delete_buf;
		}
		nbytes = udon_socket::udon::send_response(connect_fd);
		if (nbytes < 0) {
			ROS_ERROR_STREAM("udon_socket::udon::send_response: " << std::strerror(errno));
			goto delete_buf;
		}

		// XXX TBD
	}

delete_buf:
	delete[] buf;
close_connect_fd:
	close(connect_fd);
}

} // namespace

int main(int argc, char **argv)
{
	ros::init(argc, argv, "udon_receiver");

	ros::NodeHandle n;

	int backlog;
	n.param<int>("/udon_receiver/backlog", backlog, 128);
	int bufsize;
	n.param<int>("/udon_receiver/bufsize", bufsize, 4096);
	int port;
	n.param<int>("/udon_receiver/port", port, 5888);
	ROS_INFO_STREAM("backlog = " << backlog);
	ROS_INFO_STREAM("bufsize = " << bufsize);
	ROS_INFO_STREAM("port = " << port);

	int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_fd < 0) {
		ROS_ERROR_STREAM("socket: " << std::strerror(errno));
		return EXIT_FAILURE;
	}

	const int on = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		ROS_ERROR_STREAM("setsockopt: " << std::strerror(errno));
		goto close_listen_fd;
	}

	sockaddr_in server_addr;
	std::memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listen_fd, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
		ROS_ERROR_STREAM("bind: " << std::strerror(errno));
		goto close_listen_fd;
	}

	if (listen(listen_fd, backlog) < 0) {
		ROS_ERROR_STREAM("listen: " << std::strerror(errno));
		goto close_listen_fd;
	}

	char astr[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &server_addr.sin_addr, astr, sizeof(astr)) == nullptr) {
		ROS_ERROR_STREAM("inet_ntop: " << std::strerror(errno));
		goto close_listen_fd;
	}
	ROS_INFO_STREAM("listen " << astr << ":" << ntohs(server_addr.sin_port));

	struct sigaction act;
	sigaction(SIGINT, NULL, &act);
	act.sa_flags &= ~SA_RESTART;
	sigaction(SIGINT, &act, NULL);

	int connect_fd;
	while (true) {
		sockaddr_in client_addr;
		socklen_t len = sizeof(client_addr);
		connect_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &len);
		if (connect_fd < 0) {
			ROS_ERROR_STREAM("accept: " << std::strerror(errno));
			goto close_listen_fd;
		}

		if (inet_ntop(AF_INET, &client_addr.sin_addr, astr, sizeof(astr)) == nullptr) {
			ROS_ERROR_STREAM("inet_ntop: " << std::strerror(errno));
			goto close_connect_fd;
		}
		ROS_INFO_STREAM("connect " << astr << ":" << ntohs(client_addr.sin_port));

		try {
			std::thread receiver(recv_cmd, client_addr, connect_fd, bufsize);
			receiver.detach();
		} catch (std::exception &ex) {
			ROS_ERROR_STREAM("std::thread::thread: " << ex.what());
			goto close_connect_fd;
		}
	}

close_connect_fd:
	close(connect_fd);
close_listen_fd:
	close(listen_fd);

	return EXIT_FAILURE;
}
