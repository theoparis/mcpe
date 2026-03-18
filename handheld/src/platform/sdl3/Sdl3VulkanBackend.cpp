#include "Sdl3VulkanBackend.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <unistd.h>

#ifndef MCPE_VULKAN_SHADER_DIR
#define MCPE_VULKAN_SHADER_DIR "./shaders"
#endif

namespace {

constexpr std::array<const char *, 1> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};
constexpr uint32_t kMaxTextureDescriptors = 1024;
constexpr uint32_t kInvalidTextureDescriptorIndex = ~uint32_t(0);
constexpr std::array<const char *, 5> kRequiredShaderFiles = {
    "terrain_mesh.vert.spv",
    "terrain_mesh.frag.spv",
    "terrain_mesh_alpha_test.frag.spv",
    "textured_quad.vert.spv",
    "textured_quad.frag.spv",
};

void addSearchPath(std::vector<std::string> &paths, const std::string &path) {
  if (path.empty()) {
    return;
  }

  std::string normalized = path;
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }

  if (normalized.empty()) {
    return;
  }

  if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
    paths.push_back(normalized);
  }
}

auto fileExists(const std::string &path) -> bool {
  std::ifstream file(path.c_str(), std::ios::binary);
  return file.good();
}

auto executablePath() -> std::string {
  std::array<char, 4096> buffer{};
  const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (size <= 0) {
    return {};
  }

  buffer[(size_t)size] = '\0';
  return std::string(buffer.data());
}

auto directoryName(const std::string &path) -> std::string {
  const std::string::size_type slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  return path.substr(0, slash);
}

auto directoryHasAllShaders(const std::string &path) -> bool {
  for (const char *filename : kRequiredShaderFiles) {
    if (!fileExists(path + "/" + filename)) {
      return false;
    }
  }
  return true;
}

auto findShaderDirectory(std::string &shaderDir, std::string &error) -> bool {
  std::vector<std::string> candidates;

  if (const char *env = std::getenv("MCPE_VULKAN_SHADER_DIR")) {
    addSearchPath(candidates, env);
  }

  addSearchPath(candidates, MCPE_VULKAN_SHADER_DIR);
  addSearchPath(candidates, "./bazel-bin/shaders");
  addSearchPath(candidates, "./shaders");

  if (const char *runfiles = std::getenv("RUNFILES_DIR")) {
    addSearchPath(candidates, std::string(runfiles) + "/_main/shaders");
    addSearchPath(candidates, std::string(runfiles) + "/_main");
  }

  if (const char *testSrcDir = std::getenv("TEST_SRCDIR")) {
    addSearchPath(candidates, std::string(testSrcDir) + "/_main/shaders");
    addSearchPath(candidates, std::string(testSrcDir) + "/_main");
  }

  if (const char *base = SDL_GetBasePath()) {
    addSearchPath(candidates, std::string(base) + "shaders");
    addSearchPath(candidates, std::string(base) + "../shaders");
  }

  const std::string exePath = executablePath();
  if (!exePath.empty()) {
    addSearchPath(candidates, exePath + ".runfiles/_main/shaders");
    addSearchPath(candidates, exePath + ".runfiles/_main");

    const std::string exeDir = directoryName(exePath);
    addSearchPath(candidates, exeDir + "/shaders");
    addSearchPath(candidates, exeDir + "/../shaders");
  }

  for (const auto &candidate : candidates) {
    if (directoryHasAllShaders(candidate)) {
      shaderDir = candidate;
      return true;
    }
  }

  error = "Failed to locate Vulkan shaders. Searched:";
  for (const auto &candidate : candidates) {
    error += "\n  ";
    error += candidate;
  }
  return false;
}

auto vkResultToString(VkResult result) -> const char * {
  switch (result) {
  case VK_SUCCESS:
    return "VK_SUCCESS";
  case VK_NOT_READY:
    return "VK_NOT_READY";
  case VK_TIMEOUT:
    return "VK_TIMEOUT";
  case VK_EVENT_SET:
    return "VK_EVENT_SET";
  case VK_EVENT_RESET:
    return "VK_EVENT_RESET";
  case VK_INCOMPLETE:
    return "VK_INCOMPLETE";
  case VK_ERROR_OUT_OF_HOST_MEMORY:
    return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:
    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED:
    return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST:
    return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_MEMORY_MAP_FAILED:
    return "VK_ERROR_MEMORY_MAP_FAILED";
  case VK_ERROR_LAYER_NOT_PRESENT:
    return "VK_ERROR_LAYER_NOT_PRESENT";
  case VK_ERROR_EXTENSION_NOT_PRESENT:
    return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT:
    return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER:
    return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_TOO_MANY_OBJECTS:
    return "VK_ERROR_TOO_MANY_OBJECTS";
  case VK_ERROR_FORMAT_NOT_SUPPORTED:
    return "VK_ERROR_FORMAT_NOT_SUPPORTED";
  case VK_ERROR_FRAGMENTED_POOL:
    return "VK_ERROR_FRAGMENTED_POOL";
  case VK_ERROR_SURFACE_LOST_KHR:
    return "VK_ERROR_SURFACE_LOST_KHR";
  case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
    return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
  case VK_SUBOPTIMAL_KHR:
    return "VK_SUBOPTIMAL_KHR";
  case VK_ERROR_OUT_OF_DATE_KHR:
    return "VK_ERROR_OUT_OF_DATE_KHR";
  default:
    return "VK_UNKNOWN_RESULT";
  }
}

auto makeVkError(const char *message, VkResult result) -> std::string {
  std::string out(message);
  out += " (";
  out += vkResultToString(result);
  out += ")";
  return out;
}

auto textureFormatSupported(TextureFormat format) -> bool {
  return format == TEXF_UNCOMPRESSED_8888;
}

auto textureByteCount(
    uint32_t width, uint32_t height, TextureFormat format) -> VkDeviceSize {
  if (format != TEXF_UNCOMPRESSED_8888) {
    return 0;
  }
  return (VkDeviceSize)width * (VkDeviceSize)height * 4;
}

auto vkTextureFormat(TextureFormat format) -> VkFormat {
  if (format == TEXF_UNCOMPRESSED_8888) {
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
  return VK_FORMAT_UNDEFINED;
}

auto preferredPresentModes() -> std::array<VkPresentModeKHR, 3> {
  const char *overrideMode = std::getenv("MCPE_VULKAN_PRESENT_MODE");
  if (overrideMode != nullptr) {
    if (std::strcmp(overrideMode, "fifo") == 0) {
      return {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
          VK_PRESENT_MODE_IMMEDIATE_KHR};
    }
    if (std::strcmp(overrideMode, "mailbox") == 0) {
      return {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
          VK_PRESENT_MODE_FIFO_KHR};
    }
  }

  return {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
      VK_PRESENT_MODE_FIFO_KHR};
}

void configureBlendState(GraphicsBlendMode blendMode,
    VkPipelineColorBlendAttachmentState &colorBlendAttachment) {
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_TRUE;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  switch (blendMode) {
  case GraphicsBlendMode::DstColorSrcColor:
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    break;
  case GraphicsBlendMode::ZeroOneMinusSrcColor:
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    break;
  case GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor:
    colorBlendAttachment.srcColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    colorBlendAttachment.srcAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    colorBlendAttachment.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    break;
  case GraphicsBlendMode::Alpha:
  default:
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    break;
  }
}

void multiplyMatrices4x4(
    const float *matrix1, const float *matrix2, float *result) {
  result[0] = matrix1[0] * matrix2[0] + matrix1[4] * matrix2[1] +
      matrix1[8] * matrix2[2] + matrix1[12] * matrix2[3];
  result[4] = matrix1[0] * matrix2[4] + matrix1[4] * matrix2[5] +
      matrix1[8] * matrix2[6] + matrix1[12] * matrix2[7];
  result[8] = matrix1[0] * matrix2[8] + matrix1[4] * matrix2[9] +
      matrix1[8] * matrix2[10] + matrix1[12] * matrix2[11];
  result[12] = matrix1[0] * matrix2[12] + matrix1[4] * matrix2[13] +
      matrix1[8] * matrix2[14] + matrix1[12] * matrix2[15];
  result[1] = matrix1[1] * matrix2[0] + matrix1[5] * matrix2[1] +
      matrix1[9] * matrix2[2] + matrix1[13] * matrix2[3];
  result[5] = matrix1[1] * matrix2[4] + matrix1[5] * matrix2[5] +
      matrix1[9] * matrix2[6] + matrix1[13] * matrix2[7];
  result[9] = matrix1[1] * matrix2[8] + matrix1[5] * matrix2[9] +
      matrix1[9] * matrix2[10] + matrix1[13] * matrix2[11];
  result[13] = matrix1[1] * matrix2[12] + matrix1[5] * matrix2[13] +
      matrix1[9] * matrix2[14] + matrix1[13] * matrix2[15];
  result[2] = matrix1[2] * matrix2[0] + matrix1[6] * matrix2[1] +
      matrix1[10] * matrix2[2] + matrix1[14] * matrix2[3];
  result[6] = matrix1[2] * matrix2[4] + matrix1[6] * matrix2[5] +
      matrix1[10] * matrix2[6] + matrix1[14] * matrix2[7];
  result[10] = matrix1[2] * matrix2[8] + matrix1[6] * matrix2[9] +
      matrix1[10] * matrix2[10] + matrix1[14] * matrix2[11];
  result[14] = matrix1[2] * matrix2[12] + matrix1[6] * matrix2[13] +
      matrix1[10] * matrix2[14] + matrix1[14] * matrix2[15];
  result[3] = matrix1[3] * matrix2[0] + matrix1[7] * matrix2[1] +
      matrix1[11] * matrix2[2] + matrix1[15] * matrix2[3];
  result[7] = matrix1[3] * matrix2[4] + matrix1[7] * matrix2[5] +
      matrix1[11] * matrix2[6] + matrix1[15] * matrix2[7];
  result[11] = matrix1[3] * matrix2[8] + matrix1[7] * matrix2[9] +
      matrix1[11] * matrix2[10] + matrix1[15] * matrix2[11];
  result[15] = matrix1[3] * matrix2[12] + matrix1[7] * matrix2[13] +
      matrix1[11] * matrix2[14] + matrix1[15] * matrix2[15];
}

void multiplyMatrixVector4x4(
    const float *matrix, const float *vector, float *result) {
  result[0] = matrix[0] * vector[0] + matrix[4] * vector[1] +
      matrix[8] * vector[2] + matrix[12] * vector[3];
  result[1] = matrix[1] * vector[0] + matrix[5] * vector[1] +
      matrix[9] * vector[2] + matrix[13] * vector[3];
  result[2] = matrix[2] * vector[0] + matrix[6] * vector[1] +
      matrix[10] * vector[2] + matrix[14] * vector[3];
  result[3] = matrix[3] * vector[0] + matrix[7] * vector[1] +
      matrix[11] * vector[2] + matrix[15] * vector[3];
}

void correctProjectionForVulkan(float projection[16]) {
  static const float kClipCorrection[16] = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, -1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f,
  };

  float corrected[16];
  multiplyMatrices4x4(kClipCorrection, projection, corrected);
  std::memcpy(projection, corrected, sizeof(corrected));
}

