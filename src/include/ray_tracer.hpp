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

	bool shouldStop = false;

	uint32_t _frameNumber = 0;

	void create_window();		// Creates the window, call before init_vulkan to get the right extensions for the instance.

	void init_vulkan();		// Creates Vulkan Instance and selects device, mainly through VkBootstrap.
	vkb::Instance create_instance();
	void select_device(vkb::Instance& instance);

	void main_loop();

	void init_swapchain();

	void init_pipelines();

	void init_commands();

	void init_sync_structures();

	void draw();
};