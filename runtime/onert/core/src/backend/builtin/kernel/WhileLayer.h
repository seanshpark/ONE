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

#ifndef __ONERT_BACKEND_BUILTIN_KERNEL_WHILE_LAYER_H__
#define __ONERT_BACKEND_BUILTIN_KERNEL_WHILE_LAYER_H__

#include <backend/IPortableTensor.h>
#include <exec/IExecutors.h>
#include <exec/IFunction.h>
#include <ir/OperandIndexSequence.h>
#include <ir/Graph.h>
#include "../ExternalContext.h"

#include "backend/basic/MemoryManager.h"

namespace onert::backend::builtin::kernel
{

class WhileLayer : public ::onert::exec::IFunction
{
public:
  WhileLayer(const std::vector<backend::IPortableTensor *> input_tensors,
             const std::vector<backend::IPortableTensor *> output_tensors,
             const ir::SubgraphIndex &cond_subg_index, const ir::SubgraphIndex &body_subg_index,
             exec::IExecutors *executors, const ir::ModelIndex &model_index,
             basic::DynamicMemoryManager *dyn_memory_manager,
             const std::shared_ptr<ExternalContext> &external_context);

public:
  void run() override;

private:
  const ir::SubgraphIndex _cond_subg_index;
  const ir::SubgraphIndex _body_subg_index;
  const std::vector<backend::IPortableTensor *> _input_tensors;
  const std::vector<backend::IPortableTensor *> _output_tensors;
  exec::IExecutors *_executors;
  const ir::ModelIndex _model_index;
  basic::DynamicMemoryManager *_dyn_memory_manager; // For generating temp tensors
  const std::shared_ptr<ExternalContext> _external_context;
};

} // namespace onert::backend::builtin::kernel

#endif // __ONERT_BACKEND_BUILTIN_KERNEL_WHILE_LAYER_H__
