#ifndef MCPE_PLATFORM_SDL3_VULKANBACKEND_H
#define MCPE_PLATFORM_SDL3_VULKANBACKEND_H

#include "../../client/renderer/TextureData.h"
#include "Sdl3GraphicsDriver.h"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

class Sdl3VulkanBackend final : public Sdl3GraphicsDriver {
public:
  struct QuadVertex {
    float position[2];
    float texCoord[2];
    float color[4];
  };

  struct WorldVertex {
    float position[3];
    float texCoord[2];
    float tileOrigin[2];
    uint32_t color;
  };

  Sdl3VulkanBackend() = default;
  ~Sdl3VulkanBackend() override = default;

  [[nodiscard]] auto kind() const -> GraphicsBackendKind override;
  [[nodiscard]] auto name() const -> const char * override;
  [[nodiscard]] auto windowFlags() const -> Uint32 override;
  [[nodiscard]] auto currentTexture() const -> GraphicsTextureHandle override;
  auto createTexture(const TextureData &texture,
      const GraphicsTextureOptions &options,
      GraphicsTextureHandle &outTexture) -> bool override;
  auto bindTexture(GraphicsTextureHandle texture) -> bool override;
  auto updateTexture(GraphicsTextureHandle texture,
      const GraphicsTextureUpdate &update) -> bool override;
  auto uploadMesh(const GraphicsMeshVertex *vertices, uint32_t vertexCount,
      GraphicsMeshHandle existingMesh, GraphicsMeshHandle &outMesh)
      -> bool override;
  auto drawQuad(const GraphicsQuad &quad) -> bool override;
  auto drawWorldMesh(const GraphicsWorldMeshDraw &draw) -> bool override;
  void destroyTexture(GraphicsTextureHandle texture) override;
  void destroyMesh(GraphicsMeshHandle mesh) override;

