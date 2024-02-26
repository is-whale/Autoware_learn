#include <decision_maker_node.hpp>

namespace decision_maker
{
void DecisionMakerNode::updateWaitDriveReadyState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateWaitEngageState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateMotionEmergencyState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::entryDriveState(cstring_t& state_name, int status)
{
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
  }

  if (current_status_.found_stopsign_idx != -1 || current_status_.ordered_stop_idx != -1)
  {
    tryNextState("found_stop_decision");
  }
  else
  {
    tryNextState("clear");
  }
}

void DecisionMakerNode::updateDriveState(cstring_t& state_name, int status)
{
  if (isArrivedGoal())
  {
    tryNextState("arrived_goal");
    return;
  }

  if (current_status_.closest_waypoint == -1)
  {
    publishOperatorHelpMessage("The vehicle passed last waypoint or waypoint does not exist near the vehicle.");
    tryNextState("mission_aborted");
    return;
  }

  if (current_status_.finalwaypoints.waypoints.empty())
  {
    ROS_WARN("\"/final_waypoints.\" is not contain waypoints");
    return;
  }
}

// Iterate every waypoint in received /final_waypoints and find the first
// stop sign waypoint within certain search distance. If it is found, its
// gid and stop sign state (either TYPE_STOPLINE or TYPE_STOP) is returned
// as a std::pair. Otherwise, a pair of (NULLSTATE, -1) is returned.
std::pair<uint8_t, int> DecisionMakerNode::getStopSignStateFromWaypoint(void)
{
  static const double mu = 0.7;  // dry ground/ asphalt/ normal tire
  static const double g = 9.80665;
  static const double margin = 5;
  static const double reaction_time = 0.3 + margin;  // system delay(sec)

  const double velocity = amathutils::kmph2mps(current_status_.velocity);

  const double free_running_distance = reaction_time * velocity;
  const double braking_distance = velocity * velocity / (2 * g * mu);
  const double distance_to_target = (free_running_distance + braking_distance) * 2 /* safety margin*/;

  std::pair<uint8_t, int> ret(autoware_msgs::WaypointState::NULLSTATE, -1);

  double distance = 0.0;
  geometry_msgs::Pose prev_pose = current_status_.pose;

  // Index 0 holds ego-vehicle's current pose
  if (current_status_.finalwaypoints.waypoints.size() < 3)
  {
    return ret;
  }

  // reset previous stop sign waypoint
  if (current_status_.finalwaypoints.waypoints.at(1).gid > current_status_.prev_stopped_wpidx ||
      (unsigned int)(current_status_.prev_stopped_wpidx - current_status_.finalwaypoints.waypoints.at(1).gid) >
          stopline_reset_count_)
  {
    current_status_.prev_stopped_wpidx = -1;
  }

  // start from index 1 since index 0 holds ego-vehicle's current pose.
  for (size_t idx = 1; idx < current_status_.finalwaypoints.waypoints.size() - 1; ++idx)
  {
    const autoware_msgs::Waypoint &current_wpt = current_status_.finalwaypoints.waypoints.at(idx);
    distance += amathutils::find_distance(prev_pose, current_wpt.pose.pose);

    if (current_wpt.wpstate.stop_state != autoware_msgs::WaypointState::NULLSTATE)
    {
      if (current_status_.prev_stopped_wpidx != current_wpt.gid)
      {
        ret.first = current_wpt.wpstate.stop_state;
        ret.second = current_wpt.gid;
        break;
      }
    }

    if (distance > distance_to_target)
    {
      break;
    }

    prev_pose = current_wpt.pose.pose;
  }
  return ret;
}

void DecisionMakerNode::entryGoState(cstring_t& state_name, int status)
{
  publishStoplineWaypointIdx(-1);
}
void DecisionMakerNode::updateGoState(cstring_t& state_name, int status)
{
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
  }

  int obstacle_waypoint_gid = current_status_.obstacle_waypoint + current_status_.closest_waypoint;

  if (get_stopsign.first != 0 && current_status_.found_stopsign_idx != -1)
  {
    if (current_status_.obstacle_waypoint == -1 || current_status_.found_stopsign_idx <= obstacle_waypoint_gid)
    {
      tryNextState("found_stop_decision");
    }
  }

  if (current_status_.ordered_stop_idx != -1 && calcRequiredDistForStop() > getDistToWaypointIdx(current_status_.ordered_stop_idx))
  {
    if (current_status_.obstacle_waypoint == -1 || current_status_.ordered_stop_idx <= obstacle_waypoint_gid)
    {
      tryNextState("found_stop_decision");
    }
  }

}