auto hasStencilComponent(VkFormat format) -> bool {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
      format == VK_FORMAT_D24_UNORM_S8_UINT;
}

} // namespace

auto Sdl3VulkanBackend::kind() const -> GraphicsBackendKind {
  return GraphicsBackendKind::Vulkan;
}

auto Sdl3VulkanBackend::name() const -> const char * { return "vulkan"; }

auto Sdl3VulkanBackend::windowFlags() const -> Uint32 {
  return SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
}

auto Sdl3VulkanBackend::createWindow(
    AppContext &context, const char *title, int width, int height,
    std::string &error) -> bool {
  if (!_libraryLoaded && !SDL_Vulkan_LoadLibrary(nullptr)) {
    error = SDL_GetError();
    return false;
  }
  _libraryLoaded = true;

  context.window = SDL_CreateWindow(title, width, height, windowFlags());
  if (!context.window) {
    error = SDL_GetError();
    return false;
  }

  if (!createInstance(error) || !createSurface(context.window, error) ||
      !pickPhysicalDevice(error) || !createLogicalDevice(error) ||
      !createCommandPool(error) || !createSyncObjects(error) ||
      !createPersistentResources(error)) {
    destroyWindow(context);
    return false;
  }

  SDL_ShowWindow(context.window);
  SDL_RaiseWindow(context.window);
  return true;
}

auto Sdl3VulkanBackend::resetSurface(
    AppContext &context, uint32_t width, uint32_t height, std::string &error)
    -> bool {
  if (!_device) {
    error = "Vulkan device is not initialized";
    return false;
  }

  waitForDeviceIdle();
  cleanupSwapchain();

  if (width == 0 || height == 0) {
    _swapchainReady = false;
    return true;
  }

  if (!createSwapchain(width, height, error) || !createImageViews(error) ||
      !createDepthResources(error) || !createRenderPass(error) ||
      !createGraphicsPipeline(error) || !createFramebuffers(error) ||
      !allocateCommandBuffers(error)) {
    cleanupSwapchain();
    return false;
  }

  _currentFrame = 0;
  _swapchainReady = true;
  return true;
}

void Sdl3VulkanBackend::destroySurface(AppContext &) {
  waitForDeviceIdle();
  cleanupSwapchain();
  _swapchainReady = false;
}

void Sdl3VulkanBackend::destroyWindow(AppContext &context) {
  destroySurface(context);
  cleanupPersistentResources();
  cleanupFrames();

  if (_commandPool != VK_NULL_HANDLE && _device != VK_NULL_HANDLE) {
    vkDestroyCommandPool(_device, _commandPool, nullptr);
    _commandPool = VK_NULL_HANDLE;
  }

  if (_device != VK_NULL_HANDLE) {
    vkDestroyDevice(_device, nullptr);
    _device = VK_NULL_HANDLE;
  }

  if (_surface != VK_NULL_HANDLE && _instance != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    _surface = VK_NULL_HANDLE;
  }

  if (_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(_instance, nullptr);
    _instance = VK_NULL_HANDLE;
  }

  if (context.window) {
    SDL_DestroyWindow(context.window);
    context.window = nullptr;
  }

  if (_libraryLoaded) {
    SDL_Vulkan_UnloadLibrary();
    _libraryLoaded = false;
  }
}

void Sdl3VulkanBackend::present(AppContext &context) {
  if (!_swapchainReady || _device == VK_NULL_HANDLE || _swapchain == VK_NULL_HANDLE) {
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    return;
  }

  FrameSync &frame = _frames[_currentFrame];
  if (frame.inFlight == VK_NULL_HANDLE || frame.imageAvailable == VK_NULL_HANDLE ||
      frame.renderFinished == VK_NULL_HANDLE ||
      frame.commandBuffer == VK_NULL_HANDLE) {
    SDL_Log("Vulkan frame state is incomplete");
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    return;
  }

  vkWaitForFences(
      _device, 1, &frame.inFlight, VK_TRUE, std::numeric_limits<uint64_t>::max());
  flushDeferredReleases(_currentFrame);

  uint32_t imageIndex = 0;
  VkResult result = vkAcquireNextImageKHR(_device, _swapchain,
      std::numeric_limits<uint64_t>::max(), frame.imageAvailable, VK_NULL_HANDLE,
      &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    recreateSwapchain(context);
    return;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    SDL_Log("vkAcquireNextImageKHR failed: %s", vkResultToString(result));
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    return;
  }

  if (imageIndex >= _imagesInFlight.size()) {
    SDL_Log("Acquired invalid swapchain image index %u", imageIndex);
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    return;
  }

  if (_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
    vkWaitForFences(_device, 1, &_imagesInFlight[imageIndex], VK_TRUE,
        std::numeric_limits<uint64_t>::max());
  }
  _imagesInFlight[imageIndex] = frame.inFlight;

  vkResetFences(_device, 1, &frame.inFlight);
  vkResetCommandBuffer(frame.commandBuffer, 0);

  if (!recordCommandBuffer(frame.commandBuffer, imageIndex)) {
    SDL_Log("Failed to record Vulkan command buffer");
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    return;
  }

  VkSemaphore waitSemaphores[] = {frame.imageAvailable};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore signalSemaphores[] = {frame.renderFinished};

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &frame.commandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  result = vkQueueSubmit(_graphicsQueue, 1, &submitInfo, frame.inFlight);
  if (result != VK_SUCCESS) {
    SDL_Log("vkQueueSubmit failed: %s", vkResultToString(result));
    _queuedWorldMeshes.clear();
    _queuedQuads.clear();
    return;
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &_swapchain;
  presentInfo.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(_presentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    recreateSwapchain(context);
  } else if (result != VK_SUCCESS) {
    SDL_Log("vkQueuePresentKHR failed: %s", vkResultToString(result));
  }

  _currentFrame = (_currentFrame + 1) % kMaxFramesInFlight;
  _queuedWorldMeshes.clear();
  _queuedQuads.clear();
}

auto Sdl3VulkanBackend::createTexture(const TextureData &texture,
    const GraphicsTextureOptions &options, GraphicsTextureHandle &outTexture)
    -> bool {
  outTexture = 0;
  if (_device == VK_NULL_HANDLE) {
    SDL_Log("Vulkan device is not initialized");
    return false;
  }

  TextureResource resource;
  std::string error;
  if (!createTextureResource(texture, options, resource, error)) {
    destroyTextureResource(resource);
    SDL_Log("Failed to create Vulkan texture: %s", error.c_str());
    return false;
  }

  const GraphicsTextureHandle handle = _nextTextureHandle++;
  _textures.emplace(handle, resource);
  outTexture = handle;
  return true;
}

auto Sdl3VulkanBackend::currentTexture() const -> GraphicsTextureHandle {
  return _boundTexture;
}

auto Sdl3VulkanBackend::bindTexture(GraphicsTextureHandle texture) -> bool {
  if (texture == 0) {
    return false;
  }
  if (_textures.find(texture) == _textures.end()) {
    SDL_Log("Unknown Vulkan texture handle %u", texture);
    return false;
  }

  _boundTexture = texture;
  return true;
}

auto Sdl3VulkanBackend::uploadMesh(const GraphicsMeshVertex *vertices,
    uint32_t vertexCount, GraphicsMeshHandle existingMesh,
    GraphicsMeshHandle &outMesh) -> bool {
  outMesh = existingMesh;
  if (_device == VK_NULL_HANDLE || !vertices || vertexCount == 0) {
    return false;
  }

  const VkDeviceSize size =
      (VkDeviceSize)vertexCount * (VkDeviceSize)sizeof(WorldVertex);
  if (size == 0) {
    return false;
  }

  std::vector<WorldVertex> worldVertices((size_t)vertexCount);
  for (uint32_t i = 0; i < vertexCount; ++i) {
    WorldVertex &dst = worldVertices[(size_t)i];
    const GraphicsMeshVertex &src = vertices[i];
    dst.position[0] = src.position[0];
    dst.position[1] = src.position[1];
    dst.position[2] = src.position[2];
    dst.texCoord[0] = src.texCoord[0];
    dst.texCoord[1] = src.texCoord[1];
    dst.tileOrigin[0] = src.tileOrigin[0];
    dst.tileOrigin[1] = src.tileOrigin[1];
    dst.color = src.color;
  }

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
  std::string error;
  if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          stagingBuffer, stagingBufferMemory, error)) {
    SDL_Log("Failed to create Vulkan mesh staging buffer: %s", error.c_str());
    return false;
  }

  void *mapped = nullptr;
  VkResult result =
      vkMapMemory(_device, stagingBufferMemory, 0, size, 0, &mapped);
  if (result != VK_SUCCESS) {
    vkDestroyBuffer(_device, stagingBuffer, nullptr);
    vkFreeMemory(_device, stagingBufferMemory, nullptr);
    SDL_Log(
        "Failed to map Vulkan mesh staging buffer: %s", vkResultToString(result));
    return false;
  }
  std::memcpy(mapped, worldVertices.data(), (size_t)size);
  vkUnmapMemory(_device, stagingBufferMemory);

  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
  if (!createBuffer(size,
          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, bufferMemory, error) ||
      !copyBuffer(stagingBuffer, buffer, size, error)) {
    if (buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(_device, buffer, nullptr);
    }
    if (bufferMemory != VK_NULL_HANDLE) {
      vkFreeMemory(_device, bufferMemory, nullptr);
    }
    vkDestroyBuffer(_device, stagingBuffer, nullptr);
    vkFreeMemory(_device, stagingBufferMemory, nullptr);
    SDL_Log("Failed to upload Vulkan mesh buffer: %s", error.c_str());
    return false;
  }

  vkDestroyBuffer(_device, stagingBuffer, nullptr);
  vkFreeMemory(_device, stagingBufferMemory, nullptr);

  MeshResource resource;
  resource.buffer = buffer;
  resource.bufferMemory = bufferMemory;
  resource.vertexCount = vertexCount;
  resource.minPosition[0] = worldVertices[0].position[0];
  resource.minPosition[1] = worldVertices[0].position[1];
  resource.minPosition[2] = worldVertices[0].position[2];
  resource.maxPosition[0] = worldVertices[0].position[0];
  resource.maxPosition[1] = worldVertices[0].position[1];
  resource.maxPosition[2] = worldVertices[0].position[2];
  for (uint32_t i = 1; i < vertexCount; ++i) {
    for (int axis = 0; axis < 3; ++axis) {
      resource.minPosition[axis] = std::min(
          resource.minPosition[axis], worldVertices[(size_t)i].position[axis]);
      resource.maxPosition[axis] = std::max(
          resource.maxPosition[axis], worldVertices[(size_t)i].position[axis]);
    }
  }

  if (existingMesh != 0) {
    std::map<GraphicsMeshHandle, MeshResource>::iterator existing =
        _meshes.find(existingMesh);
    if (existing != _meshes.end()) {
      queueMeshRelease(existing->second);
      existing->second = resource;
      outMesh = existingMesh;
      return true;
    }
  }

  const GraphicsMeshHandle handle = _nextMeshHandle++;
  _meshes.emplace(handle, resource);
  outMesh = handle;
  return true;
}

