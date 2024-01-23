

## TODO
- op_simulation_package 模拟了聚类对象，加了噪点。这部分代码可以用在雷达获取的信息利用部分
- open planner 的思想可以借鉴
- 状态机部分要注意扩展性

## warning
部分仓库的版本可能不是1.14,planner,per,doc是1.14

## 函数记录

bool AstarAvoid::planAvoidWaypoints(int &end_of_avoid_index)

该C++函数是类AstarAvoid中的一部分，用于规划一条避开障碍物的路径。函数的主要逻辑如下：
初始化标志变量found_path为false，表示尚未找到有效路径。
计算离当前全局位姿最近的避障路点索引closest_waypoint_index。
使用循环，以步长search_waypoints_delta_逐步更新目标路点索引（从一个避障路点开始，并加上一个与障碍物相关的偏移量obstacle_waypoint_index_）。
在每次循环内：
更新目标路点索引goal_waypoint_index，并检查是否越界。
根据新的目标路点索引获取目标全局位姿goal_pose_global_。
将目标全局位姿转换为目标局部位姿goal_pose_local_，并设置其header信息。
使用A*算法类astar_初始化成本图。
执行A*搜索，从当前局部位姿到目标局部位姿规划路径，并将结果存储在found_path变量中。
如果找到路径，则发布规划路径到名为“debug”的ROS话题，并更新end_of_avoid_index为当前目标路点索引。
调用mergeAvoidWaypoints函数处理避障路径点。
若处理后的避障路点集合非空，则输出提示信息并返回true，同时重置A*算法状态。
若处理后避障路点集合为空，则恢复found_path为false。
若循环结束后仍未找到路径，则输出错误信息并返回false。
总之，此函数通过迭代方式调用A*算法，在一系列预设的目标路点上寻找可行的避开障碍物的路径，成功找到则返回对应的路点索引和规划路径，并进行后续处理，否则返回未找到路径的信息。
search_waypoints_delta_  AstarAvoid类的成员变量，用于设置每次搜索的步长，单位米。
getLocalClosestWaypoint  路线上和车辆距离最小的路点