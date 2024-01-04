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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <wait.h>

//#undef NDEBUG
#include <assert.h>

#include <tf/transform_broadcaster.h>
#include <geometry_msgs/PoseStamped.h>
#include <std_msgs/Bool.h>

#include <gnss/geo_pos_conv.hpp>

#include "ros/ros.h"
#include "tablet_socket_msgs/gear_cmd.h"
#include "tablet_socket_msgs/mode_cmd.h"
#include "tablet_socket_msgs/route_cmd.h"

#define NODE_NAME	"tablet_receiver"
#define TOPIC_NR	(5)

#define DEFAULT_PORT	(5666)
#define DEFAULT_PLANE	(7)

static int getConnect(int, int *, int *);
static int getSensorValue(int, ros::Publisher[TOPIC_NR]);
static int sendSignal(int);

class Launch {
private:
	std::string launch_;
	bool running_;
	pid_t pid_;

public:
	explicit Launch(const char *launch);

	void start();
	void stop();
};

Launch::Launch(const char *launch)
{
	launch_ = launch;
	running_ = false;
}

void Launch::start()
{
	if (running_)
		return;

	running_ = true;

	pid_t pid = fork();
	if (pid > 0)
		pid_ = pid;
	else if (pid == 0) {
		execlp("roslaunch", "roslaunch", "runtime_manager",
		       launch_.c_str(), NULL);
		running_ = false;
		exit(EXIT_FAILURE);
	}
	else
		running_ = false;
}

void Launch::stop()
{
	if (!running_)
		return;

	kill(pid_, SIGTERM);
	waitpid(pid_, NULL, 0);

	running_ = false;
}

static Launch s1("check.launch"), s2("set.launch");

static geo_pos_conv geo;

void stopChildProcess(int signo)
{
	s1.stop();
	s2.stop();
	_exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	ros::Publisher pub[TOPIC_NR];
	int port, plane;
	int sock, asock;

	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = stopChildProcess;
	if (sigaction(SIGTERM, &act, NULL) == -1) {
		perror("sigaction");
		return -1;
	}

	ros::init(argc, argv, NODE_NAME);
	ros::NodeHandle node;
	pub[0] = node.advertise<tablet_socket_msgs::gear_cmd>("gear_cmd", 1);
	pub[1] = node.advertise<tablet_socket_msgs::mode_cmd>("mode_cmd", 1);
	pub[2] = node.advertise<tablet_socket_msgs::route_cmd>("route_cmd", 1);
	pub[3] = node.advertise<geometry_msgs::PoseStamped>("gnss_pose", 1);
	pub[4] = node.advertise<std_msgs::Bool>("gnss_stat", 1);
	node.param<int>("tablet_receiver/port", port, DEFAULT_PORT);
	node.param<int>("tablet_receiver/plane", plane, DEFAULT_PLANE);
	fprintf(stderr, "listen port=%d\n", port);

	geo.set_plane(plane);

	//get connect to android
	sock = -1;

	sigaction(SIGINT, NULL, &act);
	act.sa_flags &= ~SA_RESTART;
	sigaction(SIGINT, &act, NULL);

	while (getConnect(port, &sock, &asock) != -1) {
		struct timeval tv[2];
		double sec;
		int count;

		fprintf(stderr, "get connect.\n");
		gettimeofday(tv, NULL);
		for (count = 0; ; count++) {
			if(getSensorValue(asock, pub) == -1)
				break;
			if(sendSignal(asock) == -1)
				break;
		}
		close(asock);
		gettimeofday(tv+1, NULL);
		sec = (tv[1].tv_sec - tv[0].tv_sec) +
			(tv[1].tv_usec - tv[0].tv_usec) / 1000000.0;
		fprintf(stderr, "done, %f sec\n",sec);
	}

	return 0;
}

static int getConnect(int port, int *sock, int *asock)
{
	struct sockaddr_in addr;
	struct sockaddr_in client;
	socklen_t len;
	int yes = 1;

	assert(sock != NULL);
	assert(asock != NULL);

	if (*sock < 0) {
		*sock = socket(AF_INET, SOCK_STREAM, 0);
		if (*sock == -1) {
			perror("socket");
			return -1;
		}

		std::memset(&addr, 0, sizeof(sockaddr_in));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		//make it available immediately to connect
		setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR,
			(const char *)&yes, sizeof(yes));
		if (bind(*sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
			perror("bind");
			return -1;
		}
		if (listen(*sock, 5) == -1) {
			perror("listen");
			return -1;
		}
	}

	len = sizeof(client);
	*asock = accept(*sock, (struct sockaddr *)&client, &len);
	if(*asock == -1) {
		perror("accept");
		return -1;
	}

	return 0;
}