auto Sdl3VulkanBackend::updateTexture(GraphicsTextureHandle texture,
    const GraphicsTextureUpdate &update) -> bool {
  std::map<GraphicsTextureHandle, TextureResource>::iterator it =
      _textures.find(texture);
  if (it == _textures.end() || !update.data || update.width <= 0 ||
      update.height <= 0) {
    return false;
  }
  if (!textureFormatSupported(update.format) ||
      update.format != it->second.format) {
    SDL_Log("Unsupported Vulkan texture update format %d", (int)update.format);
    return false;
  }

  const VkDeviceSize imageSize =
      textureByteCount((uint32_t)update.width, (uint32_t)update.height,
          update.format);
  if (imageSize == 0) {
    return false;
  }

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
  std::string error;
  if (!createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          stagingBuffer, stagingBufferMemory, error)) {
    SDL_Log("Failed to create Vulkan staging buffer: %s", error.c_str());
    return false;
  }

  void *mapped = nullptr;
  VkResult result =
      vkMapMemory(_device, stagingBufferMemory, 0, imageSize, 0, &mapped);
  if (result != VK_SUCCESS) {
    vkDestroyBuffer(_device, stagingBuffer, nullptr);
    vkFreeMemory(_device, stagingBufferMemory, nullptr);
    SDL_Log("Failed to map Vulkan staging buffer: %s",
        vkResultToString(result));
    return false;
  }
  std::memcpy(mapped, update.data, (size_t)imageSize);
  vkUnmapMemory(_device, stagingBufferMemory);

  waitForDeviceIdle();
  const VkFormat format = vkTextureFormat(it->second.format);
  const bool ok =
      transitionImageLayout(it->second.image, format,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, error) &&
      copyBufferToImage(stagingBuffer, it->second.image,
          (uint32_t)update.width, (uint32_t)update.height, (uint32_t)update.x,
          (uint32_t)update.y, error) &&
      transitionImageLayout(it->second.image, format,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error);

  vkDestroyBuffer(_device, stagingBuffer, nullptr);
  vkFreeMemory(_device, stagingBufferMemory, nullptr);
  if (!ok) {
    SDL_Log("Failed to update Vulkan texture: %s", error.c_str());
  }
  return ok;
}

auto Sdl3VulkanBackend::drawQuad(const GraphicsQuad &quad) -> bool {
  GraphicsTextureHandle texture = _boundTexture;
  if (quad.textured) {
    if (texture == 0 || _textures.find(texture) == _textures.end()) {
      return false;
    }
  } else {
    std::string error;
    if (!ensureSolidWhiteTexture(error)) {
      SDL_Log("Failed to create fallback Vulkan quad texture: %s",
          error.c_str());
      return false;
    }
    texture = _solidWhiteTexture;
  }

  if (_queuedQuads.size() >= kMaxQueuedQuads || quad.width == 0.0f ||
      quad.height == 0.0f) {
    return false;
  }

  QueuedQuad queuedQuad;
  queuedQuad.texture = texture;
  queuedQuad.quad = quad;
  _queuedQuads.push_back(queuedQuad);
  return true;
}

auto Sdl3VulkanBackend::drawWorldMesh(const GraphicsWorldMeshDraw &draw)
    -> bool {
  const bool missingMesh =
      draw.mesh == 0 || _meshes.find(draw.mesh) == _meshes.end();
  if (missingMesh) {
    return false;
  }

  GraphicsWorldMeshDraw queued = draw;
  if (queued.texture == 0) {
    std::string error;
    if (!ensureSolidWhiteTexture(error)) {
      SDL_Log("Failed to create fallback Vulkan mesh texture: %s",
          error.c_str());
      return false;
    }
    queued.texture = _solidWhiteTexture;
  }
  if (_textures.find(queued.texture) == _textures.end()) {
    return false;
  }
  correctProjectionForVulkan(queued.projection);
  _queuedWorldMeshes.push_back(queued);
  return true;
}

void Sdl3VulkanBackend::destroyTexture(GraphicsTextureHandle texture) {
  std::map<GraphicsTextureHandle, TextureResource>::iterator it =
      _textures.find(texture);
  if (it == _textures.end()) {
    return;
  }

  queueTextureRelease(it->second);
  _textures.erase(it);

  if (_boundTexture == texture) {
    _boundTexture = 0;
  }
  if (_solidWhiteTexture == texture) {
    _solidWhiteTexture = 0;
  }
  if (_bootstrapTexture == texture) {
    _bootstrapTexture = 0;
  }
}

void Sdl3VulkanBackend::destroyMesh(GraphicsMeshHandle mesh) {
  std::map<GraphicsMeshHandle, MeshResource>::iterator it = _meshes.find(mesh);
  if (it == _meshes.end()) {
    return;
  }

  queueMeshRelease(it->second);
  _meshes.erase(it);
}

auto Sdl3VulkanBackend::uploadTexture(
    const TextureData &texture, std::string &error) -> bool {
  clearTexture();

  GraphicsTextureOptions options;
  GraphicsTextureHandle handle = 0;
  if (!createTexture(texture, options, handle)) {
    error = "Failed to create bootstrap Vulkan texture";
    return false;
  }
  if (!bindTexture(handle)) {
    destroyTexture(handle);
    error = "Failed to bind bootstrap Vulkan texture";
    return false;
  }

  _bootstrapTexture = handle;
  return true;
}

void Sdl3VulkanBackend::clearTexture() {
  if (_bootstrapTexture != 0) {
    destroyTexture(_bootstrapTexture);
    _bootstrapTexture = 0;
  }
}

auto Sdl3VulkanBackend::createInstance(std::string &error) -> bool {
  Uint32 extensionCount = 0;
  const char *const *extensionNames =
      SDL_Vulkan_GetInstanceExtensions(&extensionCount);
  if (!extensionNames) {
    error = SDL_GetError();
    return false;
  }

  std::vector<const char *> extensions(
      extensionNames, extensionNames + extensionCount);

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "mcpe";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "mcpe";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = (uint32_t)extensions.size();
  createInfo.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
  if (validationLayersAvailable()) {
    createInfo.enabledLayerCount = (uint32_t)kValidationLayers.size();
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
  }
#endif

  const VkResult result = vkCreateInstance(&createInfo, nullptr, &_instance);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan instance", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createSurface(SDL_Window *window, std::string &error)
    -> bool {
  if (!SDL_Vulkan_CreateSurface(window, _instance, nullptr, &_surface)) {
    error = SDL_GetError();
    return false;
  }
  return true;
}

auto Sdl3VulkanBackend::pickPhysicalDevice(std::string &error) -> bool {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    error = "No Vulkan physical devices available";
    return false;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
  bool missingDynamicTextureArrayIndexing = false;
  for (VkPhysicalDevice device : devices) {
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    const QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.complete() || !checkDeviceExtensionSupport(device)) {
      continue;
    }
    if (supportedFeatures.shaderSampledImageArrayDynamicIndexing != VK_TRUE) {
      missingDynamicTextureArrayIndexing = true;
      continue;
    }

    const SwapchainSupportDetails swapchainSupport =
        querySwapchainSupport(device);
    if (swapchainSupport.formats.empty() ||
        swapchainSupport.presentModes.empty()) {
      continue;
    }

    _physicalDevice = device;
    return true;
  }

  if (missingDynamicTextureArrayIndexing) {
    error =
        "No Vulkan device supports dynamic sampled texture-array indexing";
    return false;
  }
  error = "No Vulkan device supports graphics, presentation, and swapchains";
  return false;
}

auto Sdl3VulkanBackend::createLogicalDevice(std::string &error) -> bool {
  const QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);
  const std::set<uint32_t> uniqueQueueFamilies = {
      indices.graphicsFamily.value(), indices.presentFamily.value()};

  float queuePriority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures supportedFeatures{};
  vkGetPhysicalDeviceFeatures(_physicalDevice, &supportedFeatures);
  if (supportedFeatures.shaderSampledImageArrayDynamicIndexing != VK_TRUE) {
    error = "Vulkan device does not support dynamic sampled texture arrays";
    return false;
  }

  VkPhysicalDeviceFeatures deviceFeatures{};
  deviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;

  const std::vector<const char *> deviceExtensions = createDeviceExtensions();

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();
  createInfo.pNext = nullptr;

#ifndef NDEBUG
  if (validationLayersAvailable()) {
    createInfo.enabledLayerCount = (uint32_t)kValidationLayers.size();
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
  }
#endif

  const VkResult result =
      vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan logical device", result);
    return false;
  }

  vkGetDeviceQueue(_device, indices.graphicsFamily.value(), 0, &_graphicsQueue);
  vkGetDeviceQueue(_device, indices.presentFamily.value(), 0, &_presentQueue);
  return true;
}

