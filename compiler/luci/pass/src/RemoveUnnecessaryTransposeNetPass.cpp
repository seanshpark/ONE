/*
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All Rights Reserved
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

#include "luci/Pass/RemoveUnnecessaryTransposeNetPass.h"

#include <luci/IR/CircleNodes.h>
#include <luci/Profile/CircleNodeOrigin.h>

#include <limits>
#include <vector>

namespace
{

#define CHECK_OR_FALSE(condition) \
  if (not(condition))             \
    return false;

bool extract_shape(const luci::CircleNode *node, std::vector<int32_t> &shape)
{
  uint32_t max_i32 = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());

  auto rank = node->rank();
  for (auto i = 0u; i < rank; ++i)
  {
    uint32_t v = node->dim(i).value();
    CHECK_OR_FALSE(v <= max_i32)
    shape.push_back(static_cast<int32_t>(v));
  }
  return true;
};

bool extract_const(const luci::CircleConst *const_node, std::vector<int32_t> &values)
{
  auto dtype = const_node->dtype();

  if (dtype == loco::DataType::S32)
  {
    auto size = const_node->size<loco::DataType::S32>();
    for (auto i = 0u; i < size; ++i)
    {
      int32_t v = const_node->at<loco::DataType::S32>(i);
      values.push_back(v);
    }
  }
  else if (dtype == loco::DataType::S64)
  {
    int64_t max_i32 = static_cast<int64_t>(std::numeric_limits<int32_t>::max());
    int64_t min_i32 = static_cast<int64_t>(std::numeric_limits<int32_t>::lowest());

    auto size = const_node->size<loco::DataType::S64>();
    for (auto i = 0u; i < size; ++i)
    {
      int64_t v = const_node->at<loco::DataType::S64>(i);
      CHECK_OR_FALSE(min_i32 <= v && v <= max_i32);
      values.push_back(static_cast<int32_t>(v));
    }
  }
  else
    return false;

  return true;
};

struct TagDim final
{
  TagDim(int32_t v) : value(v) {}

  int32_t value;
  std::vector<uint8_t> tags;
};

using TagShape = std::vector<TagDim>;

class TaggedShapeAnalyzer final
{
public:
  bool init(const luci::CircleTranspose *, const luci::CircleReshape *,
            const luci::CircleTranspose *);
  bool can_remove_transposes();

private:
  void init_shape_with_tag(const std::vector<int32_t> &);
  void analyze_transpose(const std::vector<int32_t> &);
  bool analyze_reshape(const std::vector<int32_t> &);
  bool verify_tag() const;

private:
  const luci::CircleNode *_in = nullptr;
  const luci::CircleTranspose *_front_transpose = nullptr;
  const luci::CircleReshape *_mid_reshape = nullptr;
  const luci::CircleTranspose *_back_transpose = nullptr;

  std::vector<int32_t> _in_shape_v;
  std::vector<int32_t> _front_perm_v;
  std::vector<int32_t> _mid_shape_v;
  std::vector<int32_t> _back_perm_v;

  const uint8_t START_TAG = 0;
  TagShape _shape;
};

/**
 * @brief initalize _shape with input tensor named 'in_shape'
 *
 * @note 'tags' are attached to non-1 valued dimension.
 */
void TaggedShapeAnalyzer::init_shape_with_tag(const std::vector<int32_t> &in_shape)
{
  _shape.clear();
  uint8_t tag = START_TAG;

  for (auto i : in_shape)
  {
    TagDim dim(i);

    if (dim.value != 1)
      dim.tags.push_back(tag++);

    _shape.push_back(dim);
  }
}

/**
 * @brief update _shape based on 'perm'
 *
 * @example Let's assume perm={0, 3, 1, 2} is given to [before] _shape.
 *
 *  This function reordered the Dims' order based on permutaiton value.
 *
 *  [before]  _shape :
 *              - value :   (1)   (7)   (7)   (448)
 *              - tags  :   (-)   (0)   (1)   (2)
 *
 *  [after]  _shape :
 *              - value :   (1)   (448)  (7)  (7)
 *              - tags  :   (-)   (2)    (0)  (1)
 */
void TaggedShapeAnalyzer::analyze_transpose(const std::vector<int32_t> &perm)
{
  assert(_shape.size() == perm.size());

  TagShape new_shape;
  for (auto i : perm)
  {
    new_shape.push_back(_shape.at(i));
  }
  _shape = new_shape;
}

/**
 * @brief update _shape based on 'new_shape'
 *
 * @return False, if it fails to update _shape
 *
 * @example Let's assume new_shape={1, 448, 49} is given to [before] _shape.
 *
 *  [before]  _shape :
 *              - value :   (1)   (448)  (7)   (7)
 *              - tags  :   (-)   (2)    (0)   (1)
 *
 *  [after]  _shape :
 *              - value :   (1)   (448)  (49)
 *              - tags  :   (-)   (2)    (0, 1)
 */
