// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/public/cpp/bindings/array.h>

#include "mojom/tensor.mojom.h"

#ifndef ML_TENSOR_VIEW_H_
#define ML_TENSOR_VIEW_H_

namespace ml {

// Provides basic error checking and a common interface for mojom::TensorPtrs of
// any underlying data type.
//
// Basic usage of a TensorView is as follows.
// Non-const view:
//   TensorPtr ptr;
//   TensorView<double> view(ptr);
//   view.Allocate();                  // Creates a FloatList in ptr.
//   view.GetValues().push_back(0.5);  // Adds first elem of FloatList in ptr.
//
// Const view:
//   const TensorPtr ptr = ...
//   const TensorView<double> view(ptr);
//   double v = view.GetValues()[0];   // Gets first elem of FloatList in ptr.
//
// Type-specific funtionality is implemented in template specializations below.
template <typename T>
class TensorView {
 public:
  // The given tensor must outlive this view.
  explicit TensorView(
      const chromeos::machine_learning::mojom::TensorPtr& tensor)
      : tensor_(tensor) {}

  // Return the shape array of the tensor.
  mojo::Array<int64_t>& GetShape() { return tensor_->shape->value; }

  const mojo::Array<int64_t>& GetShape() const {
    return const_cast<TensorView<T>*>(this)->GetShape();
  }

  // Return the value array of the tensor. Should be specialized for each tensor
  // data type T.
  mojo::Array<T>& GetValues() { return mojo::Array<T>(nullptr); }

  const mojo::Array<T>& GetValues() const {
    return const_cast<TensorView<T>*>(this)->GetValues();
  }

  // Return true if the tensor contains values of the correct type. Should be
  // specialized for each tensor data type T.
  bool IsValidType() const { return false; }

  // Return true if the tensor is in a valid format (i.e. valid dimensions and
  // the right number of entries for its shape).
  bool IsValidFormat() const {
    const mojo::Array<int64_t>& dims = GetShape();

    // Special case: no entries.
    if (dims.empty())
      return GetValues().empty();

    // Otherwise, values size should be the product of all dimension lengths.
    int64_t num_entries = 1;
    for (const int64_t dim : dims) {
      if (dim < 0)
        return false;

      num_entries *= dim;
    }
    return num_entries == GetValues().size();
  }

  // Allocate memory for the members of the tensor object (including values).
  void Allocate() {
    tensor_->shape = chromeos::machine_learning::mojom::Int64List::New();
    GetShape().SetToEmpty();

    tensor_->data = chromeos::machine_learning::mojom::ValueList::New();
    AllocateValues();
    GetValues().SetToEmpty();
  }

 private:
  // Allocate memory for the value array of this tensor. Should be specialized
  // for each tensor data type T.
  void AllocateValues() {}

  const chromeos::machine_learning::mojom::TensorPtr& tensor_;

  DISALLOW_COPY_AND_ASSIGN(TensorView);
};

// Specializations for int tensors.
template <>
mojo::Array<int64_t>& TensorView<int64_t>::GetValues();
template <>
bool TensorView<int64_t>::IsValidType() const;
template <>
void TensorView<int64_t>::AllocateValues();

// Specializations for float tensors.
template <>
mojo::Array<double>& TensorView<double>::GetValues();
template <>
bool TensorView<double>::IsValidType() const;
template <>
void TensorView<double>::AllocateValues();

}  // namespace ml

#endif  // ML_TENSOR_VIEW_H_