  auto createWindow(AppContext &context, const char *title, int width, int height,
      std::string &error) -> bool override;
  auto resetSurface(AppContext &context, uint32_t width, uint32_t height,
      std::string &error) -> bool override;
  void destroySurface(AppContext &context) override;
  void destroyWindow(AppContext &context) override;
  void present(AppContext &context) override;
  auto uploadTexture(const TextureData &texture, std::string &error) -> bool;
  void clearTexture();

private:
  struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] auto complete() const -> bool {
      return graphicsFamily.has_value() && presentFamily.has_value();
    }
  };

  struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };

  struct FrameSync {
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  };

  struct TextureResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t descriptorIndex = ~uint32_t(0);
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TEXF_UNCOMPRESSED_8888;
    bool transparent = true;
  };

  struct MeshResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    float minPosition[3] = {0.0f, 0.0f, 0.0f};
    float maxPosition[3] = {0.0f, 0.0f, 0.0f};
  };

  struct DeferredReleaseBucket {
    std::vector<TextureResource> textures;
    std::vector<MeshResource> meshes;
  };

  struct QueuedQuad {
    GraphicsTextureHandle texture = 0;
    GraphicsQuad quad;
  };

  struct WorldPushConstants {
    float modelView[16];
    float projection[16];
    uint32_t textureIndex = 0;
    uint32_t _padding[3] = {0, 0, 0};
    float colorMultiplier[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  };

  static constexpr int kMaxFramesInFlight = 2;
  static constexpr size_t kMaxQueuedQuads = 4096;
  static constexpr size_t kWorldPassCount = 3;
  static constexpr size_t kWorldPrimitiveCount = 3;
  static constexpr size_t kWorldBlendModeCount = 4;

  auto createInstance(std::string &error) -> bool;
  auto createSurface(SDL_Window *window, std::string &error) -> bool;
  auto pickPhysicalDevice(std::string &error) -> bool;
  auto createLogicalDevice(std::string &error) -> bool;
  auto createCommandPool(std::string &error) -> bool;
  auto createSyncObjects(std::string &error) -> bool;
  auto createPersistentResources(std::string &error) -> bool;
  auto createSwapchain(uint32_t width, uint32_t height, std::string &error)
      -> bool;
  auto createImageViews(std::string &error) -> bool;
  auto createDepthResources(std::string &error) -> bool;
  auto createRenderPass(std::string &error) -> bool;
  auto createGraphicsPipeline(std::string &error) -> bool;
  auto createGraphicsPipeline(
      GraphicsBlendMode blendMode, VkPipeline &pipeline, std::string &error)
      -> bool;
  auto createWorldPipeline(GraphicsWorldPass pass, bool depthTest,
      GraphicsMeshPrimitive primitive, GraphicsBlendMode blendMode,
      VkPipeline &pipeline, std::string &error) -> bool;
  auto createFramebuffers(std::string &error) -> bool;
  auto allocateCommandBuffers(std::string &error) -> bool;
  auto createVertexBuffer(std::string &error) -> bool;
  auto createTextureSampler(const GraphicsTextureOptions &options,
      VkSampler &sampler, std::string &error) -> bool;
  auto createDescriptorSetLayout(std::string &error) -> bool;
  auto createDescriptorPool(std::string &error) -> bool;
  auto allocateTextureDescriptorSet(std::string &error) -> bool;
  auto acquireTextureDescriptorIndex(
      uint32_t &descriptorIndex, std::string &error) -> bool;
  void releaseTextureDescriptorIndex(uint32_t descriptorIndex);
  auto updateTextureDescriptor(uint32_t descriptorIndex, VkImageView imageView,
      VkSampler sampler, std::string &error) -> bool;
  auto createTextureResource(const TextureData &texture,
      const GraphicsTextureOptions &options, TextureResource &resource,
      std::string &error) -> bool;
  auto ensureSolidWhiteTexture(std::string &error) -> bool;
  auto createShaderModule(
      const std::vector<char> &code, VkShaderModule &shaderModule,
      std::string &error) -> bool;
  auto readFile(const std::string &path, std::vector<char> &out,
      std::string &error) const -> bool;
  auto findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties,
      uint32_t &memoryTypeIndex) const -> bool;
  auto createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
      VkMemoryPropertyFlags properties, VkBuffer &buffer,
      VkDeviceMemory &bufferMemory, std::string &error) -> bool;
  auto createImage(uint32_t width, uint32_t height, VkFormat format,
      VkImageTiling tiling, VkImageUsageFlags usage,
      VkMemoryPropertyFlags properties, VkImage &image,
      VkDeviceMemory &imageMemory, std::string &error) -> bool;
  auto createImageView(VkImage image, VkFormat format,
      VkImageAspectFlags aspectMask, VkImageView &imageView, std::string &error)
      -> bool;
  auto copyBuffer(
      VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, std::string &error)
      -> bool;
  auto beginSingleUseCommands(VkCommandBuffer &commandBuffer,
      std::string &error) -> bool;
  auto endSingleUseCommands(VkCommandBuffer commandBuffer,
      std::string &error) -> bool;
  auto transitionImageLayout(VkImage image, VkFormat format,
      VkImageLayout oldLayout, VkImageLayout newLayout, std::string &error)
      -> bool;
  auto copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
      uint32_t height, uint32_t x, uint32_t y, std::string &error) -> bool;
  void destroyTextureResource(TextureResource &resource);
  void destroyMeshResource(MeshResource &resource);
  void queueTextureRelease(TextureResource &resource);
  void queueMeshRelease(MeshResource &resource);
  void flushDeferredReleases(size_t frameIndex);
  void clearDeferredReleases();
  void clearTextures();
  void clearMeshes();
  void cleanupCommandBuffers();
  void cleanupSwapchain();
  void cleanupFrames();
  void cleanupPersistentResources();
  void destroyGraphicsPipeline();
  void waitForDeviceIdle();
  auto recreateSwapchain(AppContext &context) -> bool;
  auto recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
      -> bool;

  [[nodiscard]] auto checkDeviceExtensionSupport(VkPhysicalDevice device) const
      -> bool;
  [[nodiscard]] auto querySwapchainSupport(VkPhysicalDevice device) const
      -> SwapchainSupportDetails;
  [[nodiscard]] auto findQueueFamilies(VkPhysicalDevice device) const
      -> QueueFamilyIndices;
  [[nodiscard]] auto chooseSurfaceFormat(
      const std::vector<VkSurfaceFormatKHR> &formats) const
      -> VkSurfaceFormatKHR;
  [[nodiscard]] auto choosePresentMode(
      const std::vector<VkPresentModeKHR> &presentModes) const
      -> VkPresentModeKHR;
  [[nodiscard]] auto chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities,
      uint32_t width, uint32_t height) const -> VkExtent2D;
  [[nodiscard]] auto findSupportedFormat(const std::vector<VkFormat> &candidates,
      VkImageTiling tiling, VkFormatFeatureFlags features) const -> VkFormat;
  [[nodiscard]] auto findDepthFormat() const -> VkFormat;
  [[nodiscard]] auto createDeviceExtensions() const -> std::vector<const char *>;
  [[nodiscard]] auto validationLayersAvailable() const -> bool;
  [[nodiscard]] static auto worldPassIndex(GraphicsWorldPass pass) -> size_t;
  [[nodiscard]] static auto worldPrimitiveIndex(GraphicsMeshPrimitive primitive)
      -> size_t;
  [[nodiscard]] static auto worldBlendModeIndex(GraphicsBlendMode blendMode)
      -> size_t;
  auto worldPipeline(GraphicsWorldPass pass, bool depthTest,
      GraphicsMeshPrimitive primitive, GraphicsBlendMode blendMode)
      -> VkPipeline &;
  [[nodiscard]] auto worldPipeline(GraphicsWorldPass pass, bool depthTest,
      GraphicsMeshPrimitive primitive, GraphicsBlendMode blendMode) const
      -> VkPipeline;

  bool _libraryLoaded = false;
  bool _swapchainReady = false;
  uint32_t _currentFrame = 0;
  GraphicsTextureHandle _nextTextureHandle = 1;
  GraphicsMeshHandle _nextMeshHandle = 1;
  GraphicsTextureHandle _boundTexture = 0;
  GraphicsTextureHandle _solidWhiteTexture = 0;
  GraphicsTextureHandle _bootstrapTexture = 0;

  VkInstance _instance = VK_NULL_HANDLE;
  VkSurfaceKHR _surface = VK_NULL_HANDLE;
  VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
  VkDevice _device = VK_NULL_HANDLE;
  VkQueue _graphicsQueue = VK_NULL_HANDLE;
  VkQueue _presentQueue = VK_NULL_HANDLE;
  VkCommandPool _commandPool = VK_NULL_HANDLE;

  VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
  VkFormat _swapchainImageFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D _swapchainExtent{};
  VkFormat _depthFormat = VK_FORMAT_UNDEFINED;
  VkRenderPass _renderPass = VK_NULL_HANDLE;
  VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
  VkPipeline _graphicsPipelineAlpha = VK_NULL_HANDLE;
  VkPipeline _graphicsPipelineDstColorSrcColor = VK_NULL_HANDLE;
  VkPipeline _graphicsPipelineZeroOneMinusSrcColor = VK_NULL_HANDLE;
  VkPipeline _graphicsPipelineOneMinusDstColorOneMinusSrcColor =
      VK_NULL_HANDLE;
  std::array<VkPipeline,
      kWorldPassCount * 2 * kWorldPrimitiveCount * kWorldBlendModeCount>
      _worldPipelines{};
  VkPhysicalDeviceDescriptorIndexingFeatures _descriptorIndexingFeatures{};
  VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet _textureDescriptorSet = VK_NULL_HANDLE;
  VkBuffer _vertexBuffer = VK_NULL_HANDLE;
  VkDeviceMemory _vertexBufferMemory = VK_NULL_HANDLE;
  VkDeviceSize _vertexBufferSize = 0;
  VkImage _depthImage = VK_NULL_HANDLE;
  VkDeviceMemory _depthImageMemory = VK_NULL_HANDLE;
  VkImageView _depthImageView = VK_NULL_HANDLE;

  std::map<GraphicsTextureHandle, TextureResource> _textures;
  std::map<GraphicsMeshHandle, MeshResource> _meshes;
  std::vector<GraphicsWorldMeshDraw> _queuedWorldMeshes;
  std::vector<QueuedQuad> _queuedQuads;
  std::vector<VkImage> _swapchainImages;
  std::vector<VkImageView> _swapchainImageViews;
  std::vector<VkFramebuffer> _swapchainFramebuffers;
  std::array<FrameSync, kMaxFramesInFlight> _frames{};
  std::array<DeferredReleaseBucket, kMaxFramesInFlight> _deferredReleases{};
  std::vector<VkFence> _imagesInFlight;
  std::vector<uint32_t> _freeTextureDescriptorIndices;
  uint32_t _nextTextureDescriptorIndex = 0;
};

#endif
