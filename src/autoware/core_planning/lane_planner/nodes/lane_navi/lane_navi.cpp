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

#include <sstream>

#include <ros/console.h>
#include <tf/transform_datatypes.h>

#include <vector_map/vector_map.h>
#include "autoware_msgs/LaneArray.h"

#include "lane_planner/lane_planner_vmap.hpp"

namespace {

int waypoint_max;
double search_radius; // meter
double velocity; // km/h
std::string frame_id;
std::string output_file;

ros::Publisher waypoint_pub;

lane_planner::vmap::VectorMap all_vmap;
lane_planner::vmap::VectorMap lane_vmap;
tablet_socket_msgs::route_cmd cached_route;

std::vector<std::string> split(const std::string& str, char delim)
{
	std::stringstream ss(str);
	std::string s;
	std::vector<std::string> vec;
	while (std::getline(ss, s, delim))
		vec.push_back(s);

	if (!str.empty() && str.back() == delim)
		vec.push_back(std::string());

	return vec;
}

std::string join(const std::vector<std::string>& vec, char delim)
{
	std::string str;
	for (size_t i = 0; i < vec.size(); ++i) {
		str += vec[i];
		if (i != (vec.size() - 1))
			str += delim;
	}

	return str;
}

int count_lane(const lane_planner::vmap::VectorMap& vmap)
{
	int lcnt = -1;

	for (const vector_map::Lane& l : vmap.lanes) {
		if (l.lcnt > lcnt)
			lcnt = l.lcnt;
	}

	return lcnt;
}

void create_waypoint(const tablet_socket_msgs::route_cmd& msg)
{
	std_msgs::Header header;
	header.stamp = ros::Time::now();
	header.frame_id = frame_id;

	if (all_vmap.points.empty() || all_vmap.lanes.empty() || all_vmap.nodes.empty()) {
		cached_route.header = header;
		cached_route.point = msg.point;
		return;
	}

	lane_planner::vmap::VectorMap coarse_vmap = lane_planner::vmap::create_coarse_vmap_from_route(msg);
	if (coarse_vmap.points.size() < 2)
		return;

	std::vector<lane_planner::vmap::VectorMap> fine_vmaps;
	lane_planner::vmap::VectorMap fine_mostleft_vmap =
		lane_planner::vmap::create_fine_vmap(lane_vmap, lane_planner::vmap::LNO_MOSTLEFT, coarse_vmap,
						     search_radius, waypoint_max);
	if (fine_mostleft_vmap.points.size() < 2)
		return;
	fine_vmaps.push_back(fine_mostleft_vmap);

	int lcnt = count_lane(fine_mostleft_vmap);
	for (int i = lane_planner::vmap::LNO_MOSTLEFT + 1; i <= lcnt; ++i) {
		lane_planner::vmap::VectorMap v =
			lane_planner::vmap::create_fine_vmap(lane_vmap, i, coarse_vmap, search_radius, waypoint_max);
		if (v.points.size() < 2)
			continue;
		fine_vmaps.push_back(v);
	}

	autoware_msgs::LaneArray lane_waypoint;
	for (const lane_planner::vmap::VectorMap& v : fine_vmaps) {
		autoware_msgs::Lane l;
		l.header = header;
		l.increment = 1;

		size_t last_index = v.points.size() - 1;
		for (size_t i = 0; i < v.points.size(); ++i) {
			double yaw;
			if (i == last_index) {
				geometry_msgs::Point p1 =
					lane_planner::vmap::create_geometry_msgs_point(v.points[i]);
				geometry_msgs::Point p2 =
					lane_planner::vmap::create_geometry_msgs_point(v.points[i - 1]);
				yaw = atan2(p2.y - p1.y, p2.x - p1.x);
				yaw -= M_PI;
			} else {
				geometry_msgs::Point p1 =
					lane_planner::vmap::create_geometry_msgs_point(v.points[i]);
				geometry_msgs::Point p2 =
					lane_planner::vmap::create_geometry_msgs_point(v.points[i + 1]);
				yaw = atan2(p2.y - p1.y, p2.x - p1.x);
			}

			autoware_msgs::Waypoint w;
			w.pose.header = header;
			w.pose.pose.position = lane_planner::vmap::create_geometry_msgs_point(v.points[i]);
			w.pose.pose.orientation = tf::createQuaternionMsgFromYaw(yaw);
			w.twist.header = header;
			w.twist.twist.linear.x = velocity / 3.6; // to m/s
			l.waypoints.push_back(w);
		}
		lane_waypoint.lanes.push_back(l);
	}
	waypoint_pub.publish(lane_waypoint);

	for (size_t i = 0; i < fine_vmaps.size(); ++i) {
		std::stringstream ss;
		ss << "_" << i;

		std::vector<std::string> v1 = split(output_file, '/');
		std::vector<std::string> v2 = split(v1.back(), '.');
		v2[0] = v2.front() + ss.str();
		v1[v1.size() - 1] = join(v2, '.');
		std::string path = join(v1, '/');

		lane_planner::vmap::write_waypoints(fine_vmaps[i].points, velocity, path);
	}
}

void update_values()
{
	if (all_vmap.points.empty() || all_vmap.lanes.empty() || all_vmap.nodes.empty())
		return;

	lane_vmap = lane_planner::vmap::create_lane_vmap(all_vmap, lane_planner::vmap::LNO_ALL);

	if (!cached_route.point.empty()) {
		create_waypoint(cached_route);
		cached_route.point.clear();
		cached_route.point.shrink_to_fit();
	}
}

void cache_point(const vector_map::PointArray& msg)
{
	all_vmap.points = msg.data;
	update_values();
}

void cache_lane(const vector_map::LaneArray& msg)
{
	all_vmap.lanes = msg.data;
	update_values();
}

void cache_node(const vector_map::NodeArray& msg)
{
	all_vmap.nodes = msg.data;
	update_values();
}

} // namespace

