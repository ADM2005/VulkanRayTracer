#define VMA_IMPLEMENTATION
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

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
#include <glm/gtx/string_cast.hpp>
#include <cmath>
#include <iterator>

constexpr bool enableValidationLayers = true;

const uint32_t width = 2100;
const uint32_t height = 1200;

const char* requiredExtensions[]{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

void RayTracer::init() {
	create_window();
	init_vulkan();
	init_vma();
	init_swapchain();
	init_commands();
	init_sync_structures();
	init_draw_images();
	init_depth_images();
	create_uniform_buffers();
	create_compute_buffers();
	init_descriptors();
	init_pipelines();
	init_imgui();
	load_scene();
}

void RayTracer::create_window() {
	SDL_Init(SDL_INIT_VIDEO);
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
	const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);


	auto instanceRet = instanceBuilder
		.set_app_name("Vulkan Ray Tracer")
		.set_engine_name("Adam's Awesome Ray Tracing Engine")
		.enable_extensions(sdlExtensionCount, sdlExtensions)
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

	auto compute_queue_index_ret = _vkb_device.get_queue_and_index(vkb::QueueType::compute);
	if (!compute_queue_index_ret) {
		throw std::runtime_error("failed to find compute queue!");
	}

	_computeQueue = compute_queue_index_ret.value().first;
	_computeQueueFamily = compute_queue_index_ret.value().second;

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

void RayTracer::init_graphics_images() {
	VkImageCreateInfo imgCreate{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgCreate.imageType = VK_IMAGE_TYPE_2D;
	imgCreate.format = VK_FORMAT_R16G16B16A16_UNORM;
	imgCreate.extent = { _swapchainExtent.width, _swapchainExtent.height, 1 };
	imgCreate.mipLevels = 1;
	imgCreate.arrayLayers = 1;
	imgCreate.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCreate.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCreate.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	imgCreate.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // for now as only using one queue
	imgCreate.queueFamilyIndexCount = 1;
	imgCreate.pQueueFamilyIndices = &_graphicsQueueFamily;

	VmaAllocationCreateInfo allocCreate{};
	allocCreate.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreate.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedImage& img = frameData[i].drawImage;
		img.extent = _swapchainExtent;
		img.format = VK_FORMAT_R16G16B16A16_UNORM;

		if (vmaCreateImage(allocator, &imgCreate, &allocCreate, &img.image, &img.alloc, nullptr) != VK_SUCCESS)
			throw std::runtime_error("failed to create view image!");
	}

	// Create image views
	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedImage& img = frameData[i].drawImage;

		VkImageViewCreateInfo imgView{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		imgView.image = img.image;
		imgView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imgView.format = VK_FORMAT_R16G16B16A16_UNORM;
		imgView.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A }; // rgba
		imgView.subresourceRange = vkinit::imageSubResourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

		if (vkCreateImageView(_device, &imgView, nullptr, &img.imageView) != VK_SUCCESS)
			throw std::runtime_error("failed to create image view!");
	}

	deletionQueue.push([&]() {
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedImage& img = frameData[i].drawImage;
			vmaDestroyImage(allocator, img.image, img.alloc);
			vkDestroyImageView(_device, img.imageView, nullptr);
		}
		});
}

void RayTracer::init_compute_images() {


	uint32_t queueFamilies[] { _computeQueueFamily, _graphicsQueueFamily };

	VkImageCreateInfo imgCreate{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgCreate.imageType = VK_IMAGE_TYPE_2D;
	imgCreate.format = VK_FORMAT_R16G16B16A16_UNORM;
	imgCreate.extent = { _swapchainExtent.width, _swapchainExtent.height, 1 };
	imgCreate.mipLevels = 1;
	imgCreate.arrayLayers = 1;
	imgCreate.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCreate.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCreate.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imgCreate.sharingMode = VK_SHARING_MODE_CONCURRENT; 
	imgCreate.queueFamilyIndexCount = 2;
	imgCreate.pQueueFamilyIndices = queueFamilies;
	imgCreate.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocCreate{};
	allocCreate.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreate.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedImage& img = frameData[i].rtImage;
		img.extent = _swapchainExtent;
		img.format = VK_FORMAT_R16G16B16A16_UNORM;

		if (vmaCreateImage(allocator, &imgCreate, &allocCreate, &img.image, &img.alloc, nullptr) != VK_SUCCESS)
			throw std::runtime_error("failed to create view image!");
	}

	// Pre-initialise images and transition them to general layouts
	immediate_submit([&](VkCommandBuffer cmd) {
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {

			AllocatedImage& allocImg = frameData[i].rtImage;
			utils::transition_image_layout(cmd, allocImg.image, allocImg.format,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT);
		}

		const VkClearColorValue clear{ 0.0,0.0,0.0,0.0 };
		VkImageSubresourceRange colorRange = vkinit::imageSubResourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedImage& allocImg = frameData[i].rtImage;
			vkCmdClearColorImage(cmd, allocImg.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &colorRange);
		}

		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedImage& allocImg = frameData[i].rtImage;
			utils::transition_image_layout(cmd, allocImg.image, allocImg.format,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_ASPECT_COLOR_BIT);
		}
		});


	// Create image views
	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		VkImageViewCreateInfo imgView{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		imgView.image = frameData[i].rtImage.image;
		imgView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imgView.format = VK_FORMAT_R16G16B16A16_UNORM;
		imgView.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A }; // rgba
		imgView.subresourceRange = vkinit::imageSubResourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

		if (vkCreateImageView(_device, &imgView, nullptr, &frameData[i].rtImage.imageView) != VK_SUCCESS)
			throw std::runtime_error("failed to create image view!");
	}

	deletionQueue.push([&]() {
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedImage& img = frameData[i].rtImage;
			vmaDestroyImage(allocator, img.image, img.alloc);
			vkDestroyImageView(_device, img.imageView, nullptr);
		}
	});
}
void RayTracer::init_draw_images() {
	init_graphics_images();
	init_compute_images();
}

