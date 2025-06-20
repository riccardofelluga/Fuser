// clang-format off
/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-present NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
// clang-format on
#include <csrc/exceptions.h>
#include <device_lower/lower2device.h>
#include <fusion.h>
#include <ir/all_nodes.h>
#include <ir/utils.h>
#include <ops/all_ops.h>
#include <runtime/executor.h>
#include <scheduler/all_schedulers.h>

#include <benchmark/benchmark.h>

#include <cuda_runtime.h>

#include <benchmarks/cpp/utils.h>
#include <tests/cpp/utils.h>

using namespace nvfuser;

//------------------------------------------------------------------------------

static void setupSoftmaxBWD(
    Fusion* fusion,
    DataType dtype,
    const int reduction_axis) {
  NVF_ERROR(dtype == DataType::Float || dtype == DataType::Half);

  FusionGuard fg(fusion);
  // setup fusion
  auto grad_output = makeContigTensor(2, dtype);
  auto output = makeContigTensor(2, dtype);
  auto input = makeContigTensor(2, dtype);
  fusion->addInput(grad_output);
  fusion->addInput(output);
  fusion->addInput(input);

  if (dtype == DataType::Half) {
    grad_output = castOp(DataType::Float, grad_output);
    output = castOp(DataType::Float, output);
    input = castOp(DataType::Float, input);
  }

  auto grad_input = softmax_backward(grad_output, output, reduction_axis);

  if (dtype == DataType::Half) {
    grad_input = castOp(DataType::Half, grad_input);
  }

  fusion->addOutput(grad_input);
}

static void NvFuserScheduler_Softmax_BWD(
    benchmark::State& benchmark_state,
    FusionExecutorCache* executor_cache,
    DataType dtype,
    const int reduction_axis) {
  NVF_ERROR(dtype == DataType::Float || dtype == DataType::Half);

  at::manual_seed(0);
  auto options =
      at::TensorOptions().dtype(data_type_to_aten(dtype)).device(at::kCUDA, 0);

  auto reduction_size = benchmark_state.range(0);
  auto iter_size = benchmark_state.range(1);

  at::Tensor input =
      (reduction_axis ? at::randn({iter_size, reduction_size}, options)
                      : at::randn({reduction_size, iter_size}, options));

  at::Tensor grad_output =
      (reduction_axis ? at::randn({iter_size, reduction_size}, options)
                      : at::randn({reduction_size, iter_size}, options));

  at::Tensor output =
      (reduction_axis ? at::randn({iter_size, reduction_size}, options)
                      : at::randn({reduction_size, iter_size}, options));

  KernelArgumentHolder args({grad_output, output, input});

  runBenchmarkIterations(benchmark_state, executor_cache, args);

  benchmark_state.SetBytesProcessed(
      int64_t(benchmark_state.iterations()) *
      (3 * input.numel() * dataTypeSizeByte(dtype)));
}

//------------------------------------------------------------------------------

static void Baseline_Softmax_BWD(
    benchmark::State& benchmark_state,
    DataType dtype,
    const int reduction_axis) {
  at::manual_seed(0);
  auto options =
      at::TensorOptions().dtype(data_type_to_aten(dtype)).device(at::kCUDA, 0);

  auto reduction_size = benchmark_state.range(0);
  auto iter_size = benchmark_state.range(1);

  at::Tensor input =
      (reduction_axis ? at::randn({iter_size, reduction_size}, options)
                      : at::randn({reduction_size, iter_size}, options));

  at::Tensor grad_output =
      (reduction_axis ? at::randn({iter_size, reduction_size}, options)
                      : at::randn({reduction_size, iter_size}, options));

  at::Tensor output =
      (reduction_axis ? at::randn({iter_size, reduction_size}, options)
                      : at::randn({reduction_size, iter_size}, options));

  for (auto _ : benchmark_state) {
    clearL2Cache();
    CudaKernelTimer timer;
    auto grad_input = at::_softmax_backward_data(
        grad_output, output, reduction_axis, data_type_to_aten(dtype));
    benchmark_state.SetIterationTime(timer.elapsed() / 1000.0);
  }
  // Sync everything up before we're finished, don't want to run ahead on the
  // cpu while benchmarking.
  C10_CUDA_CHECK(cudaDeviceSynchronize());

  benchmark_state.SetBytesProcessed(
      int64_t(benchmark_state.iterations()) *
      (3 * input.numel() * dataTypeSizeByte(dtype)));
}

static void Baseline_Softmax_BWD_Outer_fp32(benchmark::State& benchmark_state) {
  Baseline_Softmax_BWD(benchmark_state, DataType::Float, 0);
}

static void Baseline_Softmax_BWD_Inner_fp32(benchmark::State& benchmark_state) {
  Baseline_Softmax_BWD(benchmark_state, DataType::Float, 1);
}

static void Baseline_Softmax_BWD_Outer_fp16(benchmark::State& benchmark_state) {
  Baseline_Softmax_BWD(benchmark_state, DataType::Half, 0);
}

static void Baseline_Softmax_BWD_Inner_fp16(benchmark::State& benchmark_state) {
  Baseline_Softmax_BWD(benchmark_state, DataType::Half, 1);
}

//------------------------------------------------------------------------------

NVFUSER_BENCHMARK_DEFINE(
    NvFuserScheduler_Softmax_BWD_Outer_fp32,
    setupSoftmaxBWD,
    NvFuserScheduler_Softmax_BWD,
    DataType::Float,
    0);

NVFUSER_BENCHMARK_DEFINE(
    NvFuserScheduler_Softmax_BWD_Inner_fp32,
    setupSoftmaxBWD,
    NvFuserScheduler_Softmax_BWD,
    DataType::Float,
    1);

NVFUSER_BENCHMARK_DEFINE(
    NvFuserScheduler_Softmax_BWD_Outer_fp16,
    setupSoftmaxBWD,
    NvFuserScheduler_Softmax_BWD,
    DataType::Half,
    0);

NVFUSER_BENCHMARK_DEFINE(
    NvFuserScheduler_Softmax_BWD_Inner_fp16,
    setupSoftmaxBWD,
    NvFuserScheduler_Softmax_BWD,
    DataType::Half,
    1);

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

NVFUSER_BENCHMARK_RUN(NvFuserScheduler_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

//------------------------------------------------------------------------------

BENCHMARK(Baseline_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Outer_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp32)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{1, 1024 * 1024}, {160, 320}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{32768, 16 * 1024 * 1024}, {2, 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{2, 16}, {32768, 16 * 1024 * 1024}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();

BENCHMARK(Baseline_Softmax_BWD_Inner_fp16)
    // ->RangeMultiplier(2)
    ->Ranges({{128, 1024 * 16}, {128, 1024 * 16}})
    ->Unit(benchmark::kMicrosecond)
    ->UseManualTime();
