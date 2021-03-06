/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once
#include "paddle/memory/memcpy.h"
#include "paddle/platform/enforce.h"

namespace paddle {
namespace framework {

template <typename T>
inline void Tensor::check_memory_size() const {
  PADDLE_ENFORCE_NOT_NULL(
      holder_, "Tensor holds no memory. Call Tensor::mutable_data first.");
  PADDLE_ENFORCE_GE(
      holder_->size(), numel() * sizeof(T) + offset_,
      "Tensor's dims_ is out of bound. Call Tensor::mutable_data "
      "first to re-allocate memory.\n"
      "or maybe the required data-type mismatches the data already stored.");
}

template <typename T>
inline const T* Tensor::data() const {
  check_memory_size<T>();
  return reinterpret_cast<const T*>(
      reinterpret_cast<uintptr_t>(holder_->ptr()) + offset_);
}

template <typename T>
inline T* Tensor::data() {
  check_memory_size<T>();
  return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(holder_->ptr()) +
                              offset_);
}

template <typename T>
inline T* Tensor::mutable_data(DDim dims, platform::Place place) {
  static_assert(std::is_pod<T>::value, "T must be POD");
  Resize(dims);
  return mutable_data<T>(place);
}

template <typename T>
inline T* Tensor::mutable_data(platform::Place place) {
  static_assert(std::is_pod<T>::value, "T must be POD");
  PADDLE_ENFORCE_GT(numel(), 0,
                    "Tensor's numel must be larger than zero to call "
                    "Tensor::mutable_data. Call Tensor::set_dim first.");
  /* some versions of boost::variant don't have operator!= */
  int64_t size = numel() * sizeof(T);
  if (holder_ == nullptr || !(holder_->place() == place) ||
      holder_->size() < size + offset_) {
    if (platform::is_cpu_place(place)) {
      holder_.reset(new PlaceholderImpl<T, platform::CPUPlace>(
          boost::get<platform::CPUPlace>(place), size));
    } else if (platform::is_gpu_place(place)) {
#ifndef PADDLE_WITH_CUDA
      PADDLE_THROW("'GPUPlace' is not supported in CPU only device.");
    }
#else
      holder_.reset(new PlaceholderImpl<T, platform::GPUPlace>(
          boost::get<platform::GPUPlace>(place), size));
    }
#endif
    offset_ = 0;
  }
  return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(holder_->ptr()) +
                              offset_);
}

template <typename T>
inline Tensor& Tensor::ShareDataWith(const Tensor& src) {
  src.check_memory_size<T>();
  *this = src;
  return *this;
}

