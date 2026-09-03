#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <queue>
#include <functional>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include "include/types.hpp"

constexpr int FRAMES_IN_FLIGHT = 2;

class RayTracer {
public: 
	void init();

	void run();

	void cleanup();

private:
	uint32_t _currentFrame{ 0 };

	VkCommandPool _commandPool;
	std::vector<VkCommandBuffer> _commandBuffers;

	DeletionQueue _deletionQueue;

	SDL_Window* _pWindow;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;
	VkFormat _swapchainFormat;

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

	void init_swapchain();

	void init_pipelines();

	void init_gfx_pipeline();
	void init_rt_pipeline();

	void init_commands();

	void init_descriptors();

	void init_imgui();

	void init_sync_structures();

	void draw();

	void clear_screen(VkCommandBuffer cmd, VkImage img, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void draw_imgui(VkCommandBuffer cmd, uint32_t idx, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void present_swapchain_image(uint32_t idx);
};