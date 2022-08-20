#include <etx/rt/shared/optix.hxx>
#include <etx/rt/shared/vcm_shared.hxx>

using namespace etx;

static __constant__ VCMGlobal global;

ETX_GPU_CODE void finish_ray(VCMIteration& iteration, VCMPathState& state) {
  float3 result = state.merged * iteration.vm_normalization + (state.gathered / spectrum::sample_pdf()).to_xyz();

  uint32_t x = state.global_index % global.scene.camera.image_size.x;
  uint32_t y = state.global_index / global.scene.camera.image_size.x;
  uint32_t c = x + (global.scene.camera.image_size.y - 1 - y) * global.scene.camera.image_size.x;
  float4& current = global.camera_iteration_image[c];
  atomicAdd(&current.x, result.x);
  atomicAdd(&current.y, result.y);
  atomicAdd(&current.z, result.z);
}

ETX_GPU_CODE void continue_tracing(VCMIteration& iteration, const VCMPathState& state) {
  int i = atomicAdd(&iteration.active_paths, 1u);
  global.output_state[i] = state;
}

RAYGEN(main) {
  uint3 idx = optixGetLaunchIndex();
  auto& state = global.input_state[idx.x];
  const auto& scene = global.scene;
  const auto& options = global.options;

  Raytracing rt;
  bool found_intersection = rt.trace(scene, state.ray, state.intersection, state.sampler);

  state.clear_ray_action();

  Medium::Sample medium_sample = vcm_try_sampling_medium(scene, state);
  if (medium_sample.sampled_medium()) {
    bool continue_ray = vcm_handle_sampled_medium(scene, medium_sample, options, state);
    state.continue_ray(continue_ray);
  } else if (found_intersection == false) {
    vcm_cam_handle_miss(scene, options, state);
    state.continue_ray(false);
  } else if (vcm_handle_boundary_bsdf(scene, PathSource::Camera, state)) {
    state.continue_ray(true);
  }
}