template <typename T>
inline void Tensor::CopyFrom(const Tensor& src,
                             const platform::Place& dst_place,
                             const platform::DeviceContext& ctx) {
  src.check_memory_size<T>();
  Resize(src.dims());

  auto src_place = src.holder_->place();
  auto src_ptr = static_cast<const void*>(src.data<T>());

  auto dst_ptr = static_cast<void*>(mutable_data<T>(dst_place));

  auto size = src.numel() * sizeof(T);

  if (platform::is_cpu_place(src_place) && platform::is_cpu_place(dst_place)) {
    memory::Copy(boost::get<platform::CPUPlace>(dst_place), dst_ptr,
                 boost::get<platform::CPUPlace>(src_place), src_ptr, size);
  }
#ifdef PADDLE_WITH_CUDA
  else if (platform::is_gpu_place(src_place) &&
           platform::is_cpu_place(dst_place)) {
    auto src_gpu_place = boost::get<platform::GPUPlace>(src_place);
    auto dst_cpu_place = boost::get<platform::CPUPlace>(dst_place);
    auto ctx_place = ctx.GetPlace();
    PADDLE_ENFORCE(platform::is_gpu_place(ctx_place));
    auto ctx_gpu_place = boost::get<platform::GPUPlace>(ctx_place);
    PADDLE_ENFORCE_EQ(src_gpu_place, ctx_gpu_place);
    memory::Copy(
        dst_cpu_place, dst_ptr, src_gpu_place, src_ptr, size,
        reinterpret_cast<const platform::CUDADeviceContext&>(ctx).stream());
  } else if (platform::is_cpu_place(src_place) &&
             platform::is_gpu_place(dst_place)) {
    auto src_cpu_place = boost::get<platform::CPUPlace>(src_place);
    auto dst_gpu_place = boost::get<platform::GPUPlace>(dst_place);
    auto ctx_place = ctx.GetPlace();
    PADDLE_ENFORCE(platform::is_gpu_place(ctx_place));
    auto ctx_gpu_place = boost::get<platform::GPUPlace>(ctx_place);
    PADDLE_ENFORCE_EQ(dst_gpu_place, ctx_gpu_place);
    memory::Copy(
        dst_gpu_place, dst_ptr, src_cpu_place, src_ptr, size,
        reinterpret_cast<const platform::CUDADeviceContext&>(ctx).stream());
  } else if (platform::is_gpu_place(src_place) &&
             platform::is_gpu_place(dst_place)) {
    auto src_gpu_place = boost::get<platform::GPUPlace>(src_place);
    auto dst_gpu_place = boost::get<platform::GPUPlace>(dst_place);
    auto ctx_place = ctx.GetPlace();
    PADDLE_ENFORCE(platform::is_gpu_place(ctx_place));
    auto ctx_gpu_place = boost::get<platform::GPUPlace>(ctx_place);
    PADDLE_ENFORCE_EQ(src_gpu_place, ctx_gpu_place);
    memory::Copy(
        dst_gpu_place, dst_ptr, src_gpu_place, src_ptr, size,
        reinterpret_cast<const platform::CUDADeviceContext&>(ctx).stream());
  }
#endif
}

template <typename T>
inline void Tensor::CopyFromVector(const std::vector<T>& src,
                                   const platform::DeviceContext& ctx) {
  auto dst_place = ctx.GetPlace();
  auto src_ptr = static_cast<const void*>(src.data());
  platform::CPUPlace src_place;
  auto dst_ptr = static_cast<void*>(mutable_data<T>(dst_place));
  auto size = src.size() * sizeof(T);

  if (platform::is_cpu_place(dst_place)) {
    memory::Copy(boost::get<platform::CPUPlace>(dst_place), dst_ptr, src_place,
                 src_ptr, size);
  }
#ifdef PADDLE_WITH_CUDA
  else if (platform::is_gpu_place(dst_place)) {
    memory::Copy(
        boost::get<platform::GPUPlace>(dst_place), dst_ptr, src_place, src_ptr,
        size,
        reinterpret_cast<const platform::CUDADeviceContext&>(ctx).stream());
  }
#endif
}

template <typename T>
inline Tensor Tensor::Slice(const int& begin_idx, const int& end_idx) const {
  check_memory_size<T>();
  PADDLE_ENFORCE_GE(begin_idx, 0, "Slice begin index is less than zero.");
  PADDLE_ENFORCE_LE(end_idx, dims_[0], "Slice end index is out of bound.");
  PADDLE_ENFORCE_LT(begin_idx, end_idx,
                    "Begin index must be less than end index.");

  if (dims_[0] == 1) {
    return *this;
  } else {
    size_t base = numel() / dims_[0];
    Tensor dst;
    dst.holder_ = holder_;
    DDim dst_dims = dims_;
    dst_dims[0] = end_idx - begin_idx;
    dst.Resize(dst_dims);
    dst.offset_ = offset_ + begin_idx * base * sizeof(T);
    return dst;
  }
}

inline Tensor& Tensor::Resize(const DDim& dims) {
  dims_ = dims;
  return *this;
}

inline const DDim& Tensor::dims() const { return dims_; }

inline int64_t Tensor::numel() const { return product(dims_); }

template <typename T>
inline Tensor ReshapeToMatrix(const Tensor& src, int num_col_dims) {
  Tensor res;
  res.ShareDataWith<T>(src);
  res.Resize(flatten_to_2d(src.dims(), num_col_dims));
  return res;
}

}  // namespace framework
}  // namespace paddle