auto Sdl3VulkanBackend::createCommandPool(std::string &error) -> bool {
  const QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

  const VkResult result =
      vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan command pool", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createSyncObjects(std::string &error) -> bool {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (FrameSync &frame : _frames) {
    VkResult result = vkCreateSemaphore(
        _device, &semaphoreInfo, nullptr, &frame.imageAvailable);
    if (result != VK_SUCCESS) {
      error = makeVkError("Failed to create image-available semaphore", result);
      return false;
    }

    result = vkCreateSemaphore(
        _device, &semaphoreInfo, nullptr, &frame.renderFinished);
    if (result != VK_SUCCESS) {
      error = makeVkError("Failed to create render-finished semaphore", result);
      return false;
    }

    result = vkCreateFence(_device, &fenceInfo, nullptr, &frame.inFlight);
    if (result != VK_SUCCESS) {
      error = makeVkError("Failed to create in-flight fence", result);
      return false;
    }
  }

  return true;
}

auto Sdl3VulkanBackend::createPersistentResources(std::string &error) -> bool {
  if (!createDescriptorSetLayout(error) || !createDescriptorPool(error) ||
      !allocateTextureDescriptorSet(error) || !createVertexBuffer(error)) {
    return false;
  }

  if (!ensureSolidWhiteTexture(error)) {
    return false;
  }
  std::map<GraphicsTextureHandle, TextureResource>::const_iterator whiteIt =
      _textures.find(_solidWhiteTexture);
  if (whiteIt == _textures.end() ||
      whiteIt->second.imageView == VK_NULL_HANDLE ||
      whiteIt->second.sampler == VK_NULL_HANDLE) {
    error = "Failed to initialize fallback Vulkan texture descriptors";
    return false;
  }
  for (uint32_t descriptorIndex = 0; descriptorIndex < kMaxTextureDescriptors;
       ++descriptorIndex) {
    if (!updateTextureDescriptor(descriptorIndex, whiteIt->second.imageView,
            whiteIt->second.sampler, error)) {
      return false;
    }
  }

  _queuedWorldMeshes.reserve(4096);
  _queuedQuads.reserve(kMaxQueuedQuads);

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(WorldPushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  const VkResult result = vkCreatePipelineLayout(
      _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan pipeline layout", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createSwapchain(
    uint32_t width, uint32_t height, std::string &error) -> bool {
  const SwapchainSupportDetails swapchainSupport =
      querySwapchainSupport(_physicalDevice);

  if (swapchainSupport.formats.empty() ||
      swapchainSupport.presentModes.empty()) {
    error = "Vulkan swapchain support is incomplete";
    return false;
  }

  const VkSurfaceFormatKHR surfaceFormat =
      chooseSurfaceFormat(swapchainSupport.formats);
  const VkPresentModeKHR presentMode =
      choosePresentMode(swapchainSupport.presentModes);
  const VkExtent2D extent =
      chooseExtent(swapchainSupport.capabilities, width, height);

  uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
  if (swapchainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapchainSupport.capabilities.maxImageCount) {
    imageCount = swapchainSupport.capabilities.maxImageCount;
  }

  const QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);
  const uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
      indices.presentFamily.value()};

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = _surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  const VkResult result =
      vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan swapchain", result);
    return false;
  }

  vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, nullptr);
  _swapchainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(
      _device, _swapchain, &imageCount, _swapchainImages.data());

  _swapchainImageFormat = surfaceFormat.format;
  _swapchainExtent = extent;
  _imagesInFlight.assign(_swapchainImages.size(), VK_NULL_HANDLE);
  return true;
}

auto Sdl3VulkanBackend::createImageViews(std::string &error) -> bool {
  _swapchainImageViews.resize(_swapchainImages.size());

  for (size_t i = 0; i < _swapchainImages.size(); ++i) {
    if (!createImageView(
            _swapchainImages[i], _swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT,
            _swapchainImageViews[i], error)) {
      return false;
    }
  }

  return true;
}

auto Sdl3VulkanBackend::createDepthResources(std::string &error) -> bool {
  _depthFormat = findDepthFormat();
  if (_depthFormat == VK_FORMAT_UNDEFINED) {
    error = "Failed to find a supported Vulkan depth format";
    return false;
  }

  if (!createImage(_swapchainExtent.width, _swapchainExtent.height, _depthFormat,
          VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _depthImage, _depthImageMemory,
          error)) {
    return false;
  }

  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  if (hasStencilComponent(_depthFormat)) {
    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  if (!createImageView(
          _depthImage, _depthFormat, aspectMask, _depthImageView, error)) {
    return false;
  }

  return transitionImageLayout(_depthImage, _depthFormat,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      error);
}

auto Sdl3VulkanBackend::createRenderPass(std::string &error) -> bool {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = _swapchainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = _depthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  const std::array<VkAttachmentDescription, 2> attachments = {
      colorAttachment, depthAttachment};

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = (uint32_t)attachments.size();
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  const VkResult result =
      vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderPass);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan render pass", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createGraphicsPipeline(std::string &error) -> bool {
  destroyGraphicsPipeline();
  if (!createGraphicsPipeline(
          GraphicsBlendMode::Alpha, _graphicsPipelineAlpha, error) ||
      !createGraphicsPipeline(GraphicsBlendMode::DstColorSrcColor,
          _graphicsPipelineDstColorSrcColor, error) ||
      !createGraphicsPipeline(GraphicsBlendMode::ZeroOneMinusSrcColor,
          _graphicsPipelineZeroOneMinusSrcColor, error) ||
      !createGraphicsPipeline(GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor,
          _graphicsPipelineOneMinusDstColorOneMinusSrcColor, error)) {
    return false;
  }

  constexpr std::array<GraphicsWorldPass, 2> kNonBlendWorldPasses = {
      GraphicsWorldPass::Opaque, GraphicsWorldPass::AlphaTest};
  constexpr std::array<GraphicsMeshPrimitive, 3> kWorldPrimitives = {
      GraphicsMeshPrimitive::TriangleList, GraphicsMeshPrimitive::LineList,
      GraphicsMeshPrimitive::LineStrip};
  constexpr std::array<GraphicsBlendMode, 4> kBlendModes = {
      GraphicsBlendMode::Alpha, GraphicsBlendMode::DstColorSrcColor,
      GraphicsBlendMode::ZeroOneMinusSrcColor,
      GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor};

  for (bool depthTest : {true, false}) {
    for (GraphicsWorldPass pass : kNonBlendWorldPasses) {
      for (GraphicsMeshPrimitive primitive : kWorldPrimitives) {
        if (!createWorldPipeline(pass, depthTest, primitive,
                GraphicsBlendMode::Alpha,
                worldPipeline(pass, depthTest, primitive,
                    GraphicsBlendMode::Alpha),
                error)) {
          return false;
        }
      }
    }

    for (GraphicsMeshPrimitive primitive : kWorldPrimitives) {
      for (GraphicsBlendMode blendMode : kBlendModes) {
        if (!createWorldPipeline(GraphicsWorldPass::Blend, depthTest, primitive,
                blendMode,
                worldPipeline(GraphicsWorldPass::Blend, depthTest, primitive,
                    blendMode),
                error)) {
          return false;
        }
      }
    }
  }

  return true;
}

