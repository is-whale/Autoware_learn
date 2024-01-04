#ifndef EMERGENCY_HANDLER_SYSTEM_STATUS_FILTER_H
#define EMERGENCY_HANDLER_SYSTEM_STATUS_FILTER_H
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
#include <emergency_handler/libsystem_status_filter.h>

class SimpleHardwareFilter : public SystemStatusFilter
{
public:
  int selectPriority(std::shared_ptr<SystemStatus> const status);
};

class SimpleNodeFilter : public SystemStatusFilter
{
public:
  int selectPriority(std::shared_ptr<SystemStatus> const status);
};

#endif  //  EMERGENCY_HANDLER_SYSTEM_STATUS_FILTER_H
