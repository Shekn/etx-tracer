﻿#include <etx/render/host/image_pool.hxx>
#include <etx/render/host/distribution_builder.hxx>
#include <etx/render/host/pool.hxx>

#include <tinyexr/tinyexr.hxx>
#include <stb_image/stb_image.hxx>

#include <vector>
#include <unordered_map>
#include <functional>

namespace etx {

bool load_pfm(const char* path, uint2& size, std::vector<uint8_t>& data);

struct ImagePoolImpl {
  void init(uint32_t capacity) {
    image_pool.init(capacity);
  }

  void cleanup() {
    ETX_ASSERT(image_pool.count_alive() == 0);
    image_pool.cleanup();
  }

  uint32_t add_from_file(const std::string& path, uint32_t image_options) {
    auto i = mapping.find(path);
    if (i != mapping.end()) {
      return i->second;
    }

    auto handle = image_pool.alloc();
    auto& image = image_pool.get(handle);
    load_image(image, path.c_str(), image_options);
    if (image_options & Image::BuildSamplingTable) {
      build_sampling_table(image);
    }

    mapping[path] = handle;
    return handle;
  }

  const Image& get(uint32_t handle) const {
    return image_pool.get(handle);
  }

  void remove(uint32_t handle) {
    if (handle == kInvalidIndex) {
      return;
    }

    free_image(image_pool.get(handle));
    image_pool.free(handle);

    for (auto i = mapping.begin(), e = mapping.end(); i != e; ++i) {
      if (i->second == handle) {
        mapping.erase(i);
        break;
      }
    }
  }

  void remove_all() {
    image_pool.free_all(std::bind(&ImagePoolImpl::free_image, this, std::placeholders::_1));
    mapping.clear();
  }

  void load_image(Image& img, const char* file_name, uint32_t options) {
    ETX_ASSERT(img.pixels == nullptr);
    ETX_ASSERT(img.x_distributions == nullptr);
    ETX_ASSERT(img.y_distribution.size == 0);
    ETX_ASSERT(img.y_distribution.values == 0);

    std::vector<uint8_t> source_data = {};
    Image::Format format = load_data(file_name, source_data, img.isize);
    if ((format == Image::Format::Undefined) || (img.isize.x * img.isize.y == 0)) {
      source_data.resize(sizeof(float4));
      *(float4*)(source_data.data()) = {1.0f, 1.0f, 1.0f, 1.0f};
      format = Image::Format::RGBA32F;
      img.options = Image::RepeatU | Image::RepeatV;
      img.isize.x = 1;
      img.isize.y = 1;
    }

    img.options = img.options | options;
    img.fsize.x = static_cast<float>(img.isize.x);
    img.fsize.y = static_cast<float>(img.isize.y);
    img.pixels = reinterpret_cast<float4*>(calloc(1llu * img.isize.x * img.isize.y, sizeof(float4)));

    bool srgb = (options & Image::Linear) == 0;

    if (format == Image::Format::RGBA8) {
      auto src_data = reinterpret_cast<const ubyte4*>(source_data.data());
      for (uint32_t y = 0; y < img.isize.y; ++y) {
        for (uint32_t x = 0; x < img.isize.x; ++x) {
          uint32_t i = x + y * img.isize.x;
          float4 f{float(src_data[i].x) / 255.0f, float(src_data[i].y) / 255.0f, float(src_data[i].z) / 255.0f, float(src_data[i].w) / 255.0f};
          if (srgb) {
            f.x = std::pow(f.x, 2.2f);  // TODO : fix gamma conversion
            f.y = std::pow(f.y, 2.2f);  // TODO : fix gamma conversion
            f.z = std::pow(f.z, 2.2f);  // TODO : fix gamma conversion
          }
          uint32_t j = x + (img.isize.y - 1 - y) * img.isize.x;
          img.pixels[j] = f;
        }
      }
    } else if (format == Image::Format::RGBA32F) {
      memcpy(img.pixels, source_data.data(), source_data.size());
    } else {
      ETX_FAIL_FMT("Unsupported image format %u", format);
    }

    for (uint32_t i = 0, e = img.isize.x * img.isize.y; i < e; ++i) {
      if (img.pixels[i].w < 1.0f) {
        img.options = img.options | Image::HasAlphaChannel;
        break;
      }
    }
  }

