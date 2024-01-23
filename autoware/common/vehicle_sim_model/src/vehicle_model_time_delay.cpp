
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

#include "vehicle_sim_model/vehicle_model_time_delay.h"
#include <algorithm>

/*
 *
 * VehicleModelTimeDelayTwist
 *
 */

VehicleModelTimeDelayTwist::VehicleModelTimeDelayTwist(
  double vx_lim, double wz_lim, double vx_rate_lim, double wz_rate_lim,
  double dt, double vx_delay, double vx_time_constant, double wz_delay,
  double wz_time_constant)
  : VehicleModelInterface(5 /* dim x */, 2 /* dim u */)
  , MIN_TIME_CONSTANT(0.03)
  , vx_lim_(vx_lim)
  , vx_rate_lim_(vx_rate_lim)
  , wz_lim_(wz_lim)
  , wz_rate_lim_(wz_rate_lim)
  , vx_delay_(vx_delay)
  , vx_time_constant_(std::max(vx_time_constant, MIN_TIME_CONSTANT))
  , wz_delay_(wz_delay)
  , wz_time_constant_(std::max(wz_time_constant, MIN_TIME_CONSTANT))
{
  if (vx_time_constant < MIN_TIME_CONSTANT)
  {
    std::cerr << "Settings vx_time_constant is too small, replace it by "
      << MIN_TIME_CONSTANT << std::endl;
  }
  if (wz_time_constant < MIN_TIME_CONSTANT)
  {
    std::cerr << "Settings wz_time_constant is too small, replace it by "
      << MIN_TIME_CONSTANT << std::endl;
  }
  initializeInputQueue(dt);
}

const double VehicleModelTimeDelayTwist::getX() const
{
  return state_(static_cast<Eigen::Index>(IDX::X));
}
const double VehicleModelTimeDelayTwist::getY() const
{
  return state_(static_cast<Eigen::Index>(IDX::Y));
}
const double VehicleModelTimeDelayTwist::getYaw() const
{
  return state_(static_cast<Eigen::Index>(IDX::YAW));
}
const double VehicleModelTimeDelayTwist::getVx() const
{
  return state_(static_cast<Eigen::Index>(IDX::VX));
}
const double VehicleModelTimeDelayTwist::getWz() const
{
  return state_(static_cast<Eigen::Index>(IDX::WZ));
}
const double VehicleModelTimeDelayTwist::getSteer() const
{
  return 0.0;
}
void VehicleModelTimeDelayTwist::update(const double& dt)
{
  Eigen::VectorXd delayed_input = Eigen::VectorXd::Zero(dim_u_);

  vx_input_queue_.push_back(input_(static_cast<Eigen::Index>(IDX_U::VX_DES)));
  delayed_input(static_cast<Eigen::Index>(IDX_U::VX_DES)) = vx_input_queue_.front();
  vx_input_queue_.pop_front();
  wz_input_queue_.push_back(input_(static_cast<Eigen::Index>(IDX_U::WZ_DES)));
  delayed_input(static_cast<Eigen::Index>(IDX_U::WZ_DES)) = wz_input_queue_.front();
  wz_input_queue_.pop_front();

  updateRungeKutta(dt, delayed_input);
}
void VehicleModelTimeDelayTwist::initializeInputQueue(const double& dt)
{
  size_t vx_input_queue_size = static_cast<size_t>(round(vx_delay_ / dt));
  for (size_t i = 0; i < vx_input_queue_size; i++)
  {
    vx_input_queue_.push_back(0.0);
  }
  size_t wz_input_queue_size = static_cast<size_t>(round(wz_delay_ / dt));
  for (size_t i = 0; i < wz_input_queue_size; i++)
  {
    wz_input_queue_.push_back(0.0);
  }
}

Eigen::VectorXd VehicleModelTimeDelayTwist::calcModel(const Eigen::VectorXd& state, const Eigen::VectorXd& input)
{
  const double vx = state(static_cast<Eigen::Index>(IDX::VX));
  const double wz = state(static_cast<Eigen::Index>(IDX::WZ));
  const double yaw = state(static_cast<Eigen::Index>(IDX::YAW));
  const double delay_input_vx = input(static_cast<Eigen::Index>(IDX_U::VX_DES));
  const double delay_input_wz = input(static_cast<Eigen::Index>(IDX_U::WZ_DES));
  const double delay_vx_des = std::max(std::min(delay_input_vx, vx_lim_), -vx_lim_);
  const double delay_wz_des = std::max(std::min(delay_input_wz, wz_lim_), -wz_lim_);
  double vx_rate = -(vx - delay_vx_des) / vx_time_constant_;
  double wz_rate = -(wz - delay_wz_des) / wz_time_constant_;
  vx_rate = std::min(vx_rate_lim_, std::max(-vx_rate_lim_, vx_rate));
  wz_rate = std::min(wz_rate_lim_, std::max(-wz_rate_lim_, wz_rate));

  Eigen::VectorXd d_state = Eigen::VectorXd::Zero(dim_x_);
  d_state(static_cast<Eigen::Index>(IDX::X)) = vx * cos(yaw);
  d_state(static_cast<Eigen::Index>(IDX::Y)) = vx * sin(yaw);
  d_state(static_cast<Eigen::Index>(IDX::YAW)) = wz;
  d_state(static_cast<Eigen::Index>(IDX::VX)) = vx_rate;
  d_state(static_cast<Eigen::Index>(IDX::WZ)) = wz_rate;

  return d_state;
};