bool TaggedShapeAnalyzer::analyze_reshape(const std::vector<int32_t> &new_shape)
{
  if (new_shape.size() <= 0)
    return false;

  // indexing for _shape [old_shape_start_idx, old_shape_end_idx)
  uint32_t old_shape_start_idx = 0;
  uint32_t old_shape_end_idx = 1;
  auto old_shape_product = _shape[old_shape_start_idx].value;

  auto expand_range = [&]() -> bool {
    if (old_shape_end_idx >= _shape.size())
      return false;

    old_shape_product *= _shape[old_shape_end_idx].value;
    old_shape_end_idx++;
    return true;
  };

  auto move_to_next_range = [&]() -> bool {
    if (old_shape_end_idx >= _shape.size())
      return false;

    old_shape_start_idx = old_shape_end_idx;
    old_shape_end_idx++;
    old_shape_product = _shape[old_shape_start_idx].value;
    return true;
  };

  // Create 'new_tagged_shape' based on 'new_shape'
  TagShape new_tagged_shape;

  uint32_t new_shape_idx = 0;
  while (new_shape_idx < new_shape.size())
  {
    auto target_dim = new_shape[new_shape_idx];

    // Ignore dim == 1
    if (target_dim == 1)
    {
      new_tagged_shape.emplace_back(1);
      new_shape_idx++;
      continue;
    }

    while (old_shape_product < target_dim)
    {
      if (expand_range() == false)
        break;
    }

    if (old_shape_product != target_dim)
      return false;

    assert(old_shape_product == target_dim);

    TagDim dim(target_dim);
    for (uint32_t idx = old_shape_start_idx; idx < old_shape_end_idx; idx++)
    {
      const auto &old_tags = _shape[idx].tags;
      dim.tags.insert(dim.tags.end(), old_tags.begin(), old_tags.end());
    }
    new_tagged_shape.push_back(dim);

    new_shape_idx++;
    move_to_next_range();
  }
  _shape = new_tagged_shape;
  return true;
}

bool TaggedShapeAnalyzer::verify_tag() const
{
  // check whether tags in _shape are incremental
  uint8_t tag = START_TAG;
  for (const auto &dim : _shape)
  {
    for (const auto &t : dim.tags)
    {
      if (t == tag)
        tag++;
      else
        return false;
    }
  }
  return true;
}

/**
 * @brief Initialize the class members and check under conditions
 *
 *  Condtiions that have to be met for analyzer
 *    c1: input rank >= output rank
 *    c2: The 'perm' of tranpose should be a CircleConst* type
 *    c3: The input shape and the reshape node's shape should be known
 *
 * @return True, if all conditions are satisfied and class members are initialized successfully
 *         False, otherwise
 */
bool TaggedShapeAnalyzer::init(const luci::CircleTranspose *front_transpose,
                               const luci::CircleReshape *mid_reshape,
                               const luci::CircleTranspose *back_transpose)
{
  _in = loco::must_cast<luci::CircleNode *>(front_transpose->a());
  _front_transpose = front_transpose;
  _mid_reshape = mid_reshape;
  _back_transpose = back_transpose;

  // check c1
  CHECK_OR_FALSE(_in->rank() >= _back_transpose->rank());

  const auto front_perm = dynamic_cast<luci::CircleConst *>(_front_transpose->perm());
  const auto back_perm = dynamic_cast<luci::CircleConst *>(_back_transpose->perm());

  // check c2
  CHECK_OR_FALSE(front_perm != nullptr);
  CHECK_OR_FALSE(back_perm != nullptr);

  CHECK_OR_FALSE(extract_shape(_in, _in_shape_v));
  CHECK_OR_FALSE(extract_const(front_perm, _front_perm_v));
  CHECK_OR_FALSE(extract_shape(_mid_reshape, _mid_shape_v));
  CHECK_OR_FALSE(extract_const(back_perm, _back_perm_v));

  auto all_known = [](const std::vector<int32_t> &v) -> bool {
    for (auto i : v)
      if (i <= 0)
        return false;
    return true;
  };

  // check c3
  CHECK_OR_FALSE(all_known(_in_shape_v));
  CHECK_OR_FALSE(all_known(_mid_shape_v));

  return true;
}

/**
 * @brief check 'Transpose-Reshape-Transpose' can be replaced by one 'Reshape'.
 *
 * @warning '@init' have to be called first
 *
 * @example
 *  Let's explain how analyzer check Transpose-Reshape-Transpose pattern with an exact example.
 *
 *  Let's assume under pattern is given :
 *
 *      Input(1, 7, 7, 448)
 *            |
 *      Transpose(perm=(0, 3, 1, 2))
 *            |
 *      Resahape(shape=(1, 448, 49))
 *            |
 *      Transpose(perm=(0, 2, 1))
 *            |
 *      Output(1, 49, 448)
 *
 *  It simulates how each dimension of the tensor's shape are transformed/moved
 *  using a member variable named '_shape'.
 *  'tags' in _shape record the initial order of each dimension.
 *
 *   TIMELINE              |   _shape states :
 *                         |
 *   init_shape_with_tag   |    - value :   (1)   (7)   (7)   (448)
 *                         |    - tags  :   (-)   (0)   (1)   (2)
 *                         |
 *   analyze_transpose     |    - value :   (1)   (448)  (7)  (7)
 *                         |    - tags  :   (-)   (2)    (0)  (1)
 *                         |
 *   analyze_reshape       |    - value :   (1)   (448)   (49)
 *                         |    - tags  :   (-)   (2)     (0, 1)
 *                         |
 *   anaylze_transpose     |    - value  :   (1)   (49)    (448)
 *                         |    - tags   :   (-)   (0, 1)  (2)
 *
 *  After all simulation done, if tags are in same order as initial _shape,
 *  Transpose has no effect in final shape, which they can be removed as
 *  unnecessary Ops.
 */