  void build_sampling_table(Image& img) {
    float total_weight = 0.0f;
    bool uniform_sampling = (img.options & Image::UniformSamplingTable) == Image::UniformSamplingTable;
    DistributionBuilder y_dist(img.y_distribution, img.isize.y);
    img.x_distributions = reinterpret_cast<Distribution*>(calloc(img.isize.y, sizeof(Distribution)));

    for (uint32_t y = 0; y < img.isize.y; ++y) {
      float v = (float(y) + 0.5f) / img.fsize.y;
      float row_value = 0.0f;

      DistributionBuilder d_x(img.x_distributions[y], img.isize.x);
      for (uint32_t x = 0; x < img.isize.x; ++x) {
        float u = (float(x) + 0.5f) / img.fsize.x;
        float4 px = img.read(img.fsize * float2{u, v});
        float lum = luminance(px);
        row_value += lum;
        d_x.add(lum);
      }
      d_x.finalize();

      float row_weight = uniform_sampling ? 1.0f : std::sin(v * kPi);
      row_value *= row_weight;

      total_weight += row_value;
      y_dist.add(row_value);
    }
    y_dist.finalize();

    img.normalization = total_weight / (img.fsize.x * img.fsize.y);
  }

  void free_image(Image& img) {
    free(img.pixels);
    for (uint32_t i = 0; i < img.y_distribution.size; ++i) {
      free(img.x_distributions[i].values);
    }
    free(img.x_distributions);
    free(img.y_distribution.values);
    img = {};
  }

  Image::Format load_data(const char* source, std::vector<uint8_t>& data, uint2& dimensions) {
    if (source == nullptr)
      return Image::Format::Undefined;

    const char* ext = nullptr;
    if (uint64_t l = strlen(source)) {
      while ((l > 0) && (source[--l] != '.')) {
      }
      ext = source + l;
    } else {
      return Image::Format::Undefined;
    }

    if (strcmp(ext, ".exr") == 0) {
      int w = 0;
      int h = 0;
      const char* error = nullptr;
      float* rgba_data = nullptr;
      if (LoadEXR(&rgba_data, &w, &h, source, &error) != TINYEXR_SUCCESS) {
        printf("Failed to load EXR from file: %s\n", error);
        return Image::Format::Undefined;
      }

      for (int i = 0; i < 4 * w * h; ++i) {
        if (std::isinf(rgba_data[i])) {
          rgba_data[i] = 65504.0f;  // max value in half-float
        }
        if (std::isnan(rgba_data[i]) || (rgba_data[i] < 0.0f)) {
          rgba_data[i] = 0.0f;
        }
      }

      dimensions = {w, h};
      data.resize(sizeof(float4) * w * h);
      memcpy(data.data(), rgba_data, sizeof(float4) * w * h);
      // auto row_size = sizeof(float4) * w;
      // for (int y = 0; y < h; ++y) {
      //   memcpy(data.data() + row_size * (h - 1 - y), rgba_data + 4llu * y * w, row_size);
      // }
      free(rgba_data);

      return Image::Format::RGBA32F;
    }

    if (strcmp(ext, ".hdr") == 0) {
      int w = 0;
      int h = 0;
      int c = 0;
      stbi_set_flip_vertically_on_load(false);
      auto image = stbi_loadf(source, &w, &h, &c, 0);
      if (image == nullptr) {
        return Image::Format::Undefined;
      }

      dimensions = {w, h};
      data.resize(sizeof(float4) * w * h);
      auto ptr = reinterpret_cast<float4*>(data.data());
      if (c == 4) {
        memcpy(ptr, image, sizeof(float4) * w * h);
      } else {
        for (int i = 0; i < w * h; ++i) {
          ptr[i] = {image[3 * i + 0], image[3 * i + 1], image[3 * i + 1], 1.0f};
        }
      }
      free(image);
      return Image::Format::RGBA32F;
    }

    if (strcmp(ext, ".pfm") == 0) {
      return load_pfm(source, dimensions, data) ? Image::Format::RGBA32F : Image::Format::Undefined;
    }

    int w = 0;
    int h = 0;
    int c = 0;
    stbi_set_flip_vertically_on_load(true);
    auto image = stbi_load(source, &w, &h, &c, 0);
    if (image == nullptr) {
      return Image::Format::Undefined;
    }

    if ((c != 3) && (c != 4)) {
      free(image);
      ETX_FAIL_FMT("Unsupported (yet) image format with %d channels", c);
    }

    dimensions = {w, h};
    data.resize(4llu * w * h);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(data.data());
    switch (c) {
      case 4: {
        memcpy(ptr, image, 4llu * w * h);
        break;
      }

      case 3: {
        for (int i = 0; i < w * h; ++i) {
          ptr[4 * i + 0] = image[3 * i + 0];
          ptr[4 * i + 1] = image[3 * i + 1];
          ptr[4 * i + 2] = image[3 * i + 2];
          ptr[4 * i + 3] = 255;
        }
        break;
      }

      default:
        break;
    }

    free(image);
    return Image::Format::RGBA8;
  }

