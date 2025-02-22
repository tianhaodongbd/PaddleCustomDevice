// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "kernels/funcs/npu_funcs.h"
#include "kernels/funcs/npu_op_runner.h"

namespace custom_kernel {

template <typename T, typename Context>
void AclopFloorDivideKernel(const Context& dev_ctx,
                            const phi::DenseTensor& x,
                            const phi::DenseTensor& y,
                            phi::DenseTensor* out) {
  dev_ctx.template Alloc<T>(out);
  auto stream = dev_ctx.stream();
  const auto& runner = NpuOpRunner("FloorDiv", {x, y}, {*out}, {});
  runner.Run(stream);
}

template <typename T, typename Context>
void FloorDivideKernel(const Context& dev_ctx,
                       const phi::DenseTensor& x,
                       const phi::DenseTensor& y,
                       phi::DenseTensor* out) {
  DO_COMPATIBILITY(
      aclnnFloorDivide,
      (custom_kernel::AclopFloorDivideKernel<T, Context>(dev_ctx, x, y, out)));
  dev_ctx.template Alloc<T>(out);
  EXEC_NPU_CMD(aclnnFloorDivide, dev_ctx, x, y, *out);
}

}  // namespace custom_kernel

PD_REGISTER_PLUGIN_KERNEL(floor_divide,
                          npu,
                          ALL_LAYOUT,
                          custom_kernel::FloorDivideKernel,
                          int,
                          int64_t,
                          float,
                          phi::dtype::float16,
                          double) {}
