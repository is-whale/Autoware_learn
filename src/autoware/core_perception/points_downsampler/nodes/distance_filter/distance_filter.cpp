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

#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>

#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/voxel_grid.h>

#include "autoware_config_msgs/ConfigDistanceFilter.h"

#include <points_downsampler/PointsDownsamplerInfo.h>

#include <algorithm> // For std::min()
#include <chrono>

#include "points_downsampler.h"

#define MAX_MEASUREMENT_RANGE 200.0

ros::Publisher filtered_points_pub;

static int sample_num = 1000;

static ros::Publisher points_downsampler_info_pub;
static points_downsampler::PointsDownsamplerInfo points_downsampler_info_msg;

static std::chrono::time_point<std::chrono::system_clock> filter_start, filter_end;

static bool _output_log = false;
static std::ofstream ofs;
static std::string filename;

static std::string POINTS_TOPIC;
static double measurement_range = MAX_MEASUREMENT_RANGE;

static void config_callback(const autoware_config_msgs::ConfigDistanceFilter::ConstPtr& input)
{
  sample_num = input->sample_num;
  measurement_range = input->measurement_range;
}

static void scan_callback(const sensor_msgs::PointCloud2::ConstPtr& input)
{
  pcl::PointXYZI sampled_p;
  pcl::PointCloud<pcl::PointXYZI> scan;

  pcl::fromROSMsg(*input, scan);

  if(measurement_range != MAX_MEASUREMENT_RANGE){
    scan = removePointsByRange(scan, 0, measurement_range);
  }

  pcl::PointCloud<pcl::PointXYZI>::Ptr filtered_scan_ptr(new pcl::PointCloud<pcl::PointXYZI>());
  filtered_scan_ptr->header = scan.header;

  int points_num = scan.size();

  double w_total = 0.0;
  double w_step = 0.0;
  int m = 0;
  double c = 0.0;

  filter_start = std::chrono::system_clock::now();

  for (pcl::PointCloud<pcl::PointXYZI>::const_iterator item = scan.begin(); item != scan.end(); item++)
  {
    w_total += item->x * item->x + item->y * item->y + item->z * item->z;
  }
  w_step = w_total / sample_num;

  pcl::PointCloud<pcl::PointXYZI>::const_iterator item = scan.begin();
  for (m = 0; m < sample_num; m++)
  {
    while (m * w_step > c)
    {
      item++;
      c += item->x * item->x + item->y * item->y + item->z * item->z;
    }
    sampled_p.x = item->x;
    sampled_p.y = item->y;
    sampled_p.z = item->z;
    sampled_p.intensity = item->intensity;
    filtered_scan_ptr->points.push_back(sampled_p);
  }

  sensor_msgs::PointCloud2 filtered_msg;
  pcl::toROSMsg(*filtered_scan_ptr, filtered_msg);

  filter_end = std::chrono::system_clock::now();

  filtered_msg.header = input->header;
  filtered_points_pub.publish(filtered_msg);

  points_downsampler_info_msg.header = input->header;
  points_downsampler_info_msg.filter_name = "distance_filter";
  points_downsampler_info_msg.measurement_range = measurement_range;
  points_downsampler_info_msg.original_points_size = points_num;
  points_downsampler_info_msg.filtered_points_size = std::min((int)filtered_scan_ptr->size(), points_num);
  points_downsampler_info_msg.original_ring_size = 0;
  points_downsampler_info_msg.original_ring_size = 0;
  points_downsampler_info_msg.exe_time = std::chrono::duration_cast<std::chrono::microseconds>(filter_end - filter_start).count() / 1000.0;
  points_downsampler_info_pub.publish(points_downsampler_info_msg);

  if(_output_log == true){
	  if(!ofs){
		  std::cerr << "Could not open " << filename << "." << std::endl;
		  exit(1);
	  }
	  ofs << points_downsampler_info_msg.header.seq << ","
		  << points_downsampler_info_msg.header.stamp << ","
		  << points_downsampler_info_msg.header.frame_id << ","
		  << points_downsampler_info_msg.filter_name << ","
		  << points_downsampler_info_msg.original_points_size << ","
		  << points_downsampler_info_msg.filtered_points_size << ","
		  << points_downsampler_info_msg.original_ring_size << ","
		  << points_downsampler_info_msg.filtered_ring_size << ","
		  << points_downsampler_info_msg.exe_time << ","
		  << std::endl;
  }

}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "distance_filter");

  ros::NodeHandle nh;
  ros::NodeHandle private_nh("~");

  private_nh.getParam("points_topic", POINTS_TOPIC);
  private_nh.getParam("output_log", _output_log);
  if(_output_log == true){
	  char buffer[80];
	  std::time_t now = std::time(NULL);
	  std::tm *pnow = std::localtime(&now);
	  std::strftime(buffer,80,"%Y%m%d_%H%M%S",pnow);
	  filename = "distance_filter_" + std::string(buffer) + ".csv";
	  ofs.open(filename.c_str(), std::ios::app);
  }

  // Publishers
  filtered_points_pub = nh.advertise<sensor_msgs::PointCloud2>("/filtered_points", 10);
  points_downsampler_info_pub = nh.advertise<points_downsampler::PointsDownsamplerInfo>("/points_downsampler_info", 1000);

  // Subscribers
  ros::Subscriber config_sub = nh.subscribe("config/distance_filter", 10, config_callback);
  ros::Subscriber scan_sub = nh.subscribe(POINTS_TOPIC, 10, scan_callback);

  ros::spin();

  return 0;
}