static int getSensorValue(int sock, ros::Publisher pub[TOPIC_NR])
{
	int info[2];
	size_t size = sizeof(info);
	ssize_t nbytes;

	for (char *p = (char *)info; size; size -= nbytes, p += nbytes) {
		nbytes = recv(sock, info, size, 0);
		if (nbytes == -1) {
			perror("recv");
			return -1;
		}
		if (nbytes == 0) {
			fprintf(stderr, "peer is shutdown\n");
			return -1;
		}
	}
	fprintf(stderr, "info=%d value=%d\n", info[0], info[1]);

	switch(info[0]) {
	case 1: { // GEAR
		tablet_socket_msgs::gear_cmd msg;
		msg.gear = info[1];
		pub[0].publish(msg);
		break;
	}
	case 2: { // MODE
		tablet_socket_msgs::mode_cmd msg;
		msg.mode = info[1];
		pub[1].publish(msg);
		break;
	}
	case 3: { // ROUTE
		size = info[1];
		if (!size)
			break;

		double *points = (double *)malloc(size);
		if (points == NULL) {
			perror("malloc");
			return -1;
		}

		int points_nr = size / sizeof(double);

		for (char *p = (char *)points; size;
		     size -= nbytes, p += nbytes) {
			nbytes = recv(sock, p, size, 0);
			if (nbytes == -1) {
				perror("recv");
				free(points);
				return -1;
			}
			if (nbytes == 0) {
				fprintf(stderr, "peer is shutdown\n");
				free(points);
				return -1;
			}
		}

		tablet_socket_msgs::route_cmd msg;
		tablet_socket_msgs::Waypoint point;
		for (int i = 0; i < points_nr; i++) {
			if (i % 2) {
				point.lon = points[i];
				msg.point.push_back(point);
			} else
				point.lat = points[i];
		}

		free(points);

		pub[2].publish(msg);
		break;
	}
	case 4: { // S1
		if (info[1] >= 0)
			s1.start();
		else
			s1.stop();
		break;
	}
	case 5: { // S2
		if (info[1] >= 0)
			s2.start();
		else
			s2.stop();
		break;
	}
	case 6: { // POSE
		size = info[1];
		if (!size)
			break;

		double *buf = (double *)malloc(size);
		if (buf == NULL) {
			perror("malloc");
			return -1;
		}

		for (char *p = (char *)buf; size;
		     size -= nbytes, p += nbytes) {
			nbytes = recv(sock, p, size, 0);
			if (nbytes == -1) {
				perror("recv");
				free(buf);
				return -1;
			}
			if (nbytes == 0) {
				fprintf(stderr, "peer is shutdown\n");
				free(buf);
				return -1;
			}
		}

		geo.llh_to_xyz(buf[0], buf[1], buf[2]);

		tf::Transform transform;
		tf::Quaternion q;
		transform.setOrigin(tf::Vector3(geo.y(), geo.x(), geo.z()));
		q.setRPY(buf[4], buf[5], buf[3]);
		transform.setRotation(q);

		free(buf);

		ros::Time now = ros::Time::now();

		tf::TransformBroadcaster br;
		br.sendTransform(tf::StampedTransform(transform, now, "map",
						      "gps"));

		geometry_msgs::PoseStamped pose;
		pose.header.stamp = now;
		pose.header.frame_id = "map";
		pose.pose.position.x = geo.y();
		pose.pose.position.y = geo.x();
		pose.pose.position.z = geo.z();
		pose.pose.orientation.x = q.x();
		pose.pose.orientation.y = q.y();
		pose.pose.orientation.z = q.z();
		pose.pose.orientation.w = q.w();

		std_msgs::Bool stat;
		if (pose.pose.position.x == 0 || pose.pose.position.y == 0 ||
		    pose.pose.position.z == 0)
			stat.data = false;
		else
			stat.data = true;

		pub[3].publish(pose);
		pub[4].publish(stat);
		break;
	}
	default: // TERMINATOR
		fprintf(stderr, "receive %d, terminated.\n", info[0]);
		sendSignal(sock);
		return -1;
	}

	return 0;
}

static int sendSignal(int sock)
{
	int signal = 0;

	if(send(sock, &signal, sizeof(signal), 0) == -1) {
		perror("send");
		return -1;
	}
	return 0;
}