bool TaggedShapeAnalyzer::can_remove_transposes()
{
  assert(_in != nullptr && _front_transpose != nullptr && _mid_reshape != nullptr &&
         _back_transpose != nullptr);

  init_shape_with_tag(_in_shape_v);

  analyze_transpose(_front_perm_v);

  if (not analyze_reshape(_mid_shape_v))
    return false;

  analyze_transpose(_back_perm_v);

  if (not verify_tag())
    return false;

  return true;
}

/**
 * @brief create CircleReshape node that reshapes 'front_transpose input tensor shape' into
 * 'back_transposes output tensor shape'
 */
template <loco::DataType ShapeType>
luci::CircleReshape *create_reshape_node(loco::Graph *graph,
                                         const luci::CircleTranspose *front_transpose,
                                         const luci::CircleReshape *mid_reshape,
                                         const luci::CircleTranspose *back_transpose)
{
  std::string composed_name =
    front_transpose->name() + ";" + mid_reshape->name() + ";" + back_transpose->name();

  std::vector<std::shared_ptr<luci::CircleNodeOrigin>> src_origin{luci::get_origin(front_transpose),
                                                                  luci::get_origin(mid_reshape),
                                                                  luci::get_origin(back_transpose)};
  auto const composed_origin = luci::composite_origin(src_origin);

  auto shape_node = graph->nodes()->create<luci::CircleConst>();
  {
    shape_node->dtype(ShapeType);
    shape_node->rank(1);
    shape_node->dim(0).set(back_transpose->rank());

    shape_node->size<ShapeType>(back_transpose->rank());
    for (uint32_t i = 0; i < back_transpose->rank(); i++)
    {
      shape_node->at<ShapeType>(i) = back_transpose->dim(i).value();
    }
    shape_node->shape_status(luci::ShapeStatus::VALID);
    shape_node->name(composed_name + "/shape");
    luci::add_origin(shape_node, composed_origin);
  }

  auto reshape_node = graph->nodes()->create<luci::CircleReshape>();
  {
    reshape_node->name(composed_name);
    reshape_node->tensor(front_transpose->a());
    reshape_node->shape(shape_node);
    luci::add_origin(reshape_node, composed_origin);
  }
  return reshape_node;
}

bool remove_unnecessary_transpose(luci::CircleTranspose *node)
{
  // find 'front_transpose - mid_reshape - back_transpose' pattern
  const auto back_transpose = node;
  const auto mid_reshape = dynamic_cast<luci::CircleReshape *>(back_transpose->a());
  {
    if (mid_reshape == nullptr)
      return false;
  }
  const auto front_transpose = dynamic_cast<luci::CircleTranspose *>(mid_reshape->tensor());
  {
    if (not front_transpose)
      return false;
  }

  TaggedShapeAnalyzer analyzer;

  if (not analyzer.init(front_transpose, mid_reshape, back_transpose))
    return false;

  if (not analyzer.can_remove_transposes())
    return false;

  // repalce with new_node
  luci::CircleReshape *new_node = create_reshape_node<loco::DataType::S32>(
    node->graph(), front_transpose, mid_reshape, back_transpose);

  replace(node).with(new_node);

  return true;
}

} // namespace

namespace luci
{

/**
 * BEFORE
 *
 * Current pass only targets below cases:
 *  - in.rank() >= out.rank()
 *  - 'Reshape' used to reduce N dimension into one (e.g. A x B x C => A x BC)
 *
 *
 *     [CircleNode]      [CircleConst]
 *    (in)               (perm)
 *         \              /
 *       [CircleTranspose]    [CircleConst]
 *            \               (shape)
 *             \             /
 *             [CircleReshape]      [CircleConst]
 *               \                  (perm)
 *                \                /
 *                 [CircleTranspose]
 *                  \
 *                   \
 *                    [CircleNode]
 *                    (out)
 *
 * AFTER
 *
 *    [CircleNode]        [CircleConst]
 *     (in)                (shape)
 *        \               /
 *        [CircleReshape]
 *         (new)
 *          \
 *          [CircleNode]
 *          (out)
 *
 */

bool RemoveUnnecessaryTransposeNetPass::run(loco::Graph *g)
{
  bool changed = false;

  for (auto node : loco::active_nodes(loco::output_nodes(g)))
  {
    if (auto transpose_node = dynamic_cast<luci::CircleTranspose *>(node))
    {
      if (remove_unnecessary_transpose(transpose_node))
        changed = true;
    }
  }

  return changed;
}

} // namespace luci
