/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rawformatter.h"
#include "nnfw.h"
#include "nnfw_util.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace onert_run
{
void RawFormatter::loadInputs(const std::string &prefix, std::vector<Allocation> &inputs)
{
  uint32_t num_inputs = inputs.size();

  // Support multiple inputs
  // Option 1: Get comman-separated input file list like --load:raw a,b,c
  // Option 2: Get prefix --load:raw in
  //           Internally access in.0, in.1, in.2, ... in.{N-1} where N is determined by nnfw info
  //           query api.
  //
  // Currently Option 2 is implemented.
  try
  {
    for (uint32_t i = 0; i < num_inputs; ++i)
    {
      const auto bufsz = inputs[i].size();
      const auto filename = prefix + "." + std::to_string(i);
      auto filesz = std::filesystem::file_size(filename);
      if (bufsz != filesz)
      {
        throw std::runtime_error("Input " + std::to_string(i) +
                                 " size does not match: " + std::to_string(bufsz) +
                                 " expected, but " + std::to_string(filesz) + " provided.");
      }
      std::ifstream file(filename, std::ios::in | std::ios::binary);
      file.read(reinterpret_cast<char *>(inputs[i].data()), filesz);
      file.close();
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << std::endl;
    std::exit(-1);
  }
};

void RawFormatter::dumpOutputs(const std::string &prefix, const std::vector<Allocation> &outputs)
{
  uint32_t num_outputs = outputs.size();
  try
  {
    for (uint32_t i = 0; i < num_outputs; i++)
    {
      const auto bufsz = outputs[i].size();
      const auto filename = prefix + "." + std::to_string(i);

      std::ofstream file(filename, std::ios::out | std::ios::binary);
      file.write(reinterpret_cast<const char *>(outputs[i].data()), bufsz);
      file.close();
      std::cout << filename + " is generated.\n";
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << "Error during dumpOutputs on onert_run : " << e.what() << std::endl;
    std::exit(-1);
  }
}

void RawFormatter::dumpInputs(const std::string &prefix, const std::vector<Allocation> &inputs)
{
  uint32_t num_inputs = inputs.size();
  try
  {
    for (uint32_t i = 0; i < num_inputs; i++)
    {
      const auto bufsz = inputs[i].size();
      const auto filename = prefix + "." + std::to_string(i);

      std::ofstream file(filename, std::ios::out | std::ios::binary);
      file.write(reinterpret_cast<const char *>(inputs[i].data()), bufsz);
      file.close();
      std::cout << filename + " is generated.\n";
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << "Error during dumpRandomInputs on onert_run : " << e.what() << std::endl;
    std::exit(-1);
  }
}
} // end of namespace onert_run