void DecisionMakerNode::updateWaitState(cstring_t& state_name, int status)
{
  publishStoplineWaypointIdx(current_status_.finalwaypoints.waypoints.at(2).gid);
}

void DecisionMakerNode::updateStopState(cstring_t& state_name, int status)
{
  int obstacle_waypoint_gid = current_status_.obstacle_waypoint + current_status_.closest_waypoint;
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
  }

  if (current_status_.obstacle_waypoint != -1)
  {
    if ((current_status_.found_stopsign_idx != -1 && current_status_.found_stopsign_idx >= obstacle_waypoint_gid)
        || (current_status_.ordered_stop_idx != -1 && current_status_.ordered_stop_idx >= obstacle_waypoint_gid))
      tryNextState("clear");
  }

  if (get_stopsign.first != 0 && current_status_.found_stopsign_idx != -1)
  {
    if (current_status_.ordered_stop_idx == -1 || current_status_.found_stopsign_idx < current_status_.ordered_stop_idx)
    {
      switch (get_stopsign.first) {
        case autoware_msgs::WaypointState::TYPE_STOPLINE:
          tryNextState("found_stopline");
          break;
        case autoware_msgs::WaypointState::TYPE_STOP:
          tryNextState("found_reserved_stop");
          break;
        default:
          break;
      }
      return;
    }
  }

  if (current_status_.ordered_stop_idx != -1)
  {
    if (current_status_.found_stopsign_idx == -1 || current_status_.ordered_stop_idx <= current_status_.found_stopsign_idx)
    {
      tryNextState("received_stop_order");
      return;
    }
  }

}

void DecisionMakerNode::updateStoplineState(cstring_t& state_name, int status)
{
  publishStoplineWaypointIdx(current_status_.found_stopsign_idx);
  /* wait for clearing risk*/

  static bool timerflag = false;
  static ros::Timer stopping_timer;

  if (fabs(current_status_.velocity) <= stopped_vel_ && !timerflag && current_status_.stopline_waypoint != -1 && (current_status_.stopline_waypoint + current_status_.closest_waypoint) == current_status_.found_stopsign_idx)
  {
    stopping_timer = nh_.createTimer(ros::Duration(0.5),
                                     [&](const ros::TimerEvent&) {
                                       timerflag = false;
                                       current_status_.prev_stopped_wpidx = current_status_.found_stopsign_idx;
                                       current_status_.found_stopsign_idx = -1;
                                       if (current_status_.ordered_stop_idx != -1)
                                        tryNextState("received_stop_order");
                                      else
                                        tryNextState("clear");
                                       /*if found risk,
                                        * tryNextState("wait");*/
                                     },
                                     this, true);
    timerflag = true;
  }
}

void DecisionMakerNode::updateOrderedStopState(cstring_t& state_name, int status)
{
  if (current_status_.ordered_stop_idx == -1 || current_status_.closest_waypoint > current_status_.ordered_stop_idx)
  {
    tryNextState("clear");
  }
  else
  {
    publishStoplineWaypointIdx(current_status_.ordered_stop_idx);
  }
}

void DecisionMakerNode::exitOrderedStopState(cstring_t& state_name, int status)
{
  if (current_status_.found_stopsign_idx == -1 || current_status_.ordered_stop_idx < current_status_.found_stopsign_idx)
  {
    current_status_.ordered_stop_idx = -1;
  }
}

void DecisionMakerNode::updateReservedStopState(cstring_t& state_name, int status)
{
  publishStoplineWaypointIdx(current_status_.found_stopsign_idx);
}
void DecisionMakerNode::exitReservedStopState(cstring_t& state_name, int status)
{
  current_status_.prev_stopped_wpidx = current_status_.found_stopsign_idx;
  current_status_.found_stopsign_idx = -1;
}

}
