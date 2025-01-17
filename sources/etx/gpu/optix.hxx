#pragma once

#include <etx/gpu/gpu.hxx>

namespace etx {

struct GPUOptixImpl : public GPUDevice {
  GPUOptixImpl();
  ~GPUOptixImpl() override;

  bool rendering_enabled() override;

  GPUBuffer create_buffer(const GPUBuffer::Descriptor& desc) override;
  device_pointer_t get_buffer_device_pointer(GPUBuffer) const override;
  device_pointer_t upload_to_shared_buffer(device_pointer_t ptr, void* data, uint64_t size) override;
  device_pointer_t copy_to_buffer(GPUBuffer, const void* src, uint64_t offset, uint64_t size) override;
  void copy_from_buffer(GPUBuffer, void* dst, uint64_t offset, uint64_t size) override;
  void clear_buffer(GPUBuffer) override;
  void destroy_buffer(GPUBuffer) override;

  GPUPipeline create_pipeline(const GPUPipeline::Descriptor&) override;
  GPUPipeline create_pipeline_from_file(const char*, bool) override;
  void create_pipeline_from_files(TaskScheduler&, uint64_t file_count, const char* files[], GPUPipeline pipelines[], bool) override;
  void destroy_pipeline(GPUPipeline) override;

  GPUAccelerationStructure create_acceleration_structure(const GPUAccelerationStructure::Descriptor&) override;
  device_pointer_t get_acceleration_structure_device_pointer(GPUAccelerationStructure) override;
  void destroy_acceleration_structure(GPUAccelerationStructure) override;

  bool launch(GPUPipeline, const char* function, uint32_t dim_x, uint32_t dim_y, device_pointer_t params, uint64_t params_size) override;

  void setup_denoiser(uint32_t dim_x, uint32_t dim_y) override;
  bool denoise(GPUBuffer input, GPUBuffer output) override;

  ETX_PIMPL_DECLARE(GPUOptixImpl, Data, 2048);
};

}  // namespace etx