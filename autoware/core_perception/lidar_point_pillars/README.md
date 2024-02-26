# Point Pillars for 3D Object Detection: ver. 1.0

Autoware package for Point Pillars.  [Referenced paper](https://arxiv.org/abs/1812.05784).

This node can be compiled either using cuDNN and TensorRT or by using TVM, the default are cuDNN and TensorRT.

## Requirements

- CUDA Toolkit v9.0 or v10.0

To compile the node using the cuDNN and TensorRT support the requirements are:

- cuDNN: Tested with v7.3.1

- TensorRT: Tested with 5.0.2 -> [How to install](https://docs.nvidia.com/deeplearning/sdk/tensorrt-install-guide/index.html#installing)

To compile the node using TVM support the requirements are:

- TVM runtime, TVM Python bindings and dlpack headers

- tvm_utility package

## How to setup

Setup the node using cuDNN and TensorRT support:

1. Download the pretrained file from [here](https://github.com/k0suke-murakami/kitti_pretrained_point_pillars).

```
$ git clone https://github.com/k0suke-murakami/kitti_pretrained_point_pillars.git
```

Setup the node using TVM support:

1. Clone the modelzoo repository for Autoware [here](https://github.com/autowarefoundation/modelzoo).

2. Use the TVM-CLI to export point pillars models to TVM (Instruction on how to do it are present in the repository ```modelzoo/scripts/tvm_cli/README.md```), models are ```perception/lidar_obstacle_detection/point_pillars_pfe/onnx_fp32_kitti``` and ```perception/lidar_obstacle_detection/point_pillars_rpn/onnx_fp32_kitti```

3. Copy the generated files into the ```tvm_models/tvm_point_pillars_pfe``` and ```tvm_models/tvm_point_pillars_rpn``` folders respectively for each model. With these files in place, the package will be built using TVM.

4. Compile the node.

## How to launch

* Launch file (cuDNN and TensorRT support):
`roslaunch lidar_point_pillars lidar_point_pillars.launch pfe_onnx_file:=/PATH/TO/FILE.onnx rpn_onnx_file:=/PATH/TO/FILE.onnx input_topic:=/points_raw`

* Launch file (TVM support):
`roslaunch lidar_point_pillars lidar_point_pillars.launch`

* You can launch it through the runtime manager in Computing tab, as well.

## API
```
/**
* @brief Call PointPillars for the inference.
* @param[in] in_points_array pointcloud array
* @param[in] in_num_points Number of points
* @param[out] out_detections Output bounding box from the network
* @details This is an interface for the algorithm.
*/
void doInference(float* in_points_array, int in_num_points, std::vector<float> out_detections);
```

## Parameters

|Parameter| Type| Description|Default|
----------|-----|--------|----|
|`input_topic`|*String*|Input topic Pointcloud. |`/points_raw`|
|`baselink_support`|*Bool*|Whether to use baselink to adjust parameters. |`True`|
|`reproduce_result_mode`|*Bool*|Whether to enable reproducible result mode at the cost of the runtime. |`False`|
|`score_threshold`|*Float*|Minimum score required to include the result [0,1]|0.5|
|`nms_overlap_threshold`|*Float*|Minimum IOU required to have when applying NMS [0,1]|0.5|
|`pfe_onnx_file`|*String* |Path to the PFE onnx file, unused if TVM build is chosen ||
|`rpn_onnx_file`|*String* |Path to the RPN onnx file, unused if TVM build is chosen||

## Outputs

|Topic|Type|Description|
|---|---|---|
|`/detection/lidar_detector/objects`|`autoware_msgs/DetectedObjetArray`|Array of Detected Objects in Autoware format|

## Notes

* To display the results in Rviz `objects_visualizer` is required.
(Launch file launches automatically this node).

* Pretrained models are available [here](https://github.com/k0suke-murakami/kitti_pretrained_point_pillars), trained with the help of the KITTI dataset. For this reason, these are not suitable for commercial purposes. Derivative works are bound to the BY-NC-SA 3.0 License. (https://creativecommons.org/licenses/by-nc-sa/3.0/)