/*
 *
 * VehicleModelTimeDelaySteer
 *
 */
VehicleModelTimeDelaySteer::VehicleModelTimeDelaySteer(
  double vx_lim, double steer_lim, double vx_rate_lim,
  double steer_rate_lim, double wheelbase, double dt, double vx_delay,
  double vx_time_constant, double steer_delay,
  double steer_time_constant)
  : VehicleModelInterface(5 /* dim x */, 2 /* dim u */)
  , MIN_TIME_CONSTANT(0.03)
  , vx_lim_(vx_lim)
  , vx_rate_lim_(vx_rate_lim)
  , steer_lim_(steer_lim)
  , steer_rate_lim_(steer_rate_lim)
  , wheelbase_(wheelbase)
  , vx_delay_(vx_delay)
  , vx_time_constant_(std::max(vx_time_constant, MIN_TIME_CONSTANT))
  , steer_delay_(steer_delay)
  , steer_time_constant_(std::max(steer_time_constant, MIN_TIME_CONSTANT))
{
  if (vx_time_constant < MIN_TIME_CONSTANT)
  {
    std::cerr << "Settings vx_time_constant is too small, replace it by "
      << MIN_TIME_CONSTANT << std::endl;
  }
  if (steer_time_constant < MIN_TIME_CONSTANT)
  {
    std::cerr << "Settings wz_time_constant is too small, replace it by "
      << MIN_TIME_CONSTANT << std::endl;
  }

  initializeInputQueue(dt);
}

const double VehicleModelTimeDelaySteer::getX() const
{
  return state_(static_cast<Eigen::Index>(IDX::X));
}
const double VehicleModelTimeDelaySteer::getY() const
{
  return state_(static_cast<Eigen::Index>(IDX::Y));
}
const double VehicleModelTimeDelaySteer::getYaw() const
{
  return state_(static_cast<Eigen::Index>(IDX::YAW));
}
const double VehicleModelTimeDelaySteer::getVx() const
{
  return state_(static_cast<Eigen::Index>(IDX::VX));
}
const double VehicleModelTimeDelaySteer::getWz() const
{
  return state_(static_cast<Eigen::Index>(IDX::VX))
    * std::tan(state_(static_cast<Eigen::Index>(IDX::STEER))) / wheelbase_;
}
const double VehicleModelTimeDelaySteer::getSteer() const
{
  return state_(static_cast<Eigen::Index>(IDX::STEER));
}
void VehicleModelTimeDelaySteer::update(const double& dt)
{
  Eigen::VectorXd delayed_input = Eigen::VectorXd::Zero(dim_u_);

  vx_input_queue_.push_back(input_(static_cast<Eigen::Index>(IDX_U::VX_DES)));
  delayed_input(static_cast<Eigen::Index>(IDX_U::VX_DES)) = vx_input_queue_.front();
  vx_input_queue_.pop_front();
  steer_input_queue_.push_back(input_(static_cast<Eigen::Index>(IDX_U::STEER_DES)));
  delayed_input(static_cast<Eigen::Index>(IDX_U::STEER_DES)) = steer_input_queue_.front();
  steer_input_queue_.pop_front();

  updateRungeKutta(dt, delayed_input);
}
void VehicleModelTimeDelaySteer::initializeInputQueue(const double& dt)
{
  size_t vx_input_queue_size = static_cast<size_t>(round(vx_delay_ / dt));
  for (size_t i = 0; i < vx_input_queue_size; i++)
  {
    vx_input_queue_.push_back(0.0);
  }
  size_t steer_input_queue_size = static_cast<size_t>(round(steer_delay_ / dt));
  for (size_t i = 0; i < steer_input_queue_size; i++)
  {
    steer_input_queue_.push_back(0.0);
  }
}

Eigen::VectorXd VehicleModelTimeDelaySteer::calcModel(const Eigen::VectorXd& state, const Eigen::VectorXd& input)
{
  const double vel = state(static_cast<Eigen::Index>(IDX::VX));
  const double yaw = state(static_cast<Eigen::Index>(IDX::YAW));
  const double steer = state(static_cast<Eigen::Index>(IDX::STEER));
  const double delay_input_vel = input(static_cast<Eigen::Index>(IDX_U::VX_DES));
  const double delay_input_steer = input(static_cast<Eigen::Index>(IDX_U::STEER_DES));
  const double delay_vx_des = std::max(std::min(delay_input_vel, vx_lim_), -vx_lim_);
  const double delay_steer_des = std::max(std::min(delay_input_steer, steer_lim_), -steer_lim_);
  double vx_rate = -(vel - delay_vx_des) / vx_time_constant_;
  double steer_rate = -(steer - delay_steer_des) / steer_time_constant_;
  vx_rate = std::min(vx_rate_lim_, std::max(-vx_rate_lim_, vx_rate));
  steer_rate = std::min(steer_rate_lim_, std::max(-steer_rate_lim_, steer_rate));

  Eigen::VectorXd d_state = Eigen::VectorXd::Zero(dim_x_);
  d_state(static_cast<Eigen::Index>(IDX::X)) = vel * cos(yaw);
  d_state(static_cast<Eigen::Index>(IDX::Y)) = vel * sin(yaw);
  d_state(static_cast<Eigen::Index>(IDX::YAW)) = vel * std::tan(steer) / wheelbase_;
  d_state(static_cast<Eigen::Index>(IDX::VX)) = vx_rate;
  d_state(static_cast<Eigen::Index>(IDX::STEER)) = steer_rate;

  return d_state;
}
