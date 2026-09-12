#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <queue>
#include <functional>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include "include/types.hpp"

#include <vk_mem_alloc.h>

#include "scene.hpp"

constexpr int FRAMES_IN_FLIGHT = 2;

class RayTracer {
public:

	VmaAllocator allocator;

	DeletionQueue deletionQueue;

	VkDevice _device;

	void init();

	void run();

	void cleanup();

	void immediate_submit(std::function<void(VkCommandBuffer cmd)> func);

	void uploadBLAS(const BVHBuildResult& blas);

private:

	struct FrameData {
		// For graphics
		VkCommandBuffer cmd;

		AllocatedBuffer materialBuffer{};				// Stores all material data
		AllocatedBuffer objectBuffer{};
		AllocatedBuffer viewUBO{};

		AllocatedImage drawImage;
		AllocatedImage depthImage;


		VkDescriptorSet viewDescriptorSet;


		// For compute
		AllocatedImage rtImage;
		AllocatedImage historyImage;

		AllocatedBuffer computeUBO;
		VkDescriptorSet rtDescriptorSet;

		// Ray Tracer
		AllocatedBuffer tlasTable{};

		// Both
		VkFence renderFinishedFence;
		VkSemaphore imageAvailableSemaphore;
	};

	std::vector<std::pair<glm::vec3, glm::vec3>> blasObjectSpaceAABBs;

	std::vector<BLASGPU> blasData;

	AllocatedBuffer blasTable;		// Written once, global across all frames

	std::vector<FrameData> frameData{ FRAMES_IN_FLIGHT };
	uint32_t _currentFrame{ 0 };

	Scene scene{};

	glm::vec3 _monkeyPos{0, 0, -5};
	glm::vec3 _monkeyAngles{0, 0, 0};
	glm::vec3 _monkeyScale{ 1, 1, 1 };


	VkCommandPool _commandPool;

	VkCommandPool _immCommandPool;
	VkCommandBuffer _immCommandBuffer;

	VkFence _immFence;

	std::vector<AllocatedMesh> _meshData;
	AllocatedMesh selectedMesh;
	VkDeviceAddress vertAddress;


	AllocatedBuffer computeUBO{};

	SDL_Window* _pWindow;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;
	VkFormat _swapchainFormat;

	std::vector<AllocatedImage> _drawImages;	// Per frame in flight
	std::vector<AllocatedImage> _depthImages;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkQueue _presentationQueue;
	uint32_t _presentationQueueFamily;
	
	VkQueue _computeQueue;
	uint32_t _computeQueueFamily;
	std::vector<AllocatedImage> _rtImages;

	VkInstance _instance;

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

	VkDescriptorSetLayout _rtDescriptorLayout;

	std::vector<VkDescriptorSet> _rtDescriptorSets;


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
	void init_graphics_images();
	void init_compute_images();

	void init_depth_images();

	void init_pipelines();

	void init_gfx_pipeline();
	void init_rt_pipeline();

	void init_commands();
	void init_imm_commands();

	void init_descriptors();

	void create_descriptor_pool();

	void create_descriptor_layouts();
	void create_gfx_descriptors();
	void create_rt_descriptors();

	void create_uniform_buffers();
	void create_compute_buffers();

	void init_imgui();

	void init_sync_structures();

	void init_bvh();			// Create the BLAS table with the existing uploaded BLAS data, as well as 
								// the TLAS structure from the object instances.
	void load_scene();

	void load_meshes();

	void draw();

	void clear_screen(VkCommandBuffer cmd, AllocatedImage img, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void draw_imgui(VkCommandBuffer cmd, AllocatedImage img, VkImageLayout imgLayout, VkImageLayout resultLayout);

	template<typename T>
	void draw_object_menus(const char* tabName, std::vector<T>& objects, T*& selectedItem);

	void draw_gfx(VkCommandBuffer cmd, FrameData& frame, VkImageLayout imgLayout, VkImageLayout resultLayout);

	void update_scene_buffers(FrameData& frame);
	void update_compute_buffers(FrameData& frame);
	void update_tlas_table(FrameData& frame);

	void draw_compute(VkCommandBuffer cmd, VkImageLayout imgLayout, VkImageLayout resultLayout);
	void present_swapchain_image(uint32_t idx);

};