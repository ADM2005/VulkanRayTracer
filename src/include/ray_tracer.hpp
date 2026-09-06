#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <queue>
#include <functional>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include "include/types.hpp"

#include <vk_mem_alloc.h>

constexpr int FRAMES_IN_FLIGHT = 2;

class RayTracer {
public: 

	VmaAllocator allocator;

	DeletionQueue deletionQueue;


	void init();

	void run();

	void cleanup();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)> func);

private:

	uint32_t _currentFrame{ 0 };

	VkCommandPool _commandPool;
	std::vector<VkCommandBuffer> _commandBuffers;

	VkCommandPool _immCommandPool;
	VkCommandBuffer _immCommandBuffer;

	VkFence _immFence;

	std::vector<AllocatedMesh> _meshData;
	AllocatedMesh selectedMesh;
	VkDeviceAddress vertAddress;

	AllocatedBuffer viewUBO{};		// Projection and View
	AllocatedBuffer meshUBO{};		// Model matrix (potentially textures/samplers later on)

	SDL_Window* _pWindow;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;
	VkFormat _swapchainFormat;

	std::vector<AllocatedImage> _drawImages;	// Per frame in flight

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkQueue _presentationQueue;
	uint32_t _presentationQueueFamily;
	
	VkInstance _instance;

	VkDevice _device;
	vkb::Device _vkb_device;

	VkPhysicalDevice _physical_device;

	std::vector<VkFence> _renderFinishedFences;	// A fence for each frame in flight
	std::vector<VkSemaphore> _imageAvailableSemaphores; // A semaphore for each frame in flight
	std::vector<VkSemaphore> _renderFinishedSemaphores;	// A semaphore for each swapchain image

	VkPipelineLayout _gfxPipelineLayout;
	VkPipeline _gfxPipeline;

	VkDescriptorSetLayout _perFrameGFXLayout;
	std::vector<VkDescriptorSet> _perFrameGFXDescriptorSets;

	VkDescriptorSetLayout _perMeshGFXLayout;
	std::vector<VkDescriptorSet> _perMeshGFXDescriptorSets;

	VkPipelineLayout _computeRTPipelineLayout;
	VkPipeline _computeRTPipeline;

	VkDescriptorPool _descriptorPool;

	bool shouldStop = false;
	bool minimized = false;


	void create_window();		// Creates the window, call before init_vulkan to get the right extensions for the instance.

	void init_vulkan();		// Creates Vulkan Instance and selects device, mainly through VkBootstrap.
	vkb::Instance create_instance();
	void select_device(vkb::Instance& instance);

	void main_loop();

	void init_vma();

	void init_swapchain();
	void init_draw_images();

	void init_pipelines();

	void init_gfx_pipeline();
	void init_rt_pipeline();

	void init_commands();
	void init_imm_commands();

	void init_descriptors();

	void create_descriptor_pool();

	void create_descriptor_layouts();
	void create_gfx_descriptors();

	void create_uniform_buffers();

	void init_imgui();

	void init_sync_structures();

	void load_scene();

	void load_meshes();

	void draw();

	void clear_screen(VkCommandBuffer cmd, VkImage img, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void draw_imgui(VkCommandBuffer cmd, uint32_t idx, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void draw_gfx(VkCommandBuffer cmd, uint32_t idx, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void present_swapchain_image(uint32_t idx);
};