void RayTracer::init_pipelines() {
	init_gfx_pipeline();
	init_rt_pipeline();
}

void RayTracer::init_gfx_pipeline() {
	VkPushConstantRange pcRange{};
	pcRange.stageFlags = VK_SHADER_STAGE_ALL;
	pcRange.offset = 0;
	pcRange.size = sizeof(GFXPushConstants);

	VkDescriptorSetLayout layouts[]{ _perFrameGFXLayout };

	VkPipelineLayoutCreateInfo layoutCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCreateInfo.setLayoutCount = 0;
	layoutCreateInfo.pushConstantRangeCount = 1;
	layoutCreateInfo.pPushConstantRanges = &pcRange;
	layoutCreateInfo.setLayoutCount = std::size(layouts);
	layoutCreateInfo.pSetLayouts = layouts;
	
	
	if (vkCreatePipelineLayout(_device, &layoutCreateInfo, nullptr, &_gfxPipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create pipeline layout!");

	VkShaderModule vertModule = loaders::load_shader("../../shaders/mesh/bin/vert.vert.spv", _device);
	VkShaderModule fragModule = loaders::load_shader("../../shaders/mesh/bin/frag.frag.spv", _device);

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
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleState.sampleShadingEnable = VK_FALSE;


	VkPipelineDepthStencilStateCreateInfo depthStencilState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = VK_TRUE;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;	// Larger depths are drawn because of reverse-z
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
	pipelineRenderingInfo.pColorAttachmentFormats = &frameData[0].drawImage.format;
	pipelineRenderingInfo.depthAttachmentFormat = frameData[0].depthImage.format;

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

	VkPushConstantRange pcRange{};
	pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pcRange.offset = 0;
	pcRange.size = sizeof(ComputePC);

	VkDescriptorSetLayout layouts[]{ _rtDescriptorLayout };

	VkPipelineLayoutCreateInfo layoutCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutCreateInfo.setLayoutCount = std::size(layouts);
	layoutCreateInfo.pSetLayouts = layouts;
	layoutCreateInfo.pushConstantRangeCount = 1;
	layoutCreateInfo.pPushConstantRanges = &pcRange;

	if (vkCreatePipelineLayout(_device, &layoutCreateInfo, nullptr, &_computeRTPipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create RT pipeline layout!");

	VkShaderModule computeShader = loaders::load_shader("../../shaders/compute/bin/basic.comp.spv", _device);
	VkPipelineShaderStageCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderInfo.module = computeShader;
	shaderInfo.pName = "main";

	VkComputePipelineCreateInfo createInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	createInfo.stage = shaderInfo;
	createInfo.layout = _computeRTPipelineLayout;

	if (vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &_computeRTPipeline) != VK_SUCCESS)
		throw std::runtime_error("failed to create RT compute pipeline!");


	vkDestroyShaderModule(_device, computeShader, nullptr); // shader module no longer needed

	deletionQueue.push([&]() {
		vkDestroyPipeline(_device, _computeRTPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _computeRTPipelineLayout, nullptr);
		});
}


void RayTracer::init_imm_commands() {
	VkCommandPoolCreateInfo poolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolCreateInfo.queueFamilyIndex = _graphicsQueueFamily;

	vkCreateCommandPool(_device, &poolCreateInfo, nullptr, &_immCommandPool);

	VkCommandBufferAllocateInfo cmdAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmdAllocInfo.commandPool = _immCommandPool;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdAllocInfo.commandBufferCount = 1;

	vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer);

	VkFenceCreateInfo fenceCreateInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence);

	deletionQueue.push([=]() {
		vkDestroyCommandPool(_device, _immCommandPool, nullptr);

		});
}

void RayTracer::immediate_submit(std::function<void(VkCommandBuffer cmd)> func) {
	vkResetFences(_device, 1, &_immFence);
	vkResetCommandBuffer(_immCommandBuffer, 0);


	VkCommandBufferBeginInfo cmdBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(_immCommandBuffer, &cmdBeginInfo);
	func(_immCommandBuffer);
	vkEndCommandBuffer(_immCommandBuffer);

	VkCommandBufferSubmitInfo cmdSubmitInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	cmdSubmitInfo.commandBuffer = _immCommandBuffer;

	VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
	
	vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, _immFence);
	vkWaitForFences(_device, 1, &_immFence, VK_TRUE, UINT64_MAX);
}

