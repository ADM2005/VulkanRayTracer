#include "include/ray_tracer.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <VkBootstrap.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include "include/types.hpp"
#include "include/image_utils.hpp"

constexpr bool enableValidationLayers = true;

const uint32_t width = 1400;
const uint32_t height = 800;

const char* requiredExtensions[]{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
};

void RayTracer::init() {
	create_window();
	init_vulkan();
	init_swapchain();
	init_pipelines();
	init_sync_structures();
	init_commands();
}

void RayTracer::create_window() {
	_pWindow = SDL_CreateWindow("Super Awesome Ray Tracer", width, height,
		SDL_WINDOW_VULKAN);

}

void RayTracer::init_vulkan() {
	vkb::Instance vkbInstance = create_instance();
	_instance = vkbInstance.instance;

	// Create Surface
	if (!SDL_Vulkan_CreateSurface(_pWindow, _instance, nullptr, &_surface)) {
		throw std::runtime_error("failed to create surface!");
	}

	_deletionQueue.push([&]() {
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		});
	select_device(vkbInstance);


}

vkb::Instance RayTracer::create_instance() {
	vkb::InstanceBuilder instanceBuilder;

	uint32_t sdlExtensionCount;
	auto sdlExtensions = *SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

	const char* const extensions[]{ sdlExtensions };

	auto instanceRet = instanceBuilder
		.set_app_name("Vulkan Ray Tracer")
		.set_engine_name("Adam's Awesome Ray Tracing Engine")
		.enable_extensions(extensions)
		.use_default_debug_messenger()
		.enable_validation_layers(enableValidationLayers)

		.require_api_version(1, 3, 0)							// Will be using dynamic rendering, so 1.3.0 as a conservative estimate, could end up 1.4
		.build();

	if (!instanceRet) {
		throw std::runtime_error("Failed to create Vulkan Instance! Reason: " + instanceRet.error().message());
	}

	_deletionQueue.push([=]() {
		vkb::destroy_instance(instanceRet.value());
	});
	return instanceRet.value();

}

void RayTracer::select_device(vkb::Instance& instance) {
	VkPhysicalDeviceVulkan12Features vk12Features{};
	vk12Features.bufferDeviceAddress = VK_TRUE;


	VkPhysicalDeviceVulkan13Features vk13Features{};
	vk13Features.dynamicRendering = VK_TRUE;
	vk13Features.synchronization2 = VK_TRUE;

	vkb::PhysicalDeviceSelector deviceSelector(instance);
	auto phys_device_ret = deviceSelector.set_surface(_surface)
		.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
		.allow_any_gpu_device_type(false)
		.add_required_extensions(requiredExtensions)
		.set_required_features_12(vk12Features)
		.set_required_features_13(vk13Features)
		.require_present(true)
		.select();

	if (!phys_device_ret) {
		throw std::runtime_error("failed to select physical device! Reason: " + phys_device_ret.error().message());
	}

	_physical_device = phys_device_ret.value().physical_device;

	vkb::DeviceBuilder deviceBuilder{ phys_device_ret.value() };
	auto device_ret = deviceBuilder.build();
	if (!device_ret) {
		throw std::runtime_error("failed to create logical device! Reason: " + device_ret.error().message());
	}
	
	_vkb_device = device_ret.value();

	_device = _vkb_device.device;

	auto gfx_queue_index_ret = _vkb_device.get_queue_and_index(vkb::QueueType::graphics);

	if (!gfx_queue_index_ret) {
		throw std::runtime_error("failed to find graphics queue!");
	}

	_graphicsQueue = gfx_queue_index_ret.value().first;
	_graphicsQueueFamily = gfx_queue_index_ret.value().second;

	auto present_queue_index_ret = _vkb_device.get_queue_and_index(vkb::QueueType::present);

	if (!present_queue_index_ret) {
		// Should only really happen for device s
		throw std::runtime_error("failed to find presentation queue!");
	}

	_presentationQueue = present_queue_index_ret.value().first;
	_presentationQueueFamily = present_queue_index_ret.value().second;

	std::cout << "Device: " << _vkb_device.physical_device.name << '\n';
	
	_deletionQueue.push([=]() {
		vkb::destroy_device(_vkb_device);
	});
}

