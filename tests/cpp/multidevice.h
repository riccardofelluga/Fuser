// clang-format off
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-present NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on
#pragma once

#include <multidevice/communication.h>
#include <multidevice/communicator.h>
#include <multidevice/executor.h>
#include <multidevice/utils.h>
#include <tests/cpp/utils.h>

namespace nvfuser {

class MultiDeviceTest : public NVFuserTest {
 protected:
  MultiDeviceTest();
  ~MultiDeviceTest();
  void SetUp() override;

  // Given an aten tensor, TensorView the tensor is bound to, and deviceId
  // returns a shard of the tensor according the sharding annotation in tv
  // for the deviceId. If tensor is not sharded returns the original tensor.
  // TODO: If deviceId is not part of the mesh this should return an empty
  // tensor. Currently, we don't support this, so for now it returns a slice.
  static at::Tensor shardTensor(
      at::Tensor tensor,
      TensorView* tv,
      DeviceIdxType deviceId);

  static Communicator* getOrCreateCommunicator();

  Communicator* communicator;
  c10::TensorOptions tensor_options;
  bool debug_print;
  bool disable_skip;
};

class PipelineTest : public MultiDeviceTest {
 protected:
  PipelineTest();

  // Utility function used for validation in the tests. It compares the
  // (sharded) outputs with ref_unsharded_outputs. if
  // validate_with_prescribed_values is true, ref_unsharded_outputs is assumed
  // to be set manually in the test body. Otherwise, ref_unsharded_outputs is
  // computed by running a Fusion on a single device with the unsharded_inputs
  void validate(bool validate_with_prescribed_values = false);
  void executeAndValidate(bool validate_with_prescribed_values = false);

  std::unique_ptr<MultiDeviceExecutor> runtime;
  std::unique_ptr<Fusion> fusion;
  std::vector<c10::IValue> inputs;
  std::vector<c10::IValue> unsharded_inputs;
  std::vector<at::Tensor> outputs;
  std::vector<at::Tensor> ref_unsharded_outputs;
  MultiDeviceExecutorParams multi_device_executor_params;
  LaunchParams l_params = {};
};

} // namespace nvfuser
