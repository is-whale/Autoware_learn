/*
 * Copyright 2018-2019 Autoware Foundation. All rights reserved.
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

#include "twist_generator/vehicle_status_converter.hpp"

VehicleStatusConverter::VehicleStatusConverter() : nh_(""), pnh_("~")
{
  pub_twist_ = nh_.advertise<geometry_msgs::TwistStamped>("/can_twist", 1);
  pub_correction_coeff_ = pnh_.advertise<std_msgs::Float32MultiArray>("convert_correction_coeff", 1);
  sub_vehicle_status_ = nh_.subscribe("/vehicle_status", 1, &VehicleStatusConverter::callbackVehicleStatus, this);
  sub_estimate_twist_ = nh_.subscribe("/estimate_twist", 1, &VehicleStatusConverter::callbackEstimateTwist, this);

  pnh_.param("wheelbase", wheelbase_, double(2.9));
  pnh_.param("enable_adaptive_estimate", enable_adaptive_estimate_, bool(false));
  pnh_.param("enable_steering_offset_estimate", enable_steering_offset_estimate_, bool(false));

  if (wheelbase_ < 1.0E-5)
  {
    ROS_WARN("undesired wheelbase value : %f, set to 1.0", wheelbase_);
    wheelbase_ = 1.0;
  }
  ROS_INFO("set as\n * wheelbase: %f", wheelbase_);

  adaptive_coefficient_wz_ = 1.0; // adaptive coefficient for angular velocity calculaton
  adaptive_coefficient_vx_ = 1.0; // adaptive coefficient for angular velocity calculaton
  steering_offset_ = 0.0;          // steering offset
  Pn_wz_ = 1000.0;                // initial covariance
  Pn_vx_ = 1000.0;                // initial covariance
  Pn_so_ = 1000.0;                // initial_covariance
  rho_ = 0.999;                   // forgetting factor
  steering_offset_lim_ = amathutils::deg2rad(5.0);            
};

VehicleStatusConverter::~VehicleStatusConverter(){};

void VehicleStatusConverter::callbackVehicleStatus(const autoware_msgs::VehicleStatus &msg)
{
  static const double KMPH2MPS = 1000.0 / 3600.0;
  current_vehicle_status_ = msg;
  const double vel_mps = msg.speed * KMPH2MPS; // convert from [km/h] to [m/s]
  const double steer_rad = msg.angle;          // tire angle [rad]

  geometry_msgs::TwistStamped twist_stamped;
  twist_stamped.header.stamp = msg.header.stamp;
  twist_stamped.header.frame_id = "base_link";
  twist_stamped.twist.linear.x = vel_mps;
  twist_stamped.twist.angular.z = enable_steering_offset_estimate_ ? vel_mps * std::tan(steer_rad + steering_offset_) / wheelbase_:
                                                                    vel_mps * std::tan(steer_rad) / wheelbase_;
  if (enable_adaptive_estimate_)
  {
    twist_stamped.twist.linear.x *= adaptive_coefficient_vx_;
    twist_stamped.twist.angular.z *= adaptive_coefficient_wz_;
  }
  pub_twist_.publish(twist_stamped);
}

void VehicleStatusConverter::callbackEstimateTwist(const geometry_msgs::TwistStamped &estimate_twist)
{
  static const double KMPH2MPS = 1000.0 / 3600.0;
  const double vel_mps = current_vehicle_status_.speed * KMPH2MPS;
  const double steer_rad = current_vehicle_status_.angle;

  if (vel_mps > 1.0) // update only when estimate angular vel is reliable enough
  {
    updateAdaptiveCoeffVel(estimate_twist.twist.linear.x, vel_mps);
    updateAdaptiveCoeffAngvel(estimate_twist.twist.angular.z, vel_mps, steer_rad);
    // The error between tan(x) and x is about 1 % when x is 10 degree.
    if (std::abs(steer_rad + steering_offset_) < amathutils::deg2rad(10.0)){
      updateSteeringOffset(estimate_twist.twist.angular.z, vel_mps, steer_rad);
    }
  }

  std_msgs::Float32MultiArray msg;
  msg.data.push_back(adaptive_coefficient_vx_);
  msg.data.push_back(adaptive_coefficient_wz_);
  msg.data.push_back(steering_offset_);
  pub_correction_coeff_.publish(msg);
}

void VehicleStatusConverter::updateAdaptiveCoeffAngvel(const double &w_ndt, const double &vel, const double &steer)
{
  /* estimate angular velocity correction coeff by Recursive Least Squares Method */
  const double zn = enable_steering_offset_estimate_ ? vel * std::tan(steer + steering_offset_) / wheelbase_:
                                                      vel * std::tan(steer) / wheelbase_;
  const double den = rho_ + zn * Pn_wz_ * zn;
  Pn_wz_ = (Pn_wz_ - (Pn_wz_ * zn * zn * Pn_wz_) / den) / rho_; // update estimate variance
  const double temp = adaptive_coefficient_wz_ + (Pn_wz_ * zn / den) * (w_ndt - zn * adaptive_coefficient_wz_);
  adaptive_coefficient_wz_ = std::max(std::min(temp, 1.5), 0.5); // limit changes
}

void VehicleStatusConverter::updateAdaptiveCoeffVel(const double &v_ndt, const double &vel)
{
  /* estimate velocity correction coeff by Recursive Least Squares Method */
  const double zn = vel;
  const double den = rho_ + zn * Pn_vx_ * zn;
  Pn_vx_ = (Pn_vx_ - (Pn_vx_ * zn * zn * Pn_vx_) / den) / rho_; // update estimate variance
  const double temp = adaptive_coefficient_vx_ + (Pn_vx_ * zn / den) * (v_ndt - zn * adaptive_coefficient_vx_);
  adaptive_coefficient_vx_ = std::max(std::min(temp, 1.5), 0.5); // limit changes
}

void VehicleStatusConverter::updateSteeringOffset(const double &w_ndt, const double &vel, const double &steer)
{
  /* estimate steering offset by Recursive Least Squares Method */
  const double zn = vel * adaptive_coefficient_wz_ / wheelbase_;
  const double den = rho_ + zn * Pn_so_ * zn;
  Pn_so_ = (Pn_so_ - (Pn_so_ * zn * zn * Pn_so_) / den) / rho_; // update estimate variance
  const double temp = steering_offset_ + (Pn_so_ * zn / den) * (w_ndt - vel * steer * adaptive_coefficient_wz_ / wheelbase_  - zn * steering_offset_);
  steering_offset_ = std::max(std::min(temp, steering_offset_lim_), -steering_offset_lim_); // limit changes
}
