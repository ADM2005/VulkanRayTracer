#include "include/ray_tracer.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <VkBootstrap.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include "include/types.hpp"
#include "include/image_utils.hpp"
#include "include/vk_initialisers.hpp"
#include "include/loaders.hpp"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_sdl3.h>

#include <cmath>


constexpr bool enableValidationLayers = true;

const uint32_t width = 1800;
const uint32_t height = 900;

const char* requiredExtensions[]{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

void RayTracer::init() {
	create_window();
	init_vulkan();
	init_swapchain();
	init_pipelines();
	init_sync_structures();
	init_commands();
	init_descriptors();
	init_imgui();
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

	deletionQueue.push([&]() {
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

	deletionQueue.push([=]() {
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
	
	deletionQueue.push([=]() {
		vkb::destroy_device(_vkb_device);
	});
}

void RayTracer::init_swapchain() {
	vkb::SwapchainBuilder swapchainBuilder{_vkb_device, _surface};

	VkFormat desiredFormat = VK_FORMAT_R8G8B8A8_UNORM;
	VkColorSpaceKHR desiredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	VkSurfaceFormatKHR desiredSurfaceFormat{ .format = desiredFormat, .colorSpace = desiredColorSpace };

	swapchainBuilder.set_desired_format(desiredSurfaceFormat)
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
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

	deletionQueue.push([=]() {
		for (size_t i = 0; i < _swapchainImages.size(); i++) {
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		}
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	});
}

void RayTracer::init_pipelines() {
	init_gfx_pipeline();
	init_rt_pipeline();
}

void RayTracer::init_gfx_pipeline() {
	VkPipelineLayoutCreateInfo layoutCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCreateInfo.setLayoutCount = 0;
	layoutCreateInfo.pushConstantRangeCount = 0;

	
	if (vkCreatePipelineLayout(_device, &layoutCreateInfo, nullptr, &_gfxPipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create pipeline layout!");

	VkShaderModule vertModule = loaders::load_shader("../../shaders/hardcoded_triangle/bin/vert.vert.spv", _device);
	VkShaderModule fragModule = loaders::load_shader("../../shaders/hardcoded_triangle/bin/frag.frag.spv", _device);

	VkPipelineShaderStageCreateInfo vertInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};	
	vertInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertInfo.module = vertModule;
	vertInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	fragInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragInfo.module = fragModule;
	fragInfo.pName = "main";

	VkPipelineShaderStageCreateInfo stages[2] { vertInfo, fragInfo };
	
	VkPipelineVertexInputStateCreateInfo vertInputState{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	vertInputState.vertexAttributeDescriptionCount = 0;
	vertInputState.vertexBindingDescriptionCount = 0;		// Will be using vertex pulling instead of dedicated VB

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	viewportState.scissorCount = 1;
	viewportState.viewportCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleState.sampleShadingEnable = VK_FALSE;


	VkPipelineDepthStencilStateCreateInfo depthStencilState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = VK_FALSE;
	depthStencilState.depthWriteEnable = VK_FALSE;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_NEVER;
	depthStencilState.depthBoundsTestEnable = VK_FALSE;
	depthStencilState.stencilTestEnable = VK_FALSE;
	depthStencilState.minDepthBounds = 0.0;
	depthStencilState.maxDepthBounds = 1.0;

	VkPipelineColorBlendAttachmentState attachmentState{};
	attachmentState.blendEnable = VK_FALSE;
	attachmentState.colorWriteMask = VK_COLOR_COMPONENT_FLAG_BITS_MAX_ENUM;

	VkPipelineColorBlendStateCreateInfo colorBlendState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.logicOpEnable = VK_FALSE;
	colorBlendState.logicOp = VK_LOGIC_OP_CLEAR;	// Unused
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = &attachmentState;

	VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT	, VK_DYNAMIC_STATE_SCISSOR };


	VkPipelineDynamicStateCreateInfo dynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo pipelineRenderingInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	pipelineRenderingInfo.colorAttachmentCount = 1;
	pipelineRenderingInfo.pColorAttachmentFormats = &_swapchainFormat;
	
	VkGraphicsPipelineCreateInfo gfxPipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	gfxPipeline.pNext = &pipelineRenderingInfo;
	gfxPipeline.stageCount = 2;
	gfxPipeline.pStages = stages;

	gfxPipeline.pVertexInputState = &vertInputState;
	gfxPipeline.pInputAssemblyState = &inputAssembly;
	gfxPipeline.pViewportState = &viewportState;
	gfxPipeline.pRasterizationState = &rasterizer;
	gfxPipeline.pMultisampleState = &multisampleState;
	gfxPipeline.pDepthStencilState = &depthStencilState;
	gfxPipeline.pColorBlendState = &colorBlendState;
	gfxPipeline.pDynamicState = &dynamicState;
	gfxPipeline.layout = _gfxPipelineLayout;

	if (vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &gfxPipeline, nullptr, &_gfxPipeline)
		!= VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline!");
	}

	deletionQueue.push([=]() {
		vkDestroyShaderModule(_device, vertModule, nullptr);
		vkDestroyShaderModule(_device, fragModule, nullptr);
		vkDestroyPipelineLayout(_device, _gfxPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _gfxPipeline, nullptr);
		});
}

void RayTracer::init_rt_pipeline() {
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

	deletionQueue.push([=]() {
		vkDestroyCommandPool(_device, _commandPool, nullptr);
		});
}

void RayTracer::run() {
	while (!shouldStop) {
		main_loop();
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

	deletionQueue.push([&]() {
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
		ImGui_ImplSDL3_ProcessEvent(&event);
		auto eventType = event.type;
		if (eventType == SDL_EVENT_QUIT) {
			shouldStop = true;
		}
		if (eventType == SDL_EVENT_WINDOW_MINIMIZED) {
			minimized = true;
		}
		if (eventType == SDL_EVENT_WINDOW_RESTORED) {
			minimized = false;
		}
	}

	if (!shouldStop && !minimized) {
		draw();
	}
	if (minimized) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void RayTracer::clear_screen(VkCommandBuffer cmd, VkImage img, VkImageLayout imgLayout,
	VkImageLayout resultLayout) {

	utils::transition_image_layout(cmd, img, _swapchainFormat, imgLayout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT,
		VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT);


	VkClearColorValue clearColor{ {0.2, 0.2 + (std::cos(_currentFrame / 6.28f /5.f)) * 0.1, 0.2, 1.0}};

	VkImageSubresourceRange range = vkinit::imageSubResourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		&clearColor, 1, &range);

	utils::transition_image_layout(cmd, img, _swapchainFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		resultLayout, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void RayTracer::draw_imgui(VkCommandBuffer cmd, uint32_t img_index, VkImageLayout imgLayout,
	VkImageLayout resultLayout) {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGui::ShowDemoWindow();
	ImGui::Render();


	ImDrawData* draw_data = ImGui::GetDrawData();

	VkRect2D renderArea;
	renderArea.extent = _swapchainExtent;
	renderArea.offset = { 0,0 };

	VkRenderingAttachmentInfo colorAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
	colorAttachment.imageView = _swapchainImageViews[img_index];
	colorAttachment.imageLayout = imgLayout;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;


	VkRenderingInfo renderingInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
	renderingInfo.renderArea = renderArea;
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	
	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);

	vkCmdEndRendering(cmd);

	utils::transition_image_layout(cmd, _swapchainImages[img_index], _swapchainFormat, imgLayout,
		resultLayout, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void RayTracer::present_swapchain_image(uint32_t idx) {
	VkPresentInfoKHR presentInfo{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &_renderFinishedSemaphores[idx];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.pImageIndices = &idx;

	vkQueuePresentKHR(_presentationQueue, &presentInfo);
}

void RayTracer::draw() {
	int idx = _currentFrame % FRAMES_IN_FLIGHT;
	vkWaitForFences(_device, 1, &_renderFinishedFences[idx], VK_TRUE, UINT64_MAX);
	vkResetFences(_device, 1, &_renderFinishedFences[idx]);


	uint32_t img_index;
	vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
		_imageAvailableSemaphores[_currentFrame % FRAMES_IN_FLIGHT], VK_NULL_HANDLE,
		&img_index);

	VkImage img = _swapchainImages[img_index];
	VkCommandBuffer cmd = _commandBuffers[idx];

	vkResetCommandBuffer(cmd, 0);


	VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &beginInfo);

	clear_screen(cmd, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	
	draw_gfx(cmd, img_index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	draw_imgui(cmd, img_index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);


	vkEndCommandBuffer(cmd);

	VkSemaphoreSubmitInfo imgAvailableSemSubmit = vkinit::semaphoreSubmitInfo(_imageAvailableSemaphores[idx], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

	VkSemaphoreSubmitInfo renderFinishedSemSubmit = vkinit::semaphoreSubmitInfo(_renderFinishedSemaphores[img_index], VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);


	VkCommandBufferSubmitInfo cmdSubmit{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	cmdSubmit.commandBuffer = cmd;
	cmdSubmit.deviceMask = 1;

	VkSubmitInfo2 submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &imgAvailableSemSubmit;
	submitInfo.pSignalSemaphoreInfos = &renderFinishedSemSubmit;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &cmdSubmit;
	

	vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, _renderFinishedFences[idx]);

	present_swapchain_image(img_index);
	_currentFrame++;
}

void RayTracer::draw_gfx(VkCommandBuffer cmd, uint32_t img_index, VkImageLayout imgLayout, VkImageLayout resultLayout) {
	VkRect2D area{ .offset={0,0}, .extent = _swapchainExtent};

	VkRenderingAttachmentInfo attachInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	attachInfo.imageView = _swapchainImageViews[img_index];
	attachInfo.imageLayout = imgLayout;
	attachInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo rendInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
	rendInfo.renderArea = area;
	rendInfo.layerCount = 1;
	rendInfo.colorAttachmentCount = 1;
	rendInfo.pColorAttachments = &attachInfo;

	vkCmdBeginRendering(cmd, &rendInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _gfxPipeline);
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _swapchainExtent.width;
	viewport.height = _swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.extent = _swapchainExtent;
	scissor.offset = { 0,0 };

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRendering(cmd);

	utils::transition_image_layout(cmd, _swapchainImages[img_index], _swapchainFormat, imgLayout,
		resultLayout, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void RayTracer::cleanup() {
	vkDeviceWaitIdle(_device);
	deletionQueue.flush();
	SDL_DestroyWindow(_pWindow);
}

void RayTracer::init_descriptors() {
	create_descriptor_pool();
}

void RayTracer::create_descriptor_pool() {
	VkDescriptorPoolSize descriptorPoolSizes[]
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
		{ VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}
	};

	VkDescriptorPoolCreateInfo poolCreateInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolCreateInfo.maxSets = 0;
	poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	for (auto poolSize : descriptorPoolSizes) {
		poolCreateInfo.maxSets += poolSize.descriptorCount;
	}
	poolCreateInfo.poolSizeCount = 2;
	poolCreateInfo.pPoolSizes = descriptorPoolSizes;

	if (vkCreateDescriptorPool(_device, &poolCreateInfo, nullptr, &_descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor pool!");
	}

	deletionQueue.push([&]() {
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		});
}

void RayTracer::init_imgui() {
	ImGui::CreateContext();

	VkPipelineRenderingCreateInfo pipelineRenderingInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	pipelineRenderingInfo.colorAttachmentCount = 1;
	pipelineRenderingInfo.pColorAttachmentFormats = &_swapchainFormat;
	pipelineRenderingInfo.viewMask = 0;	// No multiview


	ImGui_ImplSDL3_InitForVulkan(_pWindow);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.ApiVersion = VK_API_VERSION_1_3;              
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _physical_device;
	init_info.Device = _device;
	init_info.QueueFamily = _graphicsQueueFamily;
	init_info.Queue = _graphicsQueue;
	init_info.PipelineCache = VK_NULL_HANDLE;
	init_info.DescriptorPool = _descriptorPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.Allocator = nullptr;

	init_info.UseDynamicRendering = true;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	deletionQueue.push([&]() {
		ImGui_ImplVulkan_Shutdown();
		});
}