int main(int argc, char **argv)
{
	ros::init(argc, argv, "lane_navi");

	ros::NodeHandle n;

	int sub_vmap_queue_size;
	n.param<int>("/lane_navi/sub_vmap_queue_size", sub_vmap_queue_size, 1);
	int sub_route_queue_size;
	n.param<int>("/lane_navi/sub_route_queue_size", sub_route_queue_size, 1);
	int pub_waypoint_queue_size;
	n.param<int>("/lane_navi/pub_waypoint_queue_size", pub_waypoint_queue_size, 1);
	bool pub_waypoint_latch;
	n.param<bool>("/lane_navi/pub_waypoint_latch", pub_waypoint_latch, true);

	n.param<int>("/lane_navi/waypoint_max", waypoint_max, 10000);
	n.param<double>("/lane_navi/search_radius", search_radius, 10);
	n.param<double>("/lane_navi/velocity", velocity, 40);
	n.param<std::string>("/lane_navi/frame_id", frame_id, "map");
	n.param<std::string>("/lane_navi/output_file", output_file, "/tmp/lane_waypoint.csv");

	if (output_file.empty()) {
		ROS_ERROR_STREAM("output filename is empty");
		return EXIT_FAILURE;
	}
	if (output_file.back() == '/') {
		ROS_ERROR_STREAM(output_file << " is directory");
		return EXIT_FAILURE;
	}

	waypoint_pub = n.advertise<autoware_msgs::LaneArray>("/lane_waypoints_array", pub_waypoint_queue_size,
								 pub_waypoint_latch);

	ros::Subscriber route_sub = n.subscribe("/route_cmd", sub_route_queue_size, create_waypoint);
	ros::Subscriber point_sub = n.subscribe("/vector_map_info/point", sub_vmap_queue_size, cache_point);
	ros::Subscriber lane_sub = n.subscribe("/vector_map_info/lane", sub_vmap_queue_size, cache_lane);
	ros::Subscriber node_sub = n.subscribe("/vector_map_info/node", sub_vmap_queue_size, cache_node);

	ros::spin();

	return EXIT_SUCCESS;
}
