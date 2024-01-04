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

#include <geometry_msgs/PointStamped.h>
#include <ros/console.h>
#include <tf/transform_listener.h>

#include <vector_map/vector_map.h>

#include <lane_planner/lane_planner_vmap.hpp>

namespace {

int waypoint_max;
double search_radius; // meter
double velocity; // km/h
std::string frame_id;
std::string output_file;

visualization_msgs::Marker waypoint_marker;
visualization_msgs::Marker branching_marker;
visualization_msgs::Marker merging_marker;
visualization_msgs::Marker selection_marker;
visualization_msgs::Marker route_marker;
ros::Publisher marker_pub;
tf::StampedTransform transform;

lane_planner::vmap::VectorMap all_vmap;
lane_planner::vmap::VectorMap lane_vmap;
lane_planner::vmap::VectorMap coarse_vmap;

void create_route(const geometry_msgs::PointStamped& msg)
{
	if (all_vmap.points.empty() || all_vmap.lanes.empty() || all_vmap.nodes.empty())
		return;

	geometry_msgs::Point point;
	point.x = msg.point.x + transform.getOrigin().x();
	point.y = msg.point.y + transform.getOrigin().y();
	point.z = msg.point.z + transform.getOrigin().z();
	coarse_vmap.points.push_back(lane_planner::vmap::create_vector_map_point(point));
	lane_planner::vmap::publish_add_marker(marker_pub, selection_marker, coarse_vmap.points);

	if (coarse_vmap.points.size() < 2)
		return;

	lane_planner::vmap::VectorMap fine_vmap =
		lane_planner::vmap::create_fine_vmap(lane_vmap, lane_planner::vmap::LNO_ALL, coarse_vmap,
						     search_radius, waypoint_max);
	if (fine_vmap.points.size() < 2)
		return;

	lane_planner::vmap::publish_add_marker(marker_pub, route_marker, fine_vmap.points);

	lane_planner::vmap::write_waypoints(fine_vmap.points, velocity, output_file);
}

void update_values()
{
	if (all_vmap.points.empty() || all_vmap.lanes.empty() || all_vmap.nodes.empty())
		return;

	lane_vmap = lane_planner::vmap::create_lane_vmap(all_vmap, lane_planner::vmap::LNO_ALL);
	coarse_vmap.points.clear();
	coarse_vmap.points.shrink_to_fit();

	lane_planner::vmap::publish_delete_marker(marker_pub, waypoint_marker);
	lane_planner::vmap::publish_delete_marker(marker_pub, branching_marker);
	lane_planner::vmap::publish_delete_marker(marker_pub, merging_marker);
	lane_planner::vmap::publish_delete_marker(marker_pub, selection_marker);
	lane_planner::vmap::publish_delete_marker(marker_pub, route_marker);

	lane_planner::vmap::publish_add_marker(marker_pub, waypoint_marker, lane_vmap.points);
	lane_planner::vmap::publish_add_marker(marker_pub, branching_marker,
					       lane_planner::vmap::create_branching_points(lane_vmap));
	lane_planner::vmap::publish_add_marker(marker_pub, merging_marker,
					       lane_planner::vmap::create_merging_points(lane_vmap));
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
	ros::init(argc, argv, "waypoint_clicker");

	ros::NodeHandle n;

	int sub_vmap_queue_size;
	n.param<int>("/waypoint_clicker/sub_vmap_queue_size", sub_vmap_queue_size, 1);
	int sub_pose_queue_size;
	n.param<int>("/waypoint_clicker/sub_pose_queue_size", sub_pose_queue_size, 1);
	int pub_marker_queue_size;
	n.param<int>("/waypoint_clicker/pub_marker_queue_size", pub_marker_queue_size, 10);
	bool pub_marker_latch;
	n.param<bool>("/waypoint_clicker/pub_marker_latch", pub_marker_latch, true);

	n.param<int>("/waypoint_clicker/waypoint_max", waypoint_max, 10000);
	n.param<double>("/waypoint_clicker/search_radius", search_radius, 10);
	n.param<double>("/waypoint_clicker/velocity", velocity, 40);
	n.param<std::string>("/waypoint_clicker/frame_id", frame_id, "map");
	n.param<std::string>("/waypoint_clicker/output_file", output_file, "/tmp/lane_waypoint.csv");

	if (output_file.empty()) {
		ROS_ERROR_STREAM("output filename is empty");
		return EXIT_FAILURE;
	}
	if (output_file.back() == '/') {
		ROS_ERROR_STREAM(output_file << " is directory");
		return EXIT_FAILURE;
	}

	waypoint_marker.header.frame_id = frame_id;
	waypoint_marker.ns = "waypoint";
	waypoint_marker.id = 0;
	waypoint_marker.type = visualization_msgs::Marker::SPHERE_LIST;
	waypoint_marker.scale.x = 0.2;
	waypoint_marker.scale.y = 0.2;
	waypoint_marker.color.r = 1;
	waypoint_marker.color.g = 1;
	waypoint_marker.color.b = 0;
	waypoint_marker.color.a = 1;
	waypoint_marker.frame_locked = true;

	branching_marker.header.frame_id = frame_id;
	branching_marker.ns = "branching";
	branching_marker.id = 0;
	branching_marker.type = visualization_msgs::Marker::SPHERE_LIST;
	branching_marker.scale.x = 0.3;
	branching_marker.scale.y = 0.3;
	branching_marker.color.r = 0;
	branching_marker.color.g = 1;
	branching_marker.color.b = 0;
	branching_marker.color.a = 1;
	branching_marker.frame_locked = true;

	merging_marker.header.frame_id = frame_id;
	merging_marker.ns = "merging";
	merging_marker.id = 0;
	merging_marker.type = visualization_msgs::Marker::SPHERE_LIST;
	merging_marker.scale.x = 0.3;
	merging_marker.scale.y = 0.3;
	merging_marker.color.r = 1;
	merging_marker.color.g = 0;
	merging_marker.color.b = 0;
	merging_marker.color.a = 1;
	merging_marker.frame_locked = true;

	selection_marker.header.frame_id = frame_id;
	selection_marker.ns = "selection";
	selection_marker.id = 0;
	selection_marker.type = visualization_msgs::Marker::SPHERE_LIST;
	selection_marker.scale.x = 0.4;
	selection_marker.scale.y = 0.4;
	selection_marker.color.r = 1;
	selection_marker.color.g = 1;
	selection_marker.color.b = 0;
	selection_marker.color.a = 1;
	selection_marker.frame_locked = true;

	route_marker.header.frame_id = frame_id;
	route_marker.ns = "route";
	route_marker.id = 0;
	route_marker.type = visualization_msgs::Marker::LINE_STRIP;
	route_marker.scale.x = 0.2;
	route_marker.scale.y = 0.2;
	route_marker.color.r = 1;
	route_marker.color.g = 1;
	route_marker.color.b = 0;
	route_marker.color.a = 1;
	route_marker.frame_locked = true;

	marker_pub = n.advertise<visualization_msgs::Marker>("/waypoint_guide", pub_marker_queue_size,
							     pub_marker_latch);

	tf::TransformListener listener;
	try {
		ros::Time zero = ros::Time(0);
		listener.waitForTransform("map", "world", zero, ros::Duration(10));
		listener.lookupTransform("map", "world", zero, transform);
	} catch (tf::TransformException &ex) {
		ROS_ERROR_STREAM(ex.what());
	}

	ros::Subscriber pose_sub = n.subscribe("/clicked_point", sub_pose_queue_size, create_route);
	ros::Subscriber point_sub = n.subscribe("/vector_map_info/point", sub_vmap_queue_size, cache_point);
	ros::Subscriber lane_sub = n.subscribe("/vector_map_info/lane", sub_vmap_queue_size, cache_lane);
	ros::Subscriber node_sub = n.subscribe("/vector_map_info/node", sub_vmap_queue_size, cache_node);

	ros::spin();

	return EXIT_SUCCESS;
}