auto Sdl3VulkanBackend::createGraphicsPipeline(
    GraphicsBlendMode blendMode, VkPipeline &pipeline, std::string &error)
    -> bool {
  std::vector<char> vertShaderCode;
  std::vector<char> fragShaderCode;
  std::string shaderDir;
  if (!findShaderDirectory(shaderDir, error)) {
    return false;
  }
  if (!readFile(shaderDir + "/textured_quad.vert.spv", vertShaderCode, error) ||
      !readFile(shaderDir + "/textured_quad.frag.spv", fragShaderCode, error)) {
    return false;
  }

  VkShaderModule vertShaderModule = VK_NULL_HANDLE;
  VkShaderModule fragShaderModule = VK_NULL_HANDLE;
  if (!createShaderModule(vertShaderCode, vertShaderModule, error) ||
      !createShaderModule(fragShaderCode, fragShaderModule, error)) {
    if (vertShaderModule != VK_NULL_HANDLE) {
      vkDestroyShaderModule(_device, vertShaderModule, nullptr);
    }
    if (fragShaderModule != VK_NULL_HANDLE) {
      vkDestroyShaderModule(_device, fragShaderModule, nullptr);
    }
    return false;
  }

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {
      vertShaderStageInfo, fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(QuadVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(QuadVertex, position);
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(QuadVertex, texCoord);
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(QuadVertex, color);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount =
      (uint32_t)attributeDescriptions.size();
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_FALSE;
  depthStencil.depthWriteEnable = VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  configureBlendState(blendMode, colorBlendAttachment);

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  std::array<VkDynamicState, 2> dynamicStates = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
  dynamicState.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = _pipelineLayout;
  pipelineInfo.renderPass = _renderPass;
  pipelineInfo.subpass = 0;

  const VkResult result = vkCreateGraphicsPipelines(
      _device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
  vkDestroyShaderModule(_device, fragShaderModule, nullptr);
  vkDestroyShaderModule(_device, vertShaderModule, nullptr);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan graphics pipeline", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createWorldPipeline(GraphicsWorldPass pass,
    bool depthTest, GraphicsMeshPrimitive primitive, GraphicsBlendMode blendMode,
    VkPipeline &pipeline, std::string &error) -> bool {
  std::vector<char> vertShaderCode;
  std::vector<char> fragShaderCode;
  std::string shaderDir;
  if (!findShaderDirectory(shaderDir, error)) {
    return false;
  }
  const std::string fragName = pass == GraphicsWorldPass::AlphaTest
      ? "/terrain_mesh_alpha_test.frag.spv"
      : "/terrain_mesh.frag.spv";
  if (!readFile(shaderDir + "/terrain_mesh.vert.spv", vertShaderCode, error) ||
      !readFile(shaderDir + fragName, fragShaderCode, error)) {
    return false;
  }

  VkShaderModule vertShaderModule = VK_NULL_HANDLE;
  VkShaderModule fragShaderModule = VK_NULL_HANDLE;
  if (!createShaderModule(vertShaderCode, vertShaderModule, error) ||
      !createShaderModule(fragShaderCode, fragShaderModule, error)) {
    if (vertShaderModule != VK_NULL_HANDLE) {
      vkDestroyShaderModule(_device, vertShaderModule, nullptr);
    }
    if (fragShaderModule != VK_NULL_HANDLE) {
      vkDestroyShaderModule(_device, fragShaderModule, nullptr);
    }
    return false;
  }

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {
      vertShaderStageInfo, fragShaderStageInfo};

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(WorldVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(WorldVertex, position);
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(WorldVertex, texCoord);
  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(WorldVertex, tileOrigin);
  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R8G8B8A8_UNORM;
  attributeDescriptions[3].offset = offsetof(WorldVertex, color);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount =
      (uint32_t)attributeDescriptions.size();
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  switch (primitive) {
  case GraphicsMeshPrimitive::LineList:
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    break;
  case GraphicsMeshPrimitive::LineStrip:
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    break;
  case GraphicsMeshPrimitive::TriangleList:
  default:
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    break;
  }
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = depthTest && pass != GraphicsWorldPass::Blend
      ? VK_TRUE
      : VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  if (pass == GraphicsWorldPass::Blend) {
    configureBlendState(blendMode, colorBlendAttachment);
  } else {
    colorBlendAttachment.blendEnable = VK_FALSE;
  }

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  std::array<VkDynamicState, 2> dynamicStates = {
      VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
  dynamicState.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = _pipelineLayout;
  pipelineInfo.renderPass = _renderPass;
  pipelineInfo.subpass = 0;

  const VkResult result = vkCreateGraphicsPipelines(
      _device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
  vkDestroyShaderModule(_device, fragShaderModule, nullptr);
  vkDestroyShaderModule(_device, vertShaderModule, nullptr);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan terrain pipeline", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::worldPassIndex(GraphicsWorldPass pass) -> size_t {
  switch (pass) {
  case GraphicsWorldPass::AlphaTest:
    return 1;
  case GraphicsWorldPass::Blend:
    return 2;
  case GraphicsWorldPass::Opaque:
  default:
    return 0;
  }
}

auto Sdl3VulkanBackend::worldPrimitiveIndex(
    GraphicsMeshPrimitive primitive) -> size_t {
  switch (primitive) {
  case GraphicsMeshPrimitive::LineList:
    return 1;
  case GraphicsMeshPrimitive::LineStrip:
    return 2;
  case GraphicsMeshPrimitive::TriangleList:
  default:
    return 0;
  }
}

auto Sdl3VulkanBackend::worldBlendModeIndex(
    GraphicsBlendMode blendMode) -> size_t {
  switch (blendMode) {
  case GraphicsBlendMode::DstColorSrcColor:
    return 1;
  case GraphicsBlendMode::ZeroOneMinusSrcColor:
    return 2;
  case GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor:
    return 3;
  case GraphicsBlendMode::Alpha:
  default:
    return 0;
  }
}

auto Sdl3VulkanBackend::worldPipeline(GraphicsWorldPass pass, bool depthTest,
    GraphicsMeshPrimitive primitive, GraphicsBlendMode blendMode)
    -> VkPipeline & {
  const GraphicsBlendMode resolvedBlendMode = pass == GraphicsWorldPass::Blend
      ? blendMode
      : GraphicsBlendMode::Alpha;
  const size_t depthIndex = depthTest ? 0u : 1u;
  const size_t index =
      (((worldPassIndex(pass) * 2u) + depthIndex) * kWorldPrimitiveCount +
          worldPrimitiveIndex(primitive)) *
          kWorldBlendModeCount +
      worldBlendModeIndex(resolvedBlendMode);
  return _worldPipelines[index];
}

auto Sdl3VulkanBackend::worldPipeline(GraphicsWorldPass pass, bool depthTest,
    GraphicsMeshPrimitive primitive, GraphicsBlendMode blendMode) const
    -> VkPipeline {
  const GraphicsBlendMode resolvedBlendMode = pass == GraphicsWorldPass::Blend
      ? blendMode
      : GraphicsBlendMode::Alpha;
  const size_t depthIndex = depthTest ? 0u : 1u;
  const size_t index =
      (((worldPassIndex(pass) * 2u) + depthIndex) * kWorldPrimitiveCount +
          worldPrimitiveIndex(primitive)) *
          kWorldBlendModeCount +
      worldBlendModeIndex(resolvedBlendMode);
  return _worldPipelines[index];
}

auto Sdl3VulkanBackend::createFramebuffers(std::string &error) -> bool {
  _swapchainFramebuffers.resize(_swapchainImageViews.size());

  for (size_t i = 0; i < _swapchainImageViews.size(); ++i) {
    VkImageView attachments[] = {_swapchainImageViews[i], _depthImageView};

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = _renderPass;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = _swapchainExtent.width;
    framebufferInfo.height = _swapchainExtent.height;
    framebufferInfo.layers = 1;

    const VkResult result = vkCreateFramebuffer(
        _device, &framebufferInfo, nullptr, &_swapchainFramebuffers[i]);
    if (result != VK_SUCCESS) {
      error = makeVkError("Failed to create Vulkan framebuffer", result);
      return false;
    }
  }

  return true;
}

auto Sdl3VulkanBackend::allocateCommandBuffers(std::string &error) -> bool {
  cleanupCommandBuffers();

  std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers{};
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = _commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

  const VkResult result =
      vkAllocateCommandBuffers(_device, &allocInfo, commandBuffers.data());
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to allocate Vulkan command buffers", result);
    return false;
  }

  for (size_t i = 0; i < commandBuffers.size(); ++i) {
    _frames[i].commandBuffer = commandBuffers[i];
  }
  return true;
}

auto Sdl3VulkanBackend::createVertexBuffer(std::string &error) -> bool {
  _vertexBufferSize = sizeof(QuadVertex) * 6 * kMaxQueuedQuads;
  if (_vertexBufferSize == 0) {
    error = "Invalid Vulkan quad vertex buffer size";
    return false;
  }

  const VkDeviceSize size = _vertexBufferSize;
  if (!createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          _vertexBuffer, _vertexBufferMemory, error)) {
    return false;
  }
  return true;
}

auto Sdl3VulkanBackend::createTextureSampler(
    const GraphicsTextureOptions &options, VkSampler &sampler,
    std::string &error) -> bool {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = options.blur ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.minFilter = options.blur ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  samplerInfo.addressModeU =
      options.clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                    : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV =
      options.clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                    : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW =
      options.clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                    : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.mipmapMode =
      options.mipmap ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                     : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.maxLod = 0.0f;

  const VkResult result = vkCreateSampler(_device, &samplerInfo, nullptr, &sampler);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan sampler", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createDescriptorSetLayout(std::string &error) -> bool {
  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 0;
  samplerLayoutBinding.descriptorCount = kMaxTextureDescriptors;
  samplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &samplerLayoutBinding;

  const VkResult result = vkCreateDescriptorSetLayout(
      _device, &layoutInfo, nullptr, &_descriptorSetLayout);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan descriptor set layout", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createDescriptorPool(std::string &error) -> bool {
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = kMaxTextureDescriptors;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;

  const VkResult result =
      vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan descriptor pool", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::allocateTextureDescriptorSet(std::string &error) -> bool {
  _freeTextureDescriptorIndices.clear();
  _nextTextureDescriptorIndex = 0;

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = _descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &_descriptorSetLayout;

  const VkResult result =
      vkAllocateDescriptorSets(_device, &allocInfo, &_textureDescriptorSet);
  if (result != VK_SUCCESS) {
    error = makeVkError(
        "Failed to allocate Vulkan texture descriptor set", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::acquireTextureDescriptorIndex(
    uint32_t &descriptorIndex, std::string &error) -> bool {
  if (!_freeTextureDescriptorIndices.empty()) {
    descriptorIndex = _freeTextureDescriptorIndices.back();
    _freeTextureDescriptorIndices.pop_back();
    return true;
  }
  if (_nextTextureDescriptorIndex >= kMaxTextureDescriptors) {
    error = "Exhausted Vulkan texture descriptor slots";
    return false;
  }
  descriptorIndex = _nextTextureDescriptorIndex++;
  return true;
}

void Sdl3VulkanBackend::releaseTextureDescriptorIndex(uint32_t descriptorIndex) {
  if (descriptorIndex != kInvalidTextureDescriptorIndex &&
      descriptorIndex < _nextTextureDescriptorIndex) {
    _freeTextureDescriptorIndices.push_back(descriptorIndex);
  }
}

auto Sdl3VulkanBackend::updateTextureDescriptor(uint32_t descriptorIndex,
    VkImageView imageView, VkSampler sampler, std::string &error) -> bool {
  if (_textureDescriptorSet == VK_NULL_HANDLE || imageView == VK_NULL_HANDLE ||
      sampler == VK_NULL_HANDLE || descriptorIndex >= kMaxTextureDescriptors) {
    error = "Texture descriptor resources are incomplete";
    return false;
  }
  if (_swapchainReady) {
    waitForDeviceIdle();
  }

  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = imageView;
  imageInfo.sampler = sampler;

  VkWriteDescriptorSet descriptorWrite{};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = _textureDescriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = descriptorIndex;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(_device, 1, &descriptorWrite, 0, nullptr);
  return true;
}

auto Sdl3VulkanBackend::createTextureResource(const TextureData &texture,
    const GraphicsTextureOptions &options, TextureResource &resource,
    std::string &error) -> bool {
  if (!texture.data || texture.w <= 0 || texture.h <= 0) {
    error = "TextureData is empty";
    return false;
  }
  if (!textureFormatSupported(texture.format)) {
    error = "Only RGBA8888 textures are currently supported by the Vulkan backend";
    return false;
  }

  const VkFormat format = vkTextureFormat(texture.format);
  const VkDeviceSize imageSize =
      textureByteCount((uint32_t)texture.w, (uint32_t)texture.h, texture.format);
  if (format == VK_FORMAT_UNDEFINED || imageSize == 0) {
    error = "Unsupported Vulkan texture format";
    return false;
  }

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
  if (!createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          stagingBuffer, stagingBufferMemory, error)) {
    return false;
  }

  void *mapped = nullptr;
  VkResult result =
      vkMapMemory(_device, stagingBufferMemory, 0, imageSize, 0, &mapped);
  if (result != VK_SUCCESS) {
    vkDestroyBuffer(_device, stagingBuffer, nullptr);
    vkFreeMemory(_device, stagingBufferMemory, nullptr);
    error = makeVkError("Failed to map Vulkan staging buffer", result);
    return false;
  }
  std::memcpy(mapped, texture.data, (size_t)imageSize);
  vkUnmapMemory(_device, stagingBufferMemory);

  const bool ok =
      createImage((uint32_t)texture.w, (uint32_t)texture.h, format,
          VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, resource.image,
          resource.imageMemory, error) &&
      transitionImageLayout(resource.image, format, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, error) &&
      copyBufferToImage(stagingBuffer, resource.image, (uint32_t)texture.w,
          (uint32_t)texture.h, 0, 0, error) &&
      transitionImageLayout(resource.image, format,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error) &&
      createImageView(resource.image, format, VK_IMAGE_ASPECT_COLOR_BIT,
          resource.imageView, error) &&
      createTextureSampler(options, resource.sampler, error);

  vkDestroyBuffer(_device, stagingBuffer, nullptr);
  vkFreeMemory(_device, stagingBufferMemory, nullptr);
  if (!ok) {
    return false;
  }

  if (!acquireTextureDescriptorIndex(resource.descriptorIndex, error) ||
      !updateTextureDescriptor(
          resource.descriptorIndex, resource.imageView, resource.sampler, error)) {
    releaseTextureDescriptorIndex(resource.descriptorIndex);
    resource.descriptorIndex = kInvalidTextureDescriptorIndex;
    return false;
  }

  resource.width = (uint32_t)texture.w;
  resource.height = (uint32_t)texture.h;
  resource.format = texture.format;
  resource.transparent = texture.transparent;
  return true;
}

auto Sdl3VulkanBackend::ensureSolidWhiteTexture(std::string &error) -> bool {
  if (_solidWhiteTexture != 0) {
    return true;
  }

  unsigned char whitePixel[4] = {255, 255, 255, 255};
  TextureData texture;
  texture.w = 1;
  texture.h = 1;
  texture.data = whitePixel;
  texture.numBytes = 4;
  texture.transparent = true;
  texture.memoryHandledExternally = true;
  texture.format = TEXF_UNCOMPRESSED_8888;

  GraphicsTextureOptions options;
  if (!createTexture(texture, options, _solidWhiteTexture)) {
    error = "Failed to allocate internal white texture";
    return false;
  }
  return true;
}

auto Sdl3VulkanBackend::createShaderModule(
    const std::vector<char> &code, VkShaderModule &shaderModule,
    std::string &error) -> bool {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  const VkResult result =
      vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan shader module", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::readFile(
    const std::string &path, std::vector<char> &out, std::string &error) const
    -> bool {
  std::ifstream file(path.c_str(), std::ios::ate | std::ios::binary);
  if (!file) {
    error = "Failed to open shader file: " + path;
    return false;
  }

  const std::streamsize size = file.tellg();
  if (size <= 0) {
    error = "Shader file is empty: " + path;
    return false;
  }

  out.resize((size_t)size);
  file.seekg(0);
  if (!file.read(out.data(), size)) {
    error = "Failed to read shader file: " + path;
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::findSupportedFormat(
    const std::vector<VkFormat> &candidates, VkImageTiling tiling,
    VkFormatFeatureFlags features) const -> VkFormat {
  for (VkFormat format : candidates) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &properties);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (properties.linearTilingFeatures & features) == features) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL &&
        (properties.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  return VK_FORMAT_UNDEFINED;
}

auto Sdl3VulkanBackend::findDepthFormat() const -> VkFormat {
  return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                 VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

auto Sdl3VulkanBackend::findMemoryType(uint32_t typeFilter,
    VkMemoryPropertyFlags properties, uint32_t &memoryTypeIndex) const -> bool {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memoryProperties);

  for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    if ((typeFilter & (1u << i)) &&
        (memoryProperties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      memoryTypeIndex = i;
      return true;
    }
  }

  return false;
}

auto Sdl3VulkanBackend::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, VkBuffer &buffer,
    VkDeviceMemory &bufferMemory, std::string &error) -> bool {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateBuffer(_device, &bufferInfo, nullptr, &buffer);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan buffer", result);
    return false;
  }

  VkMemoryRequirements memoryRequirements{};
  vkGetBufferMemoryRequirements(_device, buffer, &memoryRequirements);

  uint32_t memoryTypeIndex = 0;
  if (!findMemoryType(
          memoryRequirements.memoryTypeBits, properties, memoryTypeIndex)) {
    vkDestroyBuffer(_device, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    error = "Failed to find Vulkan buffer memory type";
    return false;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memoryRequirements.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  result = vkAllocateMemory(_device, &allocInfo, nullptr, &bufferMemory);
  if (result != VK_SUCCESS) {
    vkDestroyBuffer(_device, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    error = makeVkError("Failed to allocate Vulkan buffer memory", result);
    return false;
  }

  result = vkBindBufferMemory(_device, buffer, bufferMemory, 0);
  if (result != VK_SUCCESS) {
    vkFreeMemory(_device, bufferMemory, nullptr);
    bufferMemory = VK_NULL_HANDLE;
    vkDestroyBuffer(_device, buffer, nullptr);
    buffer = VK_NULL_HANDLE;
    error = makeVkError("Failed to bind Vulkan buffer memory", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::createImage(uint32_t width, uint32_t height,
    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage &image,
    VkDeviceMemory &imageMemory, std::string &error) -> bool {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkResult result = vkCreateImage(_device, &imageInfo, nullptr, &image);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan image", result);
    return false;
  }

  VkMemoryRequirements memoryRequirements{};
  vkGetImageMemoryRequirements(_device, image, &memoryRequirements);

  uint32_t memoryTypeIndex = 0;
  if (!findMemoryType(
          memoryRequirements.memoryTypeBits, properties, memoryTypeIndex)) {
    vkDestroyImage(_device, image, nullptr);
    image = VK_NULL_HANDLE;
    error = "Failed to find Vulkan image memory type";
    return false;
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memoryRequirements.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  result = vkAllocateMemory(_device, &allocInfo, nullptr, &imageMemory);
  if (result != VK_SUCCESS) {
    vkDestroyImage(_device, image, nullptr);
    image = VK_NULL_HANDLE;
    error = makeVkError("Failed to allocate Vulkan image memory", result);
    return false;
  }

  result = vkBindImageMemory(_device, image, imageMemory, 0);
  if (result != VK_SUCCESS) {
    vkFreeMemory(_device, imageMemory, nullptr);
    imageMemory = VK_NULL_HANDLE;
    vkDestroyImage(_device, image, nullptr);
    image = VK_NULL_HANDLE;
    error = makeVkError("Failed to bind Vulkan image memory", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::copyBuffer(
    VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, std::string &error)
    -> bool {
  if (size == 0) {
    error = "Cannot copy an empty Vulkan buffer";
    return false;
  }

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  if (!beginSingleUseCommands(commandBuffer, error)) {
    return false;
  }

  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  return endSingleUseCommands(commandBuffer, error);
}

auto Sdl3VulkanBackend::createImageView(VkImage image, VkFormat format,
    VkImageAspectFlags aspectMask, VkImageView &imageView, std::string &error)
    -> bool {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  const VkResult result =
      vkCreateImageView(_device, &viewInfo, nullptr, &imageView);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to create Vulkan image view", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::beginSingleUseCommands(
    VkCommandBuffer &commandBuffer, std::string &error) -> bool {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = _commandPool;
  allocInfo.commandBufferCount = 1;

  VkResult result = vkAllocateCommandBuffers(_device, &allocInfo, &commandBuffer);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to allocate single-use command buffer", result);
    return false;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to begin single-use command buffer", result);
    return false;
  }

  return true;
}

auto Sdl3VulkanBackend::endSingleUseCommands(
    VkCommandBuffer commandBuffer, std::string &error) -> bool {
  VkResult result = vkEndCommandBuffer(commandBuffer);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to end single-use command buffer", result);
    return false;
  }

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  result = vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to submit single-use command buffer", result);
    return false;
  }

  result = vkQueueWaitIdle(_graphicsQueue);
  if (result != VK_SUCCESS) {
    error = makeVkError("Failed to wait for Vulkan graphics queue", result);
    return false;
  }

  vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
  return true;
}

auto Sdl3VulkanBackend::transitionImageLayout(VkImage image, VkFormat format,
    VkImageLayout oldLayout, VkImageLayout newLayout, std::string &error)
    -> bool {
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  if (!beginSingleUseCommands(commandBuffer, error)) {
    return false;
  }

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask =
      newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
      ? VK_IMAGE_ASPECT_DEPTH_BIT
      : VK_IMAGE_ASPECT_COLOR_BIT;
  if (barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT &&
      hasStencilComponent(format)) {
    barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (
      oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
      newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (
      oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else {
    error = "Unsupported Vulkan image layout transition";
    vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
    return false;
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
      nullptr, 0, nullptr, 1, &barrier);

  return endSingleUseCommands(commandBuffer, error);
}

auto Sdl3VulkanBackend::copyBufferToImage(VkBuffer buffer, VkImage image,
    uint32_t width, uint32_t height, uint32_t x, uint32_t y,
    std::string &error) -> bool {
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  if (!beginSingleUseCommands(commandBuffer, error)) {
    return false;
  }

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {(int32_t)x, (int32_t)y, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  return endSingleUseCommands(commandBuffer, error);
}

void Sdl3VulkanBackend::destroyTextureResource(TextureResource &resource) {
  if (_device == VK_NULL_HANDLE) {
    return;
  }

  if (resource.descriptorIndex != kInvalidTextureDescriptorIndex) {
    if (_solidWhiteTexture != 0) {
      std::map<GraphicsTextureHandle, TextureResource>::const_iterator whiteIt =
          _textures.find(_solidWhiteTexture);
      if (whiteIt != _textures.end() &&
          whiteIt->second.descriptorIndex != resource.descriptorIndex &&
          whiteIt->second.imageView != VK_NULL_HANDLE &&
          whiteIt->second.sampler != VK_NULL_HANDLE) {
        std::string ignoredError;
        updateTextureDescriptor(resource.descriptorIndex, whiteIt->second.imageView,
            whiteIt->second.sampler, ignoredError);
      }
    }
    releaseTextureDescriptorIndex(resource.descriptorIndex);
    resource.descriptorIndex = kInvalidTextureDescriptorIndex;
  }
  if (resource.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(_device, resource.sampler, nullptr);
    resource.sampler = VK_NULL_HANDLE;
  }
  if (resource.imageView != VK_NULL_HANDLE) {
    vkDestroyImageView(_device, resource.imageView, nullptr);
    resource.imageView = VK_NULL_HANDLE;
  }
  if (resource.image != VK_NULL_HANDLE) {
    vkDestroyImage(_device, resource.image, nullptr);
    resource.image = VK_NULL_HANDLE;
  }
  if (resource.imageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(_device, resource.imageMemory, nullptr);
    resource.imageMemory = VK_NULL_HANDLE;
  }
}

void Sdl3VulkanBackend::destroyMeshResource(MeshResource &resource) {
  if (_device == VK_NULL_HANDLE) {
    return;
  }

  if (resource.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(_device, resource.buffer, nullptr);
    resource.buffer = VK_NULL_HANDLE;
  }
  if (resource.bufferMemory != VK_NULL_HANDLE) {
    vkFreeMemory(_device, resource.bufferMemory, nullptr);
    resource.bufferMemory = VK_NULL_HANDLE;
  }
  resource.vertexCount = 0;
}

void Sdl3VulkanBackend::queueTextureRelease(TextureResource &resource) {
  if (_device == VK_NULL_HANDLE) {
    destroyTextureResource(resource);
    return;
  }

  _deferredReleases[_currentFrame].textures.push_back(resource);
  resource = TextureResource{};
}

void Sdl3VulkanBackend::queueMeshRelease(MeshResource &resource) {
  if (_device == VK_NULL_HANDLE) {
    destroyMeshResource(resource);
    return;
  }

  _deferredReleases[_currentFrame].meshes.push_back(resource);
  resource = MeshResource{};
}

void Sdl3VulkanBackend::flushDeferredReleases(size_t frameIndex) {
  if (_device == VK_NULL_HANDLE || frameIndex >= _deferredReleases.size()) {
    return;
  }

  DeferredReleaseBucket &bucket = _deferredReleases[frameIndex];
  for (TextureResource &resource : bucket.textures) {
    destroyTextureResource(resource);
  }
  bucket.textures.clear();

  for (MeshResource &resource : bucket.meshes) {
    destroyMeshResource(resource);
  }
  bucket.meshes.clear();
}

void Sdl3VulkanBackend::clearDeferredReleases() {
  for (size_t i = 0; i < _deferredReleases.size(); ++i) {
    flushDeferredReleases(i);
  }
}

void Sdl3VulkanBackend::clearTextures() {
  clearDeferredReleases();
  for (auto &entry : _textures) {
    destroyTextureResource(entry.second);
  }
  _textures.clear();
  _queuedQuads.clear();
  _boundTexture = 0;
  _solidWhiteTexture = 0;
  _bootstrapTexture = 0;
  _nextTextureHandle = 1;
  _freeTextureDescriptorIndices.clear();
  _nextTextureDescriptorIndex = 0;
}

void Sdl3VulkanBackend::clearMeshes() {
  clearDeferredReleases();
  for (auto &entry : _meshes) {
    destroyMeshResource(entry.second);
  }
  _meshes.clear();
  _queuedWorldMeshes.clear();
  _nextMeshHandle = 1;
}

void Sdl3VulkanBackend::cleanupCommandBuffers() {
  if (_device == VK_NULL_HANDLE || _commandPool == VK_NULL_HANDLE) {
    return;
  }

  for (FrameSync &frame : _frames) {
    if (frame.commandBuffer != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(_device, _commandPool, 1, &frame.commandBuffer);
      frame.commandBuffer = VK_NULL_HANDLE;
    }
  }
}

void Sdl3VulkanBackend::destroyGraphicsPipeline() {
  if (_device == VK_NULL_HANDLE) {
    return;
  }
  if (_graphicsPipelineAlpha != VK_NULL_HANDLE) {
    vkDestroyPipeline(_device, _graphicsPipelineAlpha, nullptr);
    _graphicsPipelineAlpha = VK_NULL_HANDLE;
  }
  if (_graphicsPipelineDstColorSrcColor != VK_NULL_HANDLE) {
    vkDestroyPipeline(_device, _graphicsPipelineDstColorSrcColor, nullptr);
    _graphicsPipelineDstColorSrcColor = VK_NULL_HANDLE;
  }
  if (_graphicsPipelineZeroOneMinusSrcColor != VK_NULL_HANDLE) {
    vkDestroyPipeline(
        _device, _graphicsPipelineZeroOneMinusSrcColor, nullptr);
    _graphicsPipelineZeroOneMinusSrcColor = VK_NULL_HANDLE;
  }
  if (_graphicsPipelineOneMinusDstColorOneMinusSrcColor != VK_NULL_HANDLE) {
    vkDestroyPipeline(
        _device, _graphicsPipelineOneMinusDstColorOneMinusSrcColor, nullptr);
    _graphicsPipelineOneMinusDstColorOneMinusSrcColor = VK_NULL_HANDLE;
  }
  for (VkPipeline &pipeline : _worldPipelines) {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(_device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
  }
}

void Sdl3VulkanBackend::cleanupSwapchain() {
  if (_device == VK_NULL_HANDLE) {
    return;
  }

  cleanupCommandBuffers();

  for (VkFramebuffer framebuffer : _swapchainFramebuffers) {
    if (framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(_device, framebuffer, nullptr);
    }
  }
  _swapchainFramebuffers.clear();

  destroyGraphicsPipeline();

  if (_renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(_device, _renderPass, nullptr);
    _renderPass = VK_NULL_HANDLE;
  }

  if (_depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(_device, _depthImageView, nullptr);
    _depthImageView = VK_NULL_HANDLE;
  }
  if (_depthImage != VK_NULL_HANDLE) {
    vkDestroyImage(_device, _depthImage, nullptr);
    _depthImage = VK_NULL_HANDLE;
  }
  if (_depthImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(_device, _depthImageMemory, nullptr);
    _depthImageMemory = VK_NULL_HANDLE;
  }

  for (VkImageView imageView : _swapchainImageViews) {
    if (imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(_device, imageView, nullptr);
    }
  }
  _swapchainImageViews.clear();
  _swapchainImages.clear();
  _imagesInFlight.clear();

  if (_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    _swapchain = VK_NULL_HANDLE;
  }

  _swapchainImageFormat = VK_FORMAT_UNDEFINED;
  _depthFormat = VK_FORMAT_UNDEFINED;
  _swapchainExtent = {};
}

void Sdl3VulkanBackend::cleanupFrames() {
  if (_device == VK_NULL_HANDLE) {
    return;
  }

  cleanupCommandBuffers();

  for (FrameSync &frame : _frames) {
    if (frame.imageAvailable != VK_NULL_HANDLE) {
      vkDestroySemaphore(_device, frame.imageAvailable, nullptr);
      frame.imageAvailable = VK_NULL_HANDLE;
    }
    if (frame.renderFinished != VK_NULL_HANDLE) {
      vkDestroySemaphore(_device, frame.renderFinished, nullptr);
      frame.renderFinished = VK_NULL_HANDLE;
    }
    if (frame.inFlight != VK_NULL_HANDLE) {
      vkDestroyFence(_device, frame.inFlight, nullptr);
      frame.inFlight = VK_NULL_HANDLE;
    }
  }
}

void Sdl3VulkanBackend::cleanupPersistentResources() {
  if (_device == VK_NULL_HANDLE) {
    return;
  }

  waitForDeviceIdle();
  clearDeferredReleases();
  clearTextures();
  clearMeshes();

  if (_vertexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(_device, _vertexBuffer, nullptr);
    _vertexBuffer = VK_NULL_HANDLE;
  }
  if (_vertexBufferMemory != VK_NULL_HANDLE) {
    vkFreeMemory(_device, _vertexBufferMemory, nullptr);
    _vertexBufferMemory = VK_NULL_HANDLE;
  }

  if (_descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
    _descriptorPool = VK_NULL_HANDLE;
    _textureDescriptorSet = VK_NULL_HANDLE;
  }

  if (_pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
    _pipelineLayout = VK_NULL_HANDLE;
  }

  if (_descriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
    _descriptorSetLayout = VK_NULL_HANDLE;
  }
}

void Sdl3VulkanBackend::waitForDeviceIdle() {
  if (_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(_device);
  }
}

auto Sdl3VulkanBackend::recreateSwapchain(AppContext &context) -> bool {
  int pixelWidth = 0;
  int pixelHeight = 0;
  SDL_GetWindowSizeInPixels(context.window, &pixelWidth, &pixelHeight);
  if (pixelWidth <= 0 || pixelHeight <= 0) {
    return true;
  }

  std::string error;
  const bool ok =
      resetSurface(context, (uint32_t)pixelWidth, (uint32_t)pixelHeight, error);
  if (!ok) {
    SDL_Log("Failed to recreate swapchain: %s", error.c_str());
  }
  return ok;
}

auto Sdl3VulkanBackend::recordCommandBuffer(
    VkCommandBuffer commandBuffer, uint32_t imageIndex) -> bool {
  if (imageIndex >= _swapchainFramebuffers.size()) {
    return false;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    return false;
  }

  VkClearValue clearColor{};
  clearColor.color = {{0.137f, 0.184f, 0.239f, 1.0f}};
  VkClearValue clearDepth{};
  clearDepth.depthStencil = {1.0f, 0};

  size_t validQuadCount = 0;
  if (!_queuedQuads.empty()) {
    for (const QueuedQuad &queuedQuad : _queuedQuads) {
      const GraphicsQuad &quad = queuedQuad.quad;
      const float width =
          quad.canvasWidth > 0.0f ? quad.canvasWidth : (float)_swapchainExtent.width;
      const float height = quad.canvasHeight > 0.0f ? quad.canvasHeight
                                                    : (float)_swapchainExtent.height;
      if (width > 0.0f && height > 0.0f && quad.height > 0.0f) {
        validQuadCount++;
      }
    }
  }

  if (validQuadCount > 0) {
    const VkDeviceSize requiredSize =
        sizeof(QuadVertex) * 6 * validQuadCount;
    if (requiredSize > _vertexBufferSize) {
      SDL_Log("Queued %zu valid quads but only %llu bytes of Vulkan vertex space are available",
          validQuadCount, (unsigned long long)_vertexBufferSize);
      return false;
    }

    std::vector<QuadVertex> vertices;
    vertices.reserve(validQuadCount * 6);
    for (const QueuedQuad &queuedQuad : _queuedQuads) {
      const GraphicsQuad &quad = queuedQuad.quad;
      const float width =
          quad.canvasWidth > 0.0f ? quad.canvasWidth : (float)_swapchainExtent.width;
      const float height = quad.canvasHeight > 0.0f ? quad.canvasHeight
                                                    : (float)_swapchainExtent.height;
      if (width <= 0.0f || height <= 0.0f || quad.height <= 0.0f) {
        continue;
      }
      const float left = quad.x;
      const float right = quad.x + quad.width;
      const float top = quad.y;
      const float bottom = quad.y + quad.height;

      const float x0 = left / width * 2.0f - 1.0f;
      const float x1 = right / width * 2.0f - 1.0f;
      // Vulkan's default viewport maps NDC Y=-1 to the top of the framebuffer.
      const float y0 = top / height * 2.0f - 1.0f;
      const float y1 = bottom / height * 2.0f - 1.0f;

      const QuadVertex quadVertices[6] = {
          {{x0, y1},
              {quad.u0, quad.v1},
              {quad.bottomLeft.r, quad.bottomLeft.g, quad.bottomLeft.b,
                  quad.bottomLeft.a}},
          {{x1, y1},
              {quad.u1, quad.v1},
              {quad.bottomRight.r, quad.bottomRight.g, quad.bottomRight.b,
                  quad.bottomRight.a}},
          {{x1, y0},
              {quad.u1, quad.v0},
              {quad.topRight.r, quad.topRight.g, quad.topRight.b,
                  quad.topRight.a}},
          {{x0, y1},
              {quad.u0, quad.v1},
              {quad.bottomLeft.r, quad.bottomLeft.g, quad.bottomLeft.b,
                  quad.bottomLeft.a}},
          {{x1, y0},
              {quad.u1, quad.v0},
              {quad.topRight.r, quad.topRight.g, quad.topRight.b,
                  quad.topRight.a}},
          {{x0, y0},
              {quad.u0, quad.v0},
              {quad.topLeft.r, quad.topLeft.g, quad.topLeft.b, quad.topLeft.a}},
      };

      vertices.insert(vertices.end(), quadVertices, quadVertices + 6);
    }

    void *mapped = nullptr;
    const VkResult result =
        vkMapMemory(_device, _vertexBufferMemory, 0, requiredSize, 0, &mapped);
    if (result != VK_SUCCESS) {
      SDL_Log("Failed to map Vulkan quad vertex buffer: %s",
          vkResultToString(result));
      return false;
    }
    std::memcpy(mapped, vertices.data(), (size_t)requiredSize);
    vkUnmapMemory(_device, _vertexBufferMemory);
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = _renderPass;
  renderPassInfo.framebuffer = _swapchainFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = _swapchainExtent;
  const std::array<VkClearValue, 2> clearValues = {clearColor, clearDepth};
  renderPassInfo.clearValueCount = (uint32_t)clearValues.size();
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  if (!_queuedWorldMeshes.empty() || validQuadCount > 0) {
    // Correct NDC Y-axis for Vulkan (it points down, but UI logic expects up)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)_swapchainExtent.width;
    viewport.height = (float)_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _swapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  }

  if (!_queuedWorldMeshes.empty()) {
    VkPipeline boundWorldPipeline = VK_NULL_HANDLE;
    VkBuffer boundWorldVertexBuffer = VK_NULL_HANDLE;
    if (_textureDescriptorSet != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
          _pipelineLayout, 0, 1, &_textureDescriptorSet, 0, nullptr);
    }
    for (const GraphicsWorldMeshDraw &draw : _queuedWorldMeshes) {
      const VkPipeline pipeline = worldPipeline(
          draw.pass, draw.depthTest, draw.primitive, draw.blendMode);

      std::map<GraphicsMeshHandle, MeshResource>::const_iterator meshIt =
          _meshes.find(draw.mesh);
      std::map<GraphicsTextureHandle, TextureResource>::const_iterator textureIt =
          _textures.find(draw.texture);
      if (pipeline == VK_NULL_HANDLE) {
        continue;
      }
      if (meshIt == _meshes.end() || meshIt->second.buffer == VK_NULL_HANDLE ||
          meshIt->second.vertexCount == 0) {
        continue;
      }
      if (textureIt == _textures.end() ||
          textureIt->second.descriptorIndex == kInvalidTextureDescriptorIndex ||
          _textureDescriptorSet == VK_NULL_HANDLE) {
        continue;
      }

      if (boundWorldPipeline != pipeline) {
        vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundWorldPipeline = pipeline;
      }

      if (boundWorldVertexBuffer != meshIt->second.buffer) {
        const VkBuffer vertexBuffers[] = {meshIt->second.buffer};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        boundWorldVertexBuffer = meshIt->second.buffer;
      }

      WorldPushConstants pushConstants{};
      std::memcpy(pushConstants.modelView, draw.modelView,
          sizeof(pushConstants.modelView));
      std::memcpy(pushConstants.projection, draw.projection,
          sizeof(pushConstants.projection));
      pushConstants.textureIndex = textureIt->second.descriptorIndex;
      std::memcpy(pushConstants.colorMultiplier, draw.colorMultiplier,
          sizeof(pushConstants.colorMultiplier));
      vkCmdPushConstants(commandBuffer, _pipelineLayout,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
          sizeof(pushConstants), &pushConstants);

      vkCmdDraw(commandBuffer, meshIt->second.vertexCount, 1, 0, 0);
    }
  }

  if (validQuadCount > 0 && _graphicsPipelineAlpha != VK_NULL_HANDLE &&
      _vertexBuffer != VK_NULL_HANDLE) {
    VkBuffer vertexBuffers[] = {_vertexBuffer};
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    if (_textureDescriptorSet != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
          _pipelineLayout, 0, 1, &_textureDescriptorSet, 0, nullptr);
    }

    VkPipeline boundPipeline = VK_NULL_HANDLE;
    uint32_t drawnQuadCount = 0;
    for (size_t i = 0; i < _queuedQuads.size(); ++i) {
      const GraphicsQuad &quad = _queuedQuads[i].quad;
      const float width =
          quad.canvasWidth > 0.0f ? quad.canvasWidth : (float)_swapchainExtent.width;
      const float height = quad.canvasHeight > 0.0f ? quad.canvasHeight
                                                    : (float)_swapchainExtent.height;

      // Filter out degenerate quads and placeholders that cause artifacts
      if (width <= 0.0f || height <= 0.0f || quad.height <= 0.0f) {
        continue;
      }

      VkPipeline pipeline = _graphicsPipelineAlpha;
      switch (_queuedQuads[i].quad.blendMode) {
      case GraphicsBlendMode::DstColorSrcColor:
        pipeline = _graphicsPipelineDstColorSrcColor;
        break;
      case GraphicsBlendMode::ZeroOneMinusSrcColor:
        pipeline = _graphicsPipelineZeroOneMinusSrcColor;
        break;
      case GraphicsBlendMode::OneMinusDstColorOneMinusSrcColor:
        pipeline = _graphicsPipelineOneMinusDstColorOneMinusSrcColor;
        break;
      case GraphicsBlendMode::Alpha:
      default:
        break;
      }
      if (pipeline == VK_NULL_HANDLE) {
        continue;
      }
      if (boundPipeline != pipeline) {
        vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundPipeline = pipeline;
      }

      std::map<GraphicsTextureHandle, TextureResource>::const_iterator textureIt =
          _textures.find(_queuedQuads[i].texture);
      if (textureIt == _textures.end() ||
          textureIt->second.descriptorIndex == kInvalidTextureDescriptorIndex ||
          _textureDescriptorSet == VK_NULL_HANDLE) {
        continue;
      }

      WorldPushConstants pushConstants{};
      pushConstants.textureIndex = textureIt->second.descriptorIndex;
      pushConstants.colorMultiplier[0] = 1.0f;
      pushConstants.colorMultiplier[1] = 1.0f;
      pushConstants.colorMultiplier[2] = 1.0f;
      pushConstants.colorMultiplier[3] = 1.0f;
      vkCmdPushConstants(commandBuffer, _pipelineLayout,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
          sizeof(pushConstants), &pushConstants);
      vkCmdDraw(commandBuffer, 6, 1, drawnQuadCount * 6, 0);
      drawnQuadCount++;
    }
  }

  vkCmdEndRenderPass(commandBuffer);

  return vkEndCommandBuffer(commandBuffer) == VK_SUCCESS;
}

auto Sdl3VulkanBackend::checkDeviceExtensionSupport(
    VkPhysicalDevice device) const -> bool {
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(
      device, nullptr, &extensionCount, availableExtensions.data());

  std::set<std::string> requiredExtensions;
  for (const char *extension : createDeviceExtensions()) {
    requiredExtensions.insert(extension);
  }

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

auto Sdl3VulkanBackend::querySwapchainSupport(VkPhysicalDevice device) const
    -> SwapchainSupportDetails {
  SwapchainSupportDetails details;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);
  if (formatCount > 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, _surface, &formatCount, details.formats.data());
  }

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, _surface, &presentModeCount, nullptr);
  if (presentModeCount > 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface,
        &presentModeCount, details.presentModes.data());
  }

  return details;
}

auto Sdl3VulkanBackend::findQueueFamilies(VkPhysicalDevice device) const
    -> QueueFamilyIndices {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(
      device, &queueFamilyCount, queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; ++i) {
    const VkQueueFamilyProperties &queueFamily = queueFamilies[i];
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
    }

    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);
    if (presentSupport == VK_TRUE) {
      indices.presentFamily = i;
    }

    if (indices.complete()) {
      break;
    }
  }

  return indices;
}

auto Sdl3VulkanBackend::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &formats) const -> VkSurfaceFormatKHR {
  for (const auto &format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }

  for (const auto &format : formats) {
    if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }

  for (const auto &format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }

  return formats.front();
}

auto Sdl3VulkanBackend::choosePresentMode(
    const std::vector<VkPresentModeKHR> &presentModes) const -> VkPresentModeKHR {
  for (VkPresentModeKHR preferredMode : preferredPresentModes()) {
    for (VkPresentModeKHR presentMode : presentModes) {
      if (presentMode == preferredMode) {
        return presentMode;
      }
    }
  }

  return VK_PRESENT_MODE_FIFO_KHR;
}

auto Sdl3VulkanBackend::chooseExtent(
    const VkSurfaceCapabilitiesKHR &capabilities, uint32_t width,
    uint32_t height) const -> VkExtent2D {
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }

  VkExtent2D actualExtent = {width, height};
  actualExtent.width = std::clamp(actualExtent.width,
      capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
  actualExtent.height = std::clamp(actualExtent.height,
      capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  return actualExtent;
}

auto Sdl3VulkanBackend::createDeviceExtensions() const
    -> std::vector<const char *> {
  return {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
}

auto Sdl3VulkanBackend::validationLayersAvailable() const -> bool {
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char *layerName : kValidationLayers) {
    bool found = false;
    for (const auto &availableLayer : availableLayers) {
      if (std::strcmp(layerName, availableLayer.layerName) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }

  return true;
}