void RayTracer::init_commands() {
	VkCommandPoolCreateInfo cmdPoolCreateInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolCreateInfo.queueFamilyIndex = _graphicsQueueFamily;

	if (vkCreateCommandPool(_device, &cmdPoolCreateInfo, nullptr, &_commandPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create command pool!");
	}


	VkCommandBufferAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocInfo.commandPool = _commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = FRAMES_IN_FLIGHT;

	std::vector<VkCommandBuffer> buffers(FRAMES_IN_FLIGHT);

	if (vkAllocateCommandBuffers(_device, &allocInfo, buffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate command buffers!");
	}

	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
		frameData[i].cmd = buffers[i];
	}


	deletionQueue.push([=]() {
		vkDestroyCommandPool(_device, _commandPool, nullptr);
		vkDestroyFence(_device, _immFence, nullptr);
		});

	init_imm_commands();;
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


	_renderFinishedSemaphores.resize(_swapchainImages.size());

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		FrameData& frame = frameData[i];
		vkCreateFence(_device, &fenceCreateInfo, nullptr, &frame.renderFinishedFence);
		vkCreateSemaphore(_device, &semCreateInfo, nullptr, &frame.imageAvailableSemaphore);
	}

	for (int i = 0; i < _swapchainImages.size(); i++) {
		vkCreateSemaphore(_device, &semCreateInfo, nullptr, &_renderFinishedSemaphores[i]);
	}

	deletionQueue.push([&]() {
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			FrameData& frame = frameData[i];
			vkDestroyFence(_device, frame.renderFinishedFence, nullptr);
			vkDestroySemaphore(_device, frame.imageAvailableSemaphore, nullptr);
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

void RayTracer::clear_screen(VkCommandBuffer cmd, AllocatedImage img, VkImageLayout imgLayout,
	VkImageLayout resultLayout) {

	utils::transition_image_layout(cmd, img.image, img.format, imgLayout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT,
		VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT);


	VkClearColorValue clearColor{ {0.2, 0.2 + (std::cos(_currentFrame / 6.28f /5.f)) * 0.1, 0.2, 1.0}};

	VkImageSubresourceRange range = vkinit::imageSubResourceRange(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		&clearColor, 1, &range);

	utils::transition_image_layout(cmd, img.image, img.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		resultLayout, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

template <typename T>
void RayTracer::draw_object_menus(const char* tabName, std::vector<T>& objects, T*& selectedItem) {

	ImGui::BeginChild(
		tabName,
		ImVec2(0, 0),
		ImGuiChildFlags_Borders
	);

	// Left panel
	ImGui::BeginChild(
		"List",
		ImVec2(200, 0),
		ImGuiChildFlags_Borders
	);

	if (ImGui::BeginListBox(
		"##ClassList",
		ImVec2(-FLT_MIN, -FLT_MIN)
	))
	{
		for (size_t n = 0; n < objects.size(); n++)
		{
			T* object = &objects[n];

			bool selected = (selectedItem == object);

			if (ImGui::Selectable(
				object->name.c_str(),
				selected
			))
			{
				selectedItem = object;
			}

			if (selected)
				ImGui::SetItemDefaultFocus();
		}

		ImGui::EndListBox();
	}

	ImGui::EndChild();

	ImGui::SameLine();

	// Right panel
	ImGui::BeginChild(
		"MenuOptions",
		ImVec2(0, 0),
		ImGuiChildFlags_Borders
	);

	if (selectedItem)
		selectedItem->DrawMenu();
	else
		ImGui::TextDisabled(
			"Please select an item from the list."
		);

	ImGui::EndChild();

	ImGui::EndChild();
}

void RayTracer::draw_imgui(
	VkCommandBuffer cmd,
	AllocatedImage img,
	VkImageLayout imgLayout,
	VkImageLayout resultLayout)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	static bool open = true;

	static MeshObject* selectedMeshObject = nullptr;
	static LightObject* selectedLight = nullptr;
	static CameraObject* selectedCamera = nullptr;
	static Material* selectedMaterial = nullptr;

	ImGui::SetNextWindowSize(
		ImVec2(600, 400),
		ImGuiCond_FirstUseEver
	);

	if (ImGui::Begin("Scene Info", &open))
	{
		if (ImGui::BeginTabBar("SceneTabs"))
		{
			// -------------------------
			// Objects
			// -------------------------
			if (ImGui::BeginTabItem("Objects"))
			{
				draw_object_menus(
					"Scene Objects",
					scene.objects,
					selectedMeshObject
				);

				ImGui::EndTabItem();
			}

			// -------------------------
			// Lights
			// -------------------------
			if (ImGui::BeginTabItem("Lights"))
			{
				draw_object_menus(
					"Scene Lights",
					scene.lights,
					selectedLight
				);

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Camera"))
			{
				CameraObject& camera = scene.camera;

				camera.DrawMenu();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Materials"))
			{
				draw_object_menus("Materials", scene.materials, selectedMaterial);

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}

	ImGui::End();

	ImGui::Render();

	ImDrawData* draw_data = ImGui::GetDrawData();

	VkRect2D renderArea;
	renderArea.extent = _swapchainExtent;
	renderArea.offset = { 0, 0 };

	VkRenderingAttachmentInfo colorAttachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
	};

	colorAttachment.imageView = img.imageView;
	colorAttachment.imageLayout = imgLayout;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO
	};

	renderingInfo.renderArea = renderArea;
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);

	vkCmdEndRendering(cmd);

	utils::transition_image_layout(
		cmd,
		img.image,
		img.format,
		imgLayout,
		resultLayout,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_CLEAR_BIT,
		VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
	);
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
	FrameData& frame = frameData[idx];

	vkWaitForFences(_device, 1, &frame.renderFinishedFence, VK_TRUE, UINT64_MAX);
	vkResetFences(_device, 1, &frame.renderFinishedFence);


	uint32_t img_index;
	vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
		frame.imageAvailableSemaphore, VK_NULL_HANDLE,
		&img_index);

	VkImage& swapImage = _swapchainImages[img_index];
	VkImageView& swapImageView = _swapchainImageViews[img_index];


	AllocatedImage& drawImage = frame.drawImage;
	AllocatedImage& depthImage = frame.depthImage;


	VkCommandBuffer& cmd = frame.cmd;

	vkResetCommandBuffer(cmd, 0);


	VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &beginInfo);

	clear_screen(cmd, drawImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	
	utils::transition_image_layout(cmd, depthImage.image, depthImage.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

	//draw_compute(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	draw_gfx(cmd, frame, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	draw_imgui(cmd, drawImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// copy draw image to swapchain
	utils::transition_image_layout(cmd, swapImage, _swapchainFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
	utils::copy_image_to_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT, drawImage.extent, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT, _swapchainExtent);


	utils::transition_image_layout(cmd, swapImage, _swapchainFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
	vkEndCommandBuffer(cmd);

	VkSemaphoreSubmitInfo imgAvailableSemSubmit = vkinit::semaphoreSubmitInfo(frame.imageAvailableSemaphore, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

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
	

	vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, frame.renderFinishedFence);

	present_swapchain_image(img_index);
	_currentFrame++;
}

void RayTracer::update_scene_buffers(FrameData& frame) {
	//for (size_t i = 0; i < scene.objects.size(); ++i) {
	//	const auto& object = scene.objects[i];

	//	glm::mat4 model = object.getTransform();

	//	std::cout << "OBJECT " << i << "\n";
	//	std::cout << glm::to_string(model) << "\n";
	//}

	std::vector<MeshUBO> data;
	data.reserve(scene.objects.size());
	for (const auto& object : scene.objects) {

		MeshUBO ubo{};
		ubo.model = object.getTransform();
		ubo.objectToWorldDir = glm::inverse(glm::transpose(ubo.model));

		data.push_back(ubo);
	}

	AllocatedBuffer& objBuffer = frame.objectBuffer;
	void* ObjAddr = objBuffer.allocInfo.pMappedData;

	memcpy(ObjAddr, data.data(), scene.objects.size() * sizeof(MeshUBO));

	ViewUBO viewData{};
	float aspect = (float)_swapchainExtent.width / _swapchainExtent.height;

	auto [view, projection] = scene.camera.getViewMatrices(aspect);
	projection[1][1] *= -1;		// Account for vulkan flipped y

	viewData.view = view;
	viewData.proj = projection;

	std::vector<DirectionalLight> directionalLights;
	for (const auto& light : scene.lights) {
		DirectionalLight l;
		l.color = light.color;
		l.intensity = light.intensity;
		
		glm::quat quat(glm::radians(light.rotation));
		auto rotation = glm::mat4_cast(quat);

		l.direction = rotation * glm::vec4(0, 0, -1, 0);
		directionalLights.push_back(l);
	}

	viewData.lightCount = std::min((uint32_t)MAX_DIRECTIONAL_LIGHTS, (uint32_t)scene.lights.size());
	memcpy(viewData.lights, directionalLights.data(), viewData.lightCount * sizeof(DirectionalLight));

	AllocatedBuffer& viewBuffer = frame.viewUBO;
	void* viewAddr = viewBuffer.allocInfo.pMappedData;

	memcpy(viewAddr, &viewData, sizeof(ViewUBO));

	AllocatedBuffer& matBuffer = frame.materialBuffer;

	void* matAddr = matBuffer.allocInfo.pMappedData;
	std::vector<MaterialGPU> materialData;
	for (const auto& mat : scene.materials) materialData.push_back(mat.data);
	memcpy(matAddr, materialData.data(), sizeof(MaterialGPU) * scene.materials.size());
}

void RayTracer::draw_gfx(VkCommandBuffer cmd, FrameData& frame, VkImageLayout imgLayout, VkImageLayout resultLayout) {
	update_scene_buffers(frame);

	AllocatedImage& drawImage = frame.drawImage;
	AllocatedImage& depthImage = frame.depthImage;

	VkRect2D area{ .offset = {0,0}, .extent = drawImage.extent };

	VkRenderingAttachmentInfo attachInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	attachInfo.imageView = drawImage.imageView;
	attachInfo.imageLayout = imgLayout;
	attachInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingAttachmentInfo depthInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	depthInfo.imageView = depthImage.imageView;
	depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthInfo.clearValue = { 0.0f, 0 };
	depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo rendInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
	rendInfo.renderArea = area;
	rendInfo.layerCount = 1;
	rendInfo.colorAttachmentCount = 1;
	rendInfo.pColorAttachments = &attachInfo;
	rendInfo.pDepthAttachment = &depthInfo;


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


	vkCmdBeginRendering(cmd, &rendInfo);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _gfxPipeline);

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _gfxPipelineLayout,
		0, 1, &frame.viewDescriptorSet, 0, nullptr);


	VkDeviceAddress materialBDA = frame.materialBuffer.bufferAddress.value();
	VkDeviceAddress objectBDA = frame.objectBuffer.bufferAddress.value();

	GFXPushConstants pc;
	pc.materialAddress = materialBDA;
	pc.meshAddress = objectBDA;

	uint32_t meshIndex{ 0 };
	
	for (const auto& object : scene.objects) {
		pc.meshIndex = meshIndex;

		const AllocatedMesh& mesh = object.mesh.value();
		pc.vertAddress = mesh.vertexBuffer.bufferAddress.value();

		vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);


		for (const auto& primitive : mesh.primitives) {
			auto firstIndex = primitive.firstIndex;
			auto count = primitive.count;
			auto offset = primitive.vertexOffset;
			pc.materialIndex = primitive.materialIndex;
			
			vkCmdPushConstants(cmd, _gfxPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof(GFXPushConstants), &pc);
			vkCmdDrawIndexed(cmd, count, 1, firstIndex, offset, 0);
		}
		meshIndex++;
	}

	vkCmdEndRendering(cmd);

	utils::transition_image_layout(cmd, drawImage.image, drawImage.format, imgLayout,
		resultLayout, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void RayTracer::draw_compute(VkCommandBuffer cmd, VkImageLayout imgLayout, VkImageLayout resultLayout) {

	AllocatedImage draw_image = _rtImages[_currentFrame % FRAMES_IN_FLIGHT];
	AllocatedImage history_image = (_currentFrame % FRAMES_IN_FLIGHT == 0) ? _rtImages[FRAMES_IN_FLIGHT - 1] : _rtImages[_currentFrame % FRAMES_IN_FLIGHT - 1];

	AllocatedImage drawGFXImage = _drawImages[_currentFrame % FRAMES_IN_FLIGHT];

	ComputePC pc{};
	pc.frameNumber = glm::int16((short)_currentFrame);

	ComputeUBO ubo{};

	ubo.viewInv = glm::inverse(glm::translate(glm::mat4{ 1 }, { 0,0,-5 }));

	glm::mat4 proj = glm::perspective(70.0f, (float)_swapchainExtent.width / _swapchainExtent.height, 1000.0f, 0.01f);
	//proj[1][1] *= -1;

	ubo.projInv = glm::inverse(proj);
	ubo.imageSize = glm::ivec2(draw_image.extent.width, draw_image.extent.height);

	memcpy(computeUBO.allocInfo.pMappedData, &ubo, sizeof(ubo));

	VkDescriptorSet sets[]{ _rtDescriptorSets[_currentFrame % FRAMES_IN_FLIGHT] };
	
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _computeRTPipelineLayout,
		0, 1, sets, 0, nullptr);

	vkCmdPushConstants(cmd, _computeRTPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _computeRTPipeline);

	vkCmdDispatch(cmd, std::ceil(draw_image.extent.width / 8.0f), std::ceil(draw_image.extent.height / 8.0f), 1.0f);

	utils::transition_image_layout(cmd, draw_image.image, draw_image.format, VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	utils::transition_image_layout(cmd, drawGFXImage.image, drawGFXImage.format, imgLayout,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

	utils::copy_image_to_image(cmd, draw_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
		draw_image.extent, drawGFXImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, drawGFXImage.extent);

	utils::transition_image_layout(cmd, draw_image.image, draw_image.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

	utils::transition_image_layout(cmd, drawGFXImage.image, drawGFXImage.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		resultLayout, VK_IMAGE_ASPECT_COLOR_BIT);
}

void RayTracer::cleanup() {
	vkDeviceWaitIdle(_device);
	deletionQueue.flush();
	SDL_DestroyWindow(_pWindow);
}

void RayTracer::init_descriptors() {
	create_descriptor_pool();
	create_descriptor_layouts();
}

void RayTracer::create_descriptor_layouts() {
	create_gfx_descriptors();
	create_rt_descriptors();
}

void RayTracer::create_rt_descriptors() {
	VkDescriptorSetLayoutBinding imageBinding{};
	imageBinding.binding = 0;
	imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	imageBinding.descriptorCount = 2;
	imageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding uniformBinding{};
	uniformBinding.binding = 1;
	uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniformBinding.descriptorCount = 1;
	uniformBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutBinding bindings[]{ imageBinding, uniformBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = std::size(bindings);
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_rtDescriptorLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create descriptor set layout for compute shader");


	VkDescriptorSetLayout layouts[]{ _rtDescriptorLayout, _rtDescriptorLayout };

	std::vector<VkDescriptorSet> sets;
	sets.reserve(FRAMES_IN_FLIGHT);
	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) sets.push_back(frameData[i].rtDescriptorSet);

	VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = FRAMES_IN_FLIGHT;
	allocInfo.pSetLayouts = layouts;

	if(vkAllocateDescriptorSets(_device, &allocInfo, sets.data()) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate descriptor sets for compute shader");

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
		frameData[i].rtDescriptorSet = sets[i];


	std::vector<VkWriteDescriptorSet> set;

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {

		FrameData& frame = frameData[i];
		FrameData& previousFrame = (i == 0) ? frameData[FRAMES_IN_FLIGHT - 1] : frameData[i - 1];

		AllocatedImage& drawImage = frame.rtImage;
		AllocatedImage& historyImage = previousFrame.rtImage;

		VkDescriptorImageInfo drawImageInfo{};
		drawImageInfo.imageView = drawImage.imageView;
		drawImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;


		VkDescriptorImageInfo historyImageInfo{};
		historyImageInfo.imageView = historyImage.imageView;
		historyImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo imageWrites[2]{ drawImageInfo, historyImageInfo };

		VkWriteDescriptorSet imageWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		imageWrite.dstSet = frameData[i].rtDescriptorSet;
		imageWrite.dstBinding = 0;
		imageWrite.dstArrayElement = 0;
		imageWrite.descriptorCount = 2;
		imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		imageWrite.pImageInfo = imageWrites;

		AllocatedBuffer ubo = computeUBO; // THIS NEEDS TO BE UPDATED TO PERFRAME

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = ubo.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(ComputeUBO);

		VkWriteDescriptorSet uboWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		uboWrite.dstSet = frameData[i].rtDescriptorSet;
		uboWrite.dstBinding = 1;
		uboWrite.dstArrayElement = 0;
		uboWrite.descriptorCount = 1;
		uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboWrite.pBufferInfo = &bufferInfo;

		VkWriteDescriptorSet writes[2]{ imageWrite, uboWrite };

		vkUpdateDescriptorSets(_device, std::size(writes), writes, 0, nullptr);
	}
	deletionQueue.push([&]() {
		vkDestroyDescriptorSetLayout(_device, _rtDescriptorLayout, nullptr);
		});
}
void RayTracer::create_gfx_descriptors() {
	VkDescriptorSetLayoutBinding perFrameUniformBinding{}; // View and Projection Matrices
	perFrameUniformBinding.binding = 0;
	perFrameUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	perFrameUniformBinding.descriptorCount = 1;
	perFrameUniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo perFrameDescInfo {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	perFrameDescInfo.bindingCount = 1;
	perFrameDescInfo.pBindings = &perFrameUniformBinding;

	if (vkCreateDescriptorSetLayout(_device, &perFrameDescInfo, nullptr, &_perFrameGFXLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create descriptor set layout for per-frame graphics data");


	VkDescriptorSetLayout layouts[]{ _perFrameGFXLayout, _perFrameGFXLayout };

	VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = std::size(layouts);
	allocInfo.pSetLayouts = layouts;

	std::vector<VkDescriptorSet> sets;
	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) sets.push_back(frameData[i].viewDescriptorSet);

	if (vkAllocateDescriptorSets(_device, &allocInfo, sets.data()) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate descriptor sets!");

	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) frameData[i].viewDescriptorSet = sets[i];

	std::vector<VkWriteDescriptorSet> writes;
	writes.reserve(FRAMES_IN_FLIGHT);

	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
		FrameData& frame = frameData[i];

		VkDescriptorBufferInfo vBufferInfo{};
		vBufferInfo.buffer = frame.viewUBO.buffer;
		vBufferInfo.offset = 0;
		vBufferInfo.range = sizeof(ViewUBO);

		VkWriteDescriptorSet viewWrite{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
		};
		viewWrite.dstSet = frameData[i].viewDescriptorSet;
		viewWrite.dstBinding = 0;
		viewWrite.descriptorCount = 1;
		viewWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		viewWrite.pBufferInfo = &vBufferInfo;

		writes.push_back(viewWrite);

	}
	vkUpdateDescriptorSets(_device, writes.size(), writes.data(), 0, nullptr);

	deletionQueue.push([&]() {
		vkDestroyDescriptorSetLayout(_device, _perFrameGFXLayout, nullptr);
	});


}

void RayTracer::create_descriptor_pool() {
	VkDescriptorPoolSize descriptorPoolSizes[]
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
		{ VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}
	};

	VkDescriptorPoolCreateInfo poolCreateInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolCreateInfo.maxSets = 0;
	poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	for (auto poolSize : descriptorPoolSizes) {
		poolCreateInfo.maxSets += poolSize.descriptorCount;
	}
	poolCreateInfo.poolSizeCount = std::size(descriptorPoolSizes);
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
	pipelineRenderingInfo.pColorAttachmentFormats = &frameData[0].drawImage.format;
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

void RayTracer::init_vma() {
	VmaAllocatorCreateInfo createInfo{};
	createInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	createInfo.physicalDevice = _physical_device;
	createInfo.device = _device;
	createInfo.instance = _instance;
	createInfo.vulkanApiVersion = VK_MAKE_VERSION(1, 3, 0);

	vmaCreateAllocator(&createInfo, &allocator);

	deletionQueue.push([&]() {
		vmaDestroyAllocator(allocator);
		});
}

void RayTracer::load_scene() {
	loaders::load_scene("C:/Users/adamm/GitRepos/VulkanRayTracer/assets/Test/TestScene.gltf", this, scene);
	
	for (size_t i = 0; i < scene.objects.size(); ++i) {
		const auto& obj = scene.objects[i];
		const auto& mesh = obj.mesh.value();

		std::cout
			<< "Object " << i
			<< " name=" << obj.name
			<< " meshVB=" << mesh.vertexBuffer.buffer
			<< " meshIB=" << mesh.indexBuffer.buffer
			<< " primitives=" << mesh.primitives.size()
			<< '\n';

		for (size_t p = 0; p < mesh.primitives.size(); ++p) {
			const auto& prim = mesh.primitives[p];

			std::cout
				<< "  primitive " << p
				<< " firstIndex=" << prim.firstIndex
				<< " count=" << prim.count
				<< " vertexOffset=" << prim.vertexOffset
				<< " material=" << prim.materialIndex
				<< '\n';
		}
	}

	VkBufferCreateInfo materialBufferCreate{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	materialBufferCreate.size = sizeof(MaterialGPU) * scene.materials.size();
	materialBufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	materialBufferCreate.queueFamilyIndexCount = 1;
	materialBufferCreate.pQueueFamilyIndices = &_graphicsQueueFamily;

	VmaAllocationCreateInfo materialAllocCreate{};
	materialAllocCreate.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	materialAllocCreate.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	materialAllocCreate.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedBuffer& buff = frameData[i].materialBuffer;
		if (vmaCreateBuffer(allocator, &materialBufferCreate, &materialAllocCreate, &buff.buffer,
			&buff.alloc, &buff.allocInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to create material buffer!");
		}
		VkBufferDeviceAddressInfo addrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addrInfo.buffer = buff.buffer;

		buff.bufferAddress = vkGetBufferDeviceAddress(_device, &addrInfo);
	}

	VkBufferCreateInfo objectBufferCreate{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	objectBufferCreate.size = sizeof(MeshUBO) * scene.objects.size();
	objectBufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	objectBufferCreate.queueFamilyIndexCount = 1;
	objectBufferCreate.pQueueFamilyIndices = &_graphicsQueueFamily;

	VmaAllocationCreateInfo objectAllocCreate{};
	objectAllocCreate.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	objectAllocCreate.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	objectAllocCreate.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedBuffer& buffer = frameData[i].objectBuffer;
		if (vmaCreateBuffer(allocator, &objectBufferCreate, &objectAllocCreate, &buffer.buffer,
			&buffer.alloc, &buffer.allocInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to create object buffer!");
		}
		VkBufferDeviceAddressInfo addrInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addrInfo.buffer = frameData[i].objectBuffer.buffer;

		frameData[i].objectBuffer.bufferAddress = vkGetBufferDeviceAddress(_device, &addrInfo);
	}
	deletionQueue.push([&]() {
		for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedBuffer& obj = frameData[i].objectBuffer;
			AllocatedBuffer& mat = frameData[i].materialBuffer;

			vmaDestroyBuffer(allocator, obj.buffer, obj.alloc);
			vmaDestroyBuffer(allocator, mat.buffer, mat.alloc);
		}
		});
}

void RayTracer::load_meshes() {
	auto load_ret = loaders::load_gltf_meshes("../../assets/Suzanne/Suzanne.gltf", this);
	if (!load_ret.has_value()) {
		throw std::runtime_error("failed to load meshes!");
	}

	_meshData = load_ret.value();

	selectedMesh = _meshData[0];
	VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
	addrInfo.buffer = selectedMesh.vertexBuffer.buffer;

	vertAddress = vkGetBufferDeviceAddress(_device, &addrInfo);
}

void RayTracer::create_uniform_buffers(){
	// View Buffer
	VkBufferCreateInfo viewBufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	viewBufferInfo.size = sizeof(ViewUBO);
	viewBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	viewBufferInfo.queueFamilyIndexCount = 1;
	viewBufferInfo.pQueueFamilyIndices = &_graphicsQueueFamily;


	VmaAllocationCreateInfo viewBufferAllocInfo{};
	viewBufferAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	viewBufferAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	viewBufferAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedBuffer& ubo = frameData[i].viewUBO;
		if (vmaCreateBuffer(allocator, &viewBufferInfo, &viewBufferAllocInfo, &ubo.buffer, &ubo.alloc, &ubo.allocInfo)
			!= VK_SUCCESS) {
			throw std::runtime_error("failed to create view UBO!");
		}
	}

	deletionQueue.push([&]() {
		for (auto i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedBuffer& ubo = frameData[i].viewUBO;
			vmaDestroyBuffer(allocator, ubo.buffer, ubo.alloc);
		}
		});
}

void RayTracer::create_compute_buffers() {
	// View Buffer
	VkBufferCreateInfo compBufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	compBufferInfo.size = sizeof(ComputeUBO);
	compBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	compBufferInfo.queueFamilyIndexCount = 1;
	compBufferInfo.pQueueFamilyIndices = &_computeQueueFamily;

	VmaAllocationCreateInfo compBufferAllocInfo{};
	compBufferAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	compBufferAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	compBufferAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (vmaCreateBuffer(allocator, &compBufferInfo, &compBufferAllocInfo, &computeUBO.buffer, &computeUBO.alloc, &computeUBO.allocInfo)
		!= VK_SUCCESS) {
		throw std::runtime_error("failed to create view UBO!");
	}

	deletionQueue.push([&]() {
		vmaDestroyBuffer(allocator, computeUBO.buffer, computeUBO.alloc);
		});
}
void RayTracer::init_depth_images() {
	VkImageCreateInfo imgCreate{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgCreate.imageType = VK_IMAGE_TYPE_2D;
	imgCreate.format = VK_FORMAT_D32_SFLOAT;
	imgCreate.extent = { _swapchainExtent.width, _swapchainExtent.height, 1};
	imgCreate.mipLevels = 1;
	imgCreate.arrayLayers = 1;
	imgCreate.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCreate.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCreate.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	imgCreate.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // for now as only using one queue
	imgCreate.queueFamilyIndexCount = 1;
	imgCreate.pQueueFamilyIndices = &_graphicsQueueFamily;

	VmaAllocationCreateInfo allocCreate{};
	allocCreate.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreate.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {

		AllocatedImage& img = frameData[i].depthImage;
		img.extent = _swapchainExtent;
		img.format = VK_FORMAT_D32_SFLOAT;

		if (vmaCreateImage(allocator, &imgCreate, &allocCreate, &img.image, &img.alloc, nullptr) != VK_SUCCESS)
			throw std::runtime_error("failed to create depth image!");
	}

	// Create image views


	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		AllocatedImage& img = frameData[i].depthImage;

		VkImageViewCreateInfo imgView{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		imgView.image = img.image;
		imgView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imgView.format = VK_FORMAT_D32_SFLOAT;
		imgView.subresourceRange = vkinit::imageSubResourceRange(VK_IMAGE_ASPECT_DEPTH_BIT);

		if (vkCreateImageView(_device, &imgView, nullptr, &img.imageView) != VK_SUCCESS)
			throw std::runtime_error("failed to create image view!");
	}

	deletionQueue.push([&]() {
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
			AllocatedImage& img = frameData[i].depthImage;
			vmaDestroyImage(allocator, img.image, img.alloc);
			vkDestroyImageView(_device, img.imageView, nullptr);
		}
		});
}