﻿#pragma once

#include <etx/render/shared/base.hxx>
#include <vector>

namespace etx {

struct Film {
  Film() = default;
  ~Film() = default;

  void resize(const uint2& dim);

  void atomic_add(const float4& value, const float2& ndc_coord);
  void atomic_add(const float4& value, uint32_t x, uint32_t y);

  void accumulate(const float4& value, const float2& ndc_coord, float t);
  void accumulate(const float4& value, uint32_t x, uint32_t y, float t);

  void merge(const Film& other, float t);
  void clear();

  const uint2& dimensions() const {
    return _dimensions;
  }

  const float4* data() const {
    return _data.data();
  }

 private:
  Film(const Film&) = delete;
  Film& operator=(const Film&) = delete;
  Film(Film&&) = delete;
  Film& operator=(Film&&) = delete;

 private:
  uint2 _dimensions = {};
  std::vector<float4> _data = {};
};

}  // namespace etx
