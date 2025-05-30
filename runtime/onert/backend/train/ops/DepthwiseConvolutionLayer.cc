/*
 * Copyright (c) 2024 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "DepthwiseConvolutionLayer.h"

#include "OperationUtils.h"

#include <cker/eigen/EigenSupport.h>
#include <cker/train/operation/DepthwiseConv.h>
#include <cker/train/operation/ReLU.h>

namespace onert::backend::train::ops
{

DepthwiseConvolutionLayer::DepthwiseConvolutionLayer()
  : cpu::ops::DepthwiseConvolutionLayer(), _grad_weights{nullptr}, _grad_bias{nullptr},
    _back_prop_input{nullptr}, _back_prop_output{nullptr}, _act_back_prop_output{nullptr},
    _use_padded_filter{false}, _padded_filter{nullptr}, _filter_buffers{nullptr},
    _filter_dim_buffers{nullptr}
{
  // DO NOTHING
}

void DepthwiseConvolutionLayer::configureBackward(IPortableTensor *back_prop_input,
                                                  IPortableTensor *grad_weights,
                                                  IPortableTensor *grad_bias,
                                                  const IPortableTensor *back_prop_output,
                                                  const ir::Activation activation)
{
  _back_prop_input = back_prop_input;
  _back_prop_output = back_prop_output;
  _grad_weights = grad_weights;
  _grad_bias = grad_bias;

  if (_dilationWidth != 1 || _dilationHeight != 1)
    throw std::runtime_error("train DepthwiseConvolutionLayer: Unsupported dilation yet");

  if (activation != ir::Activation::NONE)
  {
    _act_back_prop_output = std::make_unique<BackPropTensor>(_back_prop_output->get_info());
    _act_back_prop_output->setBuffer(
      std::make_shared<basic::Allocator>(_act_back_prop_output->total_size()));
  }

  const int64_t k_packet_size = [&]() {
    const auto data_type = _back_prop_output->data_type();
    switch (data_type)
    {
      case OperandType::FLOAT32:
      {
        return nnfw::cker::eigen_support::kPacketSize<float>();
      }
      default:
        throw std::runtime_error("train DepthwiseConvolutionLayer: unsupported data type");
    }
  }();

  const auto incoming_shape = getShape(_back_prop_output);
  const int out_depth = incoming_shape.Dims(3);

  const int padded_filter_inner_dim_size =
    ((out_depth + k_packet_size - 1) / k_packet_size) * k_packet_size;

  // prepare out_bprop and in_bprop buffer for cker
  // NOTE The Eigen library uses both main thread as well as a thread pool.
  // Therefore, it needs to add an additional memory buffer for main thread.
  const int thread_count = nnfw::cker::eigen_support::getThreadCount() + 1;

  auto filter_dim_buffers_info = ir::OperandInfo(_back_prop_input->get_info());
  filter_dim_buffers_info.shape({thread_count, padded_filter_inner_dim_size});
  _filter_dim_buffers = std::make_unique<Tensor>(filter_dim_buffers_info);
  _filter_dim_buffers->setBuffer(
    std::make_shared<basic::Allocator>(_filter_dim_buffers->total_size()));

  _use_padded_filter = (out_depth % k_packet_size) == 0 ? false : true;

  const auto filter_shape = getShape(_kernel);
  const int batch = incoming_shape.Dims(0);

  const int filter_rows = filter_shape.Dims(1);
  const int filter_cols = filter_shape.Dims(2);
  const int filter_spatial_size = filter_rows * filter_cols;

  // prepare padded_filter buffer for cker
  auto padded_filter_info = ir::OperandInfo(_kernel->get_info());
  padded_filter_info.shape({batch, filter_spatial_size, padded_filter_inner_dim_size});
  _padded_filter = std::make_unique<Tensor>(padded_filter_info);
  _padded_filter->setBuffer(std::make_shared<basic::Allocator>(_padded_filter->total_size()));

  auto filter_buffers_info = ir::OperandInfo(_kernel->get_info());
  filter_buffers_info.shape({thread_count, filter_spatial_size, padded_filter_inner_dim_size});
  _filter_buffers = std::make_unique<Tensor>(filter_buffers_info);
  _filter_buffers->setBuffer(std::make_shared<basic::Allocator>(_filter_buffers->total_size()));
}

void DepthwiseConvolutionLayer::forward(bool) { cpu::ops::DepthwiseConvolutionLayer::run(); }

void DepthwiseConvolutionLayer::backward()
{
  const auto data_type = _back_prop_output->data_type();
  assert(data_type == _input->data_type());
  switch (data_type)
  {
    case OperandType::FLOAT32:
    {
      assert(data_type == _grad_bias->data_type());
      backwardFloat32();
      break;
    }
    default:
      throw std::runtime_error{"train DepthwiseConvolutionLayer: unsupported data type"};
  }
}

void DepthwiseConvolutionLayer::backwardFloat32()
{
  // Calculate gradient for activation
  const IPortableTensor *backprop_act;
  try
  {
    backprop_act =
      backpropActivation(_activation, _output, _back_prop_output, _act_back_prop_output.get());
  }
  catch (const std::exception &e)
  {
    throw std::runtime_error{"train DepthwiseConvolutionLayer: " + std::string(e.what())};
  }
  assert(backprop_act != nullptr);

  nnfw::cker::DepthwiseConvParams dconv_params;
  dconv_params.stride_width = _strideWidth;
  dconv_params.stride_height = _strideHeight;
  dconv_params.padding_values.width = _paddingLeft;
  dconv_params.padding_values.height = _paddingTop;
  dconv_params.depth_multiplier = _multiplier;
  dconv_params.dilation_height_factor = _dilationHeight;
  dconv_params.dilation_width_factor = _dilationWidth;

  // Calculate gradient for input
  nnfw::cker::train::backpropInput(
    dconv_params, getShape(backprop_act), getBuffer<float>(backprop_act), getShape(_kernel),
    getBuffer<float>(_kernel), getBuffer<float>(_padded_filter.get()), getShape(_back_prop_input),
    getBuffer<float>(_back_prop_input), _use_padded_filter, getBuffer<float>(_filter_buffers.get()),
    getBuffer<float>(_filter_dim_buffers.get()));

  // Calculate gradient for weights
  nnfw::cker::train::backpropFilter(
    dconv_params, getShape(backprop_act), getBuffer<float>(backprop_act), getShape(_input),
    getBuffer<float>(_input), getShape(_grad_weights), getBuffer<float>(_grad_weights),
    getBuffer<float>(_padded_filter.get()), getBuffer<float>(_filter_buffers.get()));

  // Calculate gradient for bias
  if (_bias)
  {
    assert(_grad_bias);
    biasGrad(backprop_act, _grad_bias);
  }
}

} // namespace onert::backend::train::ops
