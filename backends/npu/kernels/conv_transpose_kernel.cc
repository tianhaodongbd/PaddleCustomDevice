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

#include "kernels/funcs/conv_util.h"
#include "kernels/funcs/npu_funcs.h"
#include "kernels/funcs/npu_op_runner.h"

namespace custom_kernel {

template <typename T, typename Context>
void Conv2dTransposeKernel(const Context& dev_ctx,
                           const phi::DenseTensor& x,
                           const phi::DenseTensor& filter,
                           const std::vector<int>& strides,
                           const std::vector<int>& padding,
                           const std::vector<int>& out_padding,
                           const phi::IntArray& output_size,
                           const std::string& padding_algorithm,
                           int groups,
                           const std::vector<int>& dilation,
                           const std::string& data_format,
                           phi::DenseTensor* out) {
  auto paddings = padding;
  auto dilations = dilation;
  auto output_padding = out_padding;
  dev_ctx.template Alloc<T>(out);
  // check dimension
  const bool channel_last = data_format == "NHWC";

  // update paddings and dilations
  auto in_dims = x.dims();
  auto filter_dims = filter.dims();
  phi::DDim in_data_dims;
  phi::DDim filter_data_dims;

  if (channel_last) {
    in_data_dims = phi::slice_ddim(in_dims, 1, in_dims.size() - 1);
  } else {
    in_data_dims = phi::slice_ddim(in_dims, 2, in_dims.size());
  }
  filter_data_dims = phi::slice_ddim(filter_dims, 2, in_dims.size());

  std::vector<int> ksize = phi::vectorize<int>(filter_data_dims);
  custom_kernel::UpdatePaddingAndDilation(
      &paddings, &dilations, padding_algorithm, in_data_dims, strides, ksize);

  // construct NPU attr
  std::vector<int> strides_vec(4, 1);
  std::vector<int> dilations_vec(4, 1);

  phi::DenseTensor input_tensor(x), output_tensor(*out);

  if (channel_last) {
    phi::DenseTensorMeta in_meta = {
        x.dtype(), x.dims(), phi::DataLayout::kNHWC};
    input_tensor.set_meta(in_meta);
    phi::DenseTensorMeta out_meta = {
        out->dtype(), out->dims(), phi::DataLayout::kNHWC};
    output_tensor.set_meta(out_meta);
    strides_vec[1] = strides[0];
    strides_vec[2] = strides[1];
    dilations_vec[1] = dilations[0];
    dilations_vec[2] = dilations[1];
  } else {
    strides_vec[2] = strides[0];
    strides_vec[3] = strides[1];
    dilations_vec[2] = dilations[0];
    dilations_vec[3] = dilations[1];
  }

  for (auto i = output_padding.size(); i < 4; ++i) {
    output_padding.insert(output_padding.begin(), 0);
  }
  auto output_dim_vec = phi::vectorize(output_tensor.dims());

  auto stream = dev_ctx.stream();
  NpuOpRunner runner;
  runner.SetType("Conv2DTranspose")
      .AddInput(dev_ctx, std::move(output_dim_vec))
      .AddInput(input_tensor)
      .AddInput(filter)
      .AddOutput(output_tensor)
      .AddAttr("strides", strides_vec)
      .AddAttr("pads", paddings)
      .AddAttr("dilations", dilations_vec)
      .AddAttr("groups", groups)
      .AddAttr("data_format", data_format)
      .AddAttr("output_padding", output_padding);
  runner.Run(stream);
}

template <typename T, typename Context>
void Conv2dTransposeGradKernel(const Context& dev_ctx,
                               const phi::DenseTensor& x,
                               const phi::DenseTensor& filter,
                               const phi::DenseTensor& dout,
                               const std::vector<int>& strides,
                               const std::vector<int>& padding,
                               const std::vector<int>& output_padding,
                               const phi::IntArray& output_size,
                               const std::string& padding_algorithm,
                               int groups,
                               const std::vector<int>& dilation,
                               const std::string& data_format,
                               phi::DenseTensor* dx,
                               phi::DenseTensor* dfilter) {
  auto paddings = padding;
  auto dilations = dilation;
  if ((!dx) && (!dfilter)) return;

  const phi::DataLayout data_layout = common::StringToDataLayout(data_format);

  auto in_dims = x.dims();
  auto filter_dims = filter.dims();
  // auto out_grad_dims = output_grad->dims();
  // const int batch_size = static_cast<int>(input->dims()[0]);

  const bool channel_last = (data_layout == phi::DataLayout::kNHWC);

  phi::DDim in_data_dims;
  if (channel_last) {
    in_data_dims = phi::slice_ddim(in_dims, 1, in_dims.size() - 1);
  } else {
    in_data_dims = phi::slice_ddim(in_dims, 2, in_dims.size());
  }
  phi::DDim filter_data_dims =
      phi::slice_ddim(filter_dims, 2, filter_dims.size());
  std::vector<int> ksize = phi::vectorize<int>(filter_data_dims);
  custom_kernel::UpdatePaddingAndDilation(
      &paddings, &dilations, padding_algorithm, in_data_dims, strides, ksize);

  std::vector<int> strides_vec(4, 1);
  std::vector<int> dilations_vec(4, 1);

  phi::DenseTensor input_tensor(x), output_grad_tensor(dout);
  if (channel_last) {
    phi::DenseTensorMeta in_meta = {
        x.dtype(), x.dims(), phi::DataLayout::kNHWC};
    input_tensor.set_meta(in_meta);
    phi::DenseTensorMeta out_meta = {
        dout.dtype(), dout.dims(), phi::DataLayout::kNHWC};
    output_grad_tensor.set_meta(out_meta);
    strides_vec[1] = strides[0];
    strides_vec[2] = strides[1];
    dilations_vec[1] = dilations[0];
    dilations_vec[2] = dilations[1];
  } else {
    strides_vec[2] = strides[0];
    strides_vec[3] = strides[1];
    dilations_vec[2] = dilations[0];
    dilations_vec[3] = dilations[1];
  }

  auto stream = dev_ctx.stream();
  if (dfilter) {
    dev_ctx.template Alloc<T>(dfilter);
    NpuOpRunner runner_filter;
    runner_filter.SetType("Conv2DBackpropFilter")
        .AddInput(output_grad_tensor)
        .AddInput(dev_ctx, phi::vectorize<int>(filter_dims))
        .AddInput(input_tensor)
        .AddOutput(*dfilter)
        .AddAttrs({{"strides", strides_vec}})
        .AddAttrs({{"pads", paddings}})
        .AddAttrs({{"dilations", dilations_vec}})
        .AddAttrs({{"groups", groups}})
        .AddAttrs({{"data_format", data_format}})
        .Run(stream);
  }
  if (dx) {
    dev_ctx.template Alloc<T>(dx);
    phi::DenseTensor input_grad_tensor(*dx);
    if (channel_last) {
      phi::DenseTensorMeta meta = {
          dx->dtype(), dx->dims(), phi::DataLayout::kNHWC};
      input_grad_tensor.set_meta(meta);
    }
    const auto& runner = NpuOpRunner("Conv2D",
                                     {output_grad_tensor, filter},
                                     {input_grad_tensor},
                                     {{"strides", strides_vec},
                                      {"pads", paddings},
                                      {"dilations", dilations_vec},
                                      {"groups", groups},
                                      {"data_format", data_format}});
    runner.Run(stream);
  }
}

template <typename T, typename Context>
void DepthwiseConv2dTransposeKernel(const Context& dev_ctx,
                                    const phi::DenseTensor& x,
                                    const phi::DenseTensor& filter,
                                    const std::vector<int>& strides,
                                    const std::vector<int>& padding,
                                    const std::vector<int>& out_padding,
                                    const phi::IntArray& output_size,
                                    const std::string& padding_algorithm,
                                    int groups,
                                    const std::vector<int>& dilation,
                                    const std::string& data_format,
                                    phi::DenseTensor* out) {
  auto paddings = padding;
  auto dilations = dilation;
  auto output_padding = out_padding;
  dev_ctx.template Alloc<T>(out);
  // check dimension
  const bool channel_last = data_format == "NHWC";

  // update paddings and dilations
  auto in_dims = x.dims();
  auto filter_dims = filter.dims();
  phi::DDim in_data_dims;
  phi::DDim filter_data_dims;

  if (channel_last) {
    in_data_dims = phi::slice_ddim(in_dims, 1, in_dims.size() - 1);
  } else {
    in_data_dims = phi::slice_ddim(in_dims, 2, in_dims.size());
  }
  filter_data_dims = phi::slice_ddim(filter_dims, 2, in_dims.size());

  std::vector<int> ksize = phi::vectorize<int>(filter_data_dims);
  custom_kernel::UpdatePaddingAndDilation(
      &paddings, &dilations, padding_algorithm, in_data_dims, strides, ksize);

  // construct NPU attr
  std::vector<int> strides_vec(4, 1);
  std::vector<int> dilations_vec(4, 1);

  phi::DenseTensor input_tensor(x), output_tensor(*out);

  if (channel_last) {
    phi::DenseTensorMeta in_meta = {
        x.dtype(), x.dims(), phi::DataLayout::kNHWC};
    input_tensor.set_meta(in_meta);
    phi::DenseTensorMeta out_meta = {
        out->dtype(), out->dims(), phi::DataLayout::kNHWC};
    output_tensor.set_meta(out_meta);
    strides_vec[1] = strides[0];
    strides_vec[2] = strides[1];
    dilations_vec[1] = dilations[0];
    dilations_vec[2] = dilations[1];
  } else {
    strides_vec[2] = strides[0];
    strides_vec[3] = strides[1];
    dilations_vec[2] = dilations[0];
    dilations_vec[3] = dilations[1];
  }

  for (auto i = output_padding.size(); i < 4; ++i) {
    output_padding.insert(output_padding.begin(), 0);
  }
  auto output_dim_vec = phi::vectorize(output_tensor.dims());

  auto stream = dev_ctx.stream();
  NpuOpRunner runner;
  runner.SetType("Conv2DTranspose")
      .AddInput(dev_ctx, std::move(output_dim_vec))
      .AddInput(input_tensor)
      .AddInput(filter)
      .AddOutput(output_tensor)
      .AddAttr("strides", strides_vec)
      .AddAttr("pads", paddings)
      .AddAttr("dilations", dilations_vec)
      .AddAttr("groups", groups)
      .AddAttr("data_format", data_format)
      .AddAttr("output_padding", output_padding);
  runner.Run(stream);
}

template <typename T, typename Context>
void DepthwiseConv2dTransposeGradKernel(const Context& dev_ctx,
                                        const phi::DenseTensor& x,
                                        const phi::DenseTensor& filter,
                                        const phi::DenseTensor& dout,
                                        const std::vector<int>& strides,
                                        const std::vector<int>& padding,
                                        const std::vector<int>& output_padding,
                                        const phi::IntArray& output_size,
                                        const std::string& padding_algorithm,
                                        int groups,
                                        const std::vector<int>& dilation,
                                        const std::string& data_format,
                                        phi::DenseTensor* dx,
                                        phi::DenseTensor* dfilter) {
  auto paddings = padding;
  auto dilations = dilation;
  if ((!dx) && (!dfilter)) return;

  const phi::DataLayout data_layout = common::StringToDataLayout(data_format);

  auto in_dims = x.dims();
  auto filter_dims = filter.dims();
  // auto out_grad_dims = output_grad->dims();
  // const int batch_size = static_cast<int>(input->dims()[0]);

  const bool channel_last = (data_layout == phi::DataLayout::kNHWC);

  phi::DDim in_data_dims;
  if (channel_last) {
    in_data_dims = phi::slice_ddim(in_dims, 1, in_dims.size() - 1);
  } else {
    in_data_dims = phi::slice_ddim(in_dims, 2, in_dims.size());
  }
  phi::DDim filter_data_dims =
      phi::slice_ddim(filter_dims, 2, filter_dims.size());
  std::vector<int> ksize = phi::vectorize<int>(filter_data_dims);
  custom_kernel::UpdatePaddingAndDilation(
      &paddings, &dilations, padding_algorithm, in_data_dims, strides, ksize);

  std::vector<int> strides_vec(4, 1);
  std::vector<int> dilations_vec(4, 1);

  phi::DenseTensor input_tensor(x), output_grad_tensor(dout);
  if (channel_last) {
    phi::DenseTensorMeta in_meta = {
        x.dtype(), x.dims(), phi::DataLayout::kNHWC};
    input_tensor.set_meta(in_meta);
    phi::DenseTensorMeta out_meta = {
        dout.dtype(), dout.dims(), phi::DataLayout::kNHWC};
    output_grad_tensor.set_meta(out_meta);
    strides_vec[1] = strides[0];
    strides_vec[2] = strides[1];
    dilations_vec[1] = dilations[0];
    dilations_vec[2] = dilations[1];
  } else {
    strides_vec[2] = strides[0];
    strides_vec[3] = strides[1];
    dilations_vec[2] = dilations[0];
    dilations_vec[3] = dilations[1];
  }

  auto stream = dev_ctx.stream();
  if (dfilter) {
    dev_ctx.template Alloc<T>(dfilter);
    NpuOpRunner runner_filter;
    runner_filter.SetType("Conv2DBackpropFilter")
        .AddInput(output_grad_tensor)
        .AddInput(dev_ctx, phi::vectorize<int>(filter_dims))
        .AddInput(input_tensor)
        .AddOutput(*dfilter)
        .AddAttrs({{"strides", strides_vec}})
        .AddAttrs({{"pads", paddings}})
        .AddAttrs({{"dilations", dilations_vec}})
        .AddAttrs({{"groups", groups}})
        .AddAttrs({{"data_format", data_format}})
        .Run(stream);
  }
  if (dx) {
    dev_ctx.template Alloc<T>(dx);
    phi::DenseTensor input_grad_tensor(*dx);
    if (channel_last) {
      phi::DenseTensorMeta meta = {
          dx->dtype(), dx->dims(), phi::DataLayout::kNHWC};
      input_grad_tensor.set_meta(meta);
    }
    const auto& runner = NpuOpRunner("Conv2D",
                                     {output_grad_tensor, filter},
                                     {input_grad_tensor},
                                     {{"strides", strides_vec},
                                      {"pads", paddings},
                                      {"dilations", dilations_vec},
                                      {"groups", groups},
                                      {"data_format", data_format}});
    runner.Run(stream);
  }
}

}  // namespace custom_kernel

PD_REGISTER_PLUGIN_KERNEL(conv2d_transpose,
                          npu,
                          ALL_LAYOUT,
                          custom_kernel::Conv2dTransposeKernel,
                          float,
                          phi::dtype::float16) {}

PD_REGISTER_PLUGIN_KERNEL(conv2d_transpose_grad,
                          npu,
                          ALL_LAYOUT,
                          custom_kernel::Conv2dTransposeGradKernel,
                          float,
                          phi::dtype::float16) {}

PD_REGISTER_PLUGIN_KERNEL(depthwise_conv2d_transpose,
                          npu,
                          ALL_LAYOUT,
                          custom_kernel::DepthwiseConv2dTransposeKernel,
                          float,
                          phi::dtype::float16) {}

PD_REGISTER_PLUGIN_KERNEL(depthwise_conv2d_transpose_grad,
                          npu,
                          ALL_LAYOUT,
                          custom_kernel::DepthwiseConv2dTransposeGradKernel,
                          float,
                          phi::dtype::float16) {}
