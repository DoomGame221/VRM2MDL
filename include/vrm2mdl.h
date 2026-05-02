#ifndef VRM2MDL_H
#define VRM2MDL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #define VRM2MDL_API __declspec(dllexport)
#else
  #define VRM2MDL_API __attribute__((visibility("default")))
#endif

// Callback function type for logging
typedef void (*LogCallback)(void* user_data, const char* message);

// Returns the version of the library
VRM2MDL_API const char* vrm2mdl_version();

// Converts a VRM file to SMD/VTA/QC/VMT/VTF files for Source Engine
// vrm_path: Path to the input .vrm file
// output_dir: Directory where the output will be saved (e.g. "./output")
// scale: Scale multiplier for the model
// log_cb: Callback function to receive progress messages (can be NULL)
// user_data: User data passed to the callback function
// Returns 0 on success, non-zero on error.
VRM2MDL_API int vrm2mdl_convert(
    const char* vrm_path,
    const char* output_dir,
    float scale,
    LogCallback log_cb,
    void* user_data
);

#ifdef __cplusplus
}
#endif

#endif // VRM2MDL_H