  ObjectIndexPool<Image> image_pool;
  std::unordered_map<std::string, uint32_t> mapping;
  Image empty;
};

ETX_PIMPL_IMPLEMENT_ALL(ImagePool, Impl);

void ImagePool::init(uint32_t capacity) {
  _private->init(capacity);
}

void ImagePool::cleanup() {
  _private->cleanup();
}

uint32_t ImagePool::add_from_file(const std::string& path, uint32_t image_options) {
  return _private->add_from_file(path, image_options);
}

const Image& ImagePool::get(uint32_t handle) {
  return _private->get(handle);
}

void ImagePool::remove(uint32_t handle) {
  _private->remove(handle);
}

void ImagePool::remove_all() {
  _private->remove_all();
}

Image* ImagePool::as_array() {
  return _private->image_pool.data();
}

uint64_t ImagePool::array_size() {
  return 1llu + _private->image_pool.latest_alive_index();
}

bool load_pfm(const char* path, uint2& size, std::vector<uint8_t>& data) {
  FILE* in_file = fopen(path, "rb");
  if (in_file == nullptr) {
    return false;
  }

  char buffer[16] = {};

  auto read_line = [&]() {
    memset(buffer, 0, sizeof(buffer));
    char c = {};
    int p = 0;
    while ((p < 16) && (fread(&c, 1, 1, in_file) == 1)) {
      if (c == '\n') {
        return;
      } else {
        buffer[p++] = c;
      }
    }
  };

  read_line();
  if ((buffer[0] != 'P') && (buffer[1] != 'f') && (buffer[1] != 'F')) {
    fclose(in_file);
    return false;
  }
  char format = buffer[1];

  read_line();
  if (sscanf(buffer, "%d", &size.x) != 1) {
    fclose(in_file);
    return false;
  }

  read_line();
  if (sscanf(buffer, "%d", &size.y) != 1) {
    fclose(in_file);
    return false;
  }

  read_line();
  float scale = 0.0f;
  if (sscanf(buffer, "%f", &scale) != 1) {
    fclose(in_file);
    return false;
  }

  auto sw = [](float t) {
    uint32_t x;
    memcpy(&x, &t, 4);
    x = ((x & 0x000000ff) >> 0) << 24 | ((x & 0x0000ff00) >> 8) << 16 | ((x & 0x00ff0000) >> 16) << 8 | ((x & 0xff000000) >> 24) << 0;
    memcpy(&t, &x, 4);
    return t;
  };

  data.resize(sizeof(float4) * size.x * size.y);
  auto data_ptr = reinterpret_cast<float4*>(data.data());

  if (format == 'f') {
    for (uint32_t i = 0; i < size.y; ++i) {
      for (uint32_t j = 0; j < size.x; ++j) {
        float value = 0.0f;
        if (fread(&value, sizeof(float), 1, in_file) != 1) {
          fclose(in_file);
          return false;
        }
        data_ptr[j + i * size.x] = {value, value, value, 1.0f};
      }
    }
  } else if (format == 'F') {
    for (uint32_t i = 0; i < size.y; ++i) {
      for (uint32_t j = 0; j < size.x; ++j) {
        float3 value = {};
        if (fread(&value, 3 * sizeof(float), 1, in_file) != 1) {
          fclose(in_file);
          return false;
        }
        data_ptr[j + i * size.x] = {value, 1.0f};
      }
    }
  } else {
    fclose(in_file);
    return false;
  }

  fclose(in_file);
  return true;
}

}  // namespace etx