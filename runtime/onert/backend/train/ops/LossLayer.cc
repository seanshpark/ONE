/*
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "LossLayer.h"

namespace onert::backend::train::ops
{

LossLayer::LossLayer()
  : _y_pred(nullptr), _y_true(nullptr), _output(nullptr), _back_prop_y_pred(nullptr),
    _reduction_type(ir::train::LossReductionType::Undefined)
{
  // DO NOTHING
}

void LossLayer::configure(const IPortableTensor *y_pred, const IPortableTensor *y_true,
                          IPortableTensor *output, IPortableTensor *back_prop_y_pred,
                          ir::train::LossReductionType reduction_type)
{
  assert(y_pred != nullptr);
  assert(y_true != nullptr);
  assert(output != nullptr);
  // back_prop_y_pred can be nullptr if backwarding is not required

  _y_pred = y_pred;
  _y_true = y_true;
  _output = output;
  _back_prop_y_pred = back_prop_y_pred;
  _reduction_type = reduction_type;
}

} // namespace onert::backend::train::ops
