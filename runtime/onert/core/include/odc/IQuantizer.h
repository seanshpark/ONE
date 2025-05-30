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

#ifndef __ONERT_ODC_IQUANTIZER_H__
#define __ONERT_ODC_IQUANTIZER_H__

#include "odc/QuantizeType.h"
#include <cstdint>

namespace onert::odc
{

class IQuantizer
{
public:
  virtual ~IQuantizer() = default;

  virtual int quantize(const char *in, const char *out, QuantizeType qtype) = 0;

  /**
   * @brief Set the number of minmax records enough for quantization
   */
  virtual void setMinMaxRecordsThreshold(uint32_t value) = 0;

  /**
   * @brief Checking the number of minmax records enough for quantization
   *
   * @return True if ready, False otherwise
   */
  virtual bool readyForQuantize() = 0;

  /**
   * @brief Delete minmax file
   *
   * @return True if there were no errors, False otherwise
   */
  virtual bool deleteMinMaxFile() = 0;
};

} // namespace onert::odc

#endif // __ONERT_ODC_IQUANTIZER_H__
