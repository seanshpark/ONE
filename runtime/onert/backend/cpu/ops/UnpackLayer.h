/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All Rights Reserved
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

#ifndef __ONERT_BACKEND_CPU_OPS_UNPACKLAYER_H__
#define __ONERT_BACKEND_CPU_OPS_UNPACKLAYER_H__

#include <backend/IPortableTensor.h>

#include <exec/IFunction.h>

namespace onert::backend::cpu::ops
{

class UnpackLayer : public ::onert::exec::IFunction
{
public:
  UnpackLayer();

public:
  void configure(const IPortableTensor *input, uint32_t axis, int32_t num_output,
                 std::vector<IPortableTensor *> &output);
  void run() override;

private:
  template <typename T> void unpackImpl();

private:
  const IPortableTensor *_input;
  std::vector<IPortableTensor *> _outputs;
  uint32_t _axis;
  int32_t _num_output;
};

} // namespace onert::backend::cpu::ops

#endif // __ONERT_BACKEND_CPU_OPS_UNPACKLAYER_H__
