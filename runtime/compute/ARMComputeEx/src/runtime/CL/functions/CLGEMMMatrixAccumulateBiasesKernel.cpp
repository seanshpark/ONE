/*
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All Rights Reserved
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
/*
 * Copyright (c) 2017-2020 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "arm_compute/core/CL/kernels/CLGEMMMatrixAccumulateBiasesKernel.h"

#include "arm_compute/core/CL/CLKernelLibrary.h"
#include "arm_compute/core/CL/CLKernelLibraryEx.h"
#include "arm_compute/core/CL/CLHelpers.h"
#include "arm_compute/core/CL/ICLTensor.h"
#include "arm_compute/core/CL/OpenCL.h"
#include "arm_compute/core/Error.h"
#include "arm_compute/core/Helpers.h"
#include "arm_compute/core/Types.h"
#include "arm_compute/core/Utils.h"
#include "support/StringSupport.h"
#include "src/core/CL/CLValidate.h"
#include "src/core/AccessWindowStatic.h"
#include "src/core/helpers/WindowHelpers.h"

using namespace arm_compute;

namespace
{
Status validate_arguments(const ITensorInfo *accum, const ITensorInfo *biases)
{
  ARM_COMPUTE_RETURN_ERROR_ON_F16_UNSUPPORTED(accum);
  ARM_COMPUTE_RETURN_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(accum, 1, DataType::F16, DataType::F32);
  ARM_COMPUTE_RETURN_ERROR_ON_MISMATCHING_DATA_TYPES(biases, accum);
  ARM_COMPUTE_RETURN_ERROR_ON(biases->num_dimensions() != 1);

  return Status{};
}

std::pair<Status, Window>
validate_and_configure_window(ITensorInfo *accum, ITensorInfo *biases, GPUTarget gpu_target,
                              unsigned int &num_elems_processed_per_iteration)
{
  // Select the vector size to use (8 for Bifrost; 16 for Midgard).
  bool is_gpu_bifrost =
    gpu_target_is_in(gpu_target, GPUTarget::G71, GPUTarget::G72, GPUTarget::G76, GPUTarget::G51,
                     GPUTarget::G51BIG, GPUTarget::G51LIT, GPUTarget::G52, GPUTarget::G52LIT);
  num_elems_processed_per_iteration = is_gpu_bifrost ? 8 : 16;

  // Configure kernel window
  Window win = calculate_max_window(*accum, Steps(num_elems_processed_per_iteration));

  AccessWindowStatic biases_access(
    biases, 0, 0, ceil_to_multiple(biases->dimension(0), num_elems_processed_per_iteration),
    biases->dimension(1));
  AccessWindowHorizontal accum_access(accum, 0, num_elems_processed_per_iteration);

  bool window_changed = update_window_and_padding(win, biases_access, accum_access);

  Status err = (window_changed)
                 ? ARM_COMPUTE_CREATE_ERROR(ErrorCode::RUNTIME_ERROR, "Insufficient Padding!")
                 : Status{};
  return std::make_pair(err, win);
}
} // namespace

CLGEMMMatrixAccumulateBiasesKernel::CLGEMMMatrixAccumulateBiasesKernel()
  : _accum(nullptr), _biases(nullptr)
{
}

void CLGEMMMatrixAccumulateBiasesKernel::configure(ICLTensor *accum, const ICLTensor *biases)
{
  configure(CLKernelLibrary::get().get_compile_context(), accum, biases);
}

void CLGEMMMatrixAccumulateBiasesKernel::configure(const CLCompileContext &compile_context,
                                                   ICLTensor *accum, const ICLTensor *biases)
{
  ARM_COMPUTE_UNUSED(compile_context);
  // Perform validate step
  ARM_COMPUTE_ERROR_ON_NULLPTR(accum, biases);
  ARM_COMPUTE_ERROR_THROW_ON(validate_arguments(accum->info(), biases->info()));

  _biases = biases;
  _accum = accum;

  // Get the target gpu
  GPUTarget gpu_target = get_target();
  unsigned int vector_size = 0;

  // Configure kernel window
  auto [error, window] =
    validate_and_configure_window(accum->info(), biases->info(), gpu_target, vector_size);
  ARM_COMPUTE_ERROR_THROW_ON(error);
  ICLKernel::configure_internal(window);

  // Add build options
  CLBuildOptions build_opts;
  build_opts.add_option("-DDATA_TYPE=" + get_cl_type_from_data_type(accum->info()->data_type()));
  build_opts.add_option("-DVECTOR_SIZE=" + support::cpp11::to_string(vector_size));

  // Create kernel
  _kernel = static_cast<cl::Kernel>(
    CLKernelLibraryEx::get().create_kernel("gemm_accumulate_biases", build_opts.options()));
}

Status CLGEMMMatrixAccumulateBiasesKernel::validate(const ITensorInfo *accum,
                                                    const ITensorInfo *biases, GPUTarget gpu_target)
{
  unsigned int num_elems_processed_per_iteration = 0;
  ARM_COMPUTE_RETURN_ON_ERROR(validate_arguments(accum, biases));
  ARM_COMPUTE_RETURN_ON_ERROR(validate_and_configure_window(accum->clone().get(),
                                                            biases->clone().get(), gpu_target,
                                                            num_elems_processed_per_iteration)
                                .first);

  return Status{};
}

void CLGEMMMatrixAccumulateBiasesKernel::run(const Window &window, cl::CommandQueue &queue)
{
  ARM_COMPUTE_ERROR_ON_UNCONFIGURED_KERNEL(this);
  ARM_COMPUTE_ERROR_ON_MISMATCHING_WINDOWS(ICLKernel::window(), window);

  Window accum_slice = window.first_slice_window_2D();

  Window biases_slice(accum_slice);
  biases_slice.set(Window::DimY, Window::Dimension(0, 1, 1));

  // Run kernel
  do
  {
    // Set arguments
    unsigned int idx = 0;
    add_2D_tensor_argument(idx, _accum, accum_slice);
    add_1D_tensor_argument(idx, _biases, biases_slice);

    enqueue(queue, *this, accum_slice, lws_hint());
  } while (window.slide_window_slice_2D(accum_slice));
}
