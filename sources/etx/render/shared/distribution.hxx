﻿#pragma once

#include <etx/render/shared/base.hxx>

namespace etx {

struct ETX_ALIGNED Distribution {
  struct Entry {
    float value = 0.0f;
    float pdf = 0.0f;
    float cdf = 0.0f;
  };
  ArrayView<Entry> values ETX_EMPTY_INIT;
  float total_weight ETX_EMPTY_INIT;

  ETX_GPU_CODE uint32_t sample(float rnd, float& pdf) const {
    auto index = find(rnd);
    pdf = values[index].pdf;
    return index;
  }

  ETX_GPU_CODE float pdf(float value, uint32_t& index) const {
    index = static_cast<uint32_t>(min(value * float(values.count) + 0.5f, float(values.count - 1llu)));
    return values[index].pdf;
  }

  ETX_GPU_CODE uint32_t find(float rnd) const {
    uint32_t b = 0;
    uint32_t e = static_cast<uint32_t>(values.count);
    do {
      uint32_t m = b + (e - b) / 2;
      if (values[m].cdf >= rnd) {
        e = m;
      } else {
        b = m;
      }
    } while ((e - b) > 1);
    return b;
  }
};

}  // namespace etx