void RayTracer::init_swapchain() {
	vkb::SwapchainBuilder swapchainBuilder{_vkb_device, _surface};

	VkFormat desiredFormat = VK_FORMAT_R8G8B8A8_UNORM;
	VkColorSpaceKHR desiredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	VkSurfaceFormatKHR desiredSurfaceFormat{ .format = desiredFormat, .colorSpace = desiredColorSpace };

	swapchainBuilder.set_desired_format(desiredSurfaceFormat)
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.add_image_usage_flags(VK_IMAGE_USAGE_STORAGE_BIT);

	auto swapchain_ret = swapchainBuilder.build();
	if (!swapchain_ret) {
		throw std::runtime_error("failed to build swapchain!");
	}

	vkb::Swapchain vkb_swapchain = swapchain_ret.value();
	

	_swapchainImages = vkb_swapchain.get_images().value();
	_swapchainImageViews = vkb_swapchain.get_image_views().value();
	_swapchainExtent = vkb_swapchain.extent;

	_swapchain = vkb_swapchain.swapchain;
	_swapchainFormat = vkb_swapchain.image_format;

	_deletionQueue.push([=]() {
		for (size_t i = 0; i < _swapchainImages.size(); i++) {
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		}
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	});
}

void RayTracer::init_pipelines() {
	
}

void RayTracer::init_commands() {
	VkCommandPoolCreateInfo cmdPoolCreateInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolCreateInfo.queueFamilyIndex = _graphicsQueueFamily;

	if (vkCreateCommandPool(_device, &cmdPoolCreateInfo, nullptr, &_commandPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create command pool!");
	}

	_commandBuffers.resize(FRAMES_IN_FLIGHT);

	VkCommandBufferAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocInfo.commandPool = _commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = FRAMES_IN_FLIGHT;

	if (vkAllocateCommandBuffers(_device, &allocInfo, _commandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate command buffers!");
	}

	_deletionQueue.push([=]() {
		vkDestroyCommandPool(_device, _commandPool, nullptr);
		});
}

void RayTracer::run() {
	while (!shouldStop) {
		main_loop();
		_frameNumber++;
	}

}

void RayTracer::init_sync_structures() {
	VkSemaphoreCreateInfo semCreateInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkFenceCreateInfo fenceCreateInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	_renderFinishedFences.resize(FRAMES_IN_FLIGHT);
	_imageAvailableSemaphores.resize(FRAMES_IN_FLIGHT);
	_renderFinishedSemaphores.resize(_swapchainImages.size());

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFinishedFences[i]);
		vkCreateSemaphore(_device, &semCreateInfo, nullptr, &_imageAvailableSemaphores[i]);
	}

	for (int i = 0; i < _swapchainImages.size(); i++) {
		vkCreateSemaphore(_device, &semCreateInfo, nullptr, &_renderFinishedSemaphores[i]);
	}

	_deletionQueue.push([&]() {
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			vkDestroyFence(_device, _renderFinishedFences[i], nullptr);
			vkDestroySemaphore(_device, _imageAvailableSemaphores[i], nullptr);
		}
		for (int i = 0; i < _swapchainImages.size(); i++) {
			vkDestroySemaphore(_device, _renderFinishedSemaphores[i], nullptr);
		}
		});
}

void RayTracer::main_loop() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		auto eventType = event.type;
		if (eventType == SDL_EVENT_QUIT) {
			shouldStop = true;
		}
	}

	draw();
}

void RayTracer::draw() {
	int idx = _currentFrame % FRAMES_IN_FLIGHT;
	vkWaitForFences(_device, 1, &_renderFinishedFences[idx], VK_TRUE, UINT64_MAX);
	vkResetFences(_device, 1, &_renderFinishedFences[idx]);


	uint32_t img_index;
	vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
		_imageAvailableSemaphores[_currentFrame % FRAMES_IN_FLIGHT], VK_NULL_HANDLE,
		&img_index);

	VkCommandBuffer cmd = _commandBuffers[idx];
	vkResetCommandBuffer(cmd, 0);

	VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &beginInfo);

	VkImage img = _swapchainImages[img_index];

	utils::transition_image_layout(cmd, img, _swapchainFormat, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, _graphicsQueueFamily, _graphicsQueueFamily);

	VkClearColorValue clearColor{ {0.0, 1.0, 0.0, 1.0} };

	VkImageSubresourceRange range{};
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.baseArrayLayer = 0;
	range.baseMipLevel = 0;
	range.layerCount = 1;
	range.levelCount = 1;

	vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		&clearColor, 1, &range);
	
	utils::transition_image_layout(cmd, img, _swapchainFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE, _graphicsQueueFamily, _presentationQueueFamily);

	vkEndCommandBuffer(cmd);

	VkPipelineStageFlags stageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;

	VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &_imageAvailableSemaphores[idx];
	submitInfo.pWaitDstStageMask = &stageMask;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &_renderFinishedSemaphores[img_index];
	
	
	vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _renderFinishedFences[idx]);

	VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &_renderFinishedSemaphores[img_index];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.pImageIndices = &img_index;
	
	vkQueuePresentKHR(_presentationQueue, &presentInfo);

	_currentFrame++;
}

void RayTracer::cleanup() {
	vkDeviceWaitIdle(_device);
	_deletionQueue.flush();
	SDL_DestroyWindow(_pWindow);
}