#pragma once
#include "types.hpp"


namespace utils {
	/* Vulkan Image Utilities. */

	void transition_image_layout(
		VkCommandBuffer cmd,
		VkImage image,
		VkFormat format,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkImageAspectFlags aspect,
		VkPipelineStageFlags2 srcStageMask,
		VkPipelineStageFlags2 dstStageMask,
		VkAccessFlags2 srcAccessMask,
		VkAccessFlags2 dstAccessMask,
		uint32_t srcQueueFamilyIndex,
		uint32_t dstQueueFamilyIndex
	);

	// Overload for when transitioning between the same queue family but want stage/access control
	void transition_image_layout(
		VkCommandBuffer cmd,
		VkImage image,
		VkFormat format,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkImageAspectFlags aspect,
		VkPipelineStageFlags2 srcStageMask,
		VkPipelineStageFlags2 dstStageMask,
		VkAccessFlags2 srcAccessMask,
		VkAccessFlags2 dstAccessMask
	);

	// Overload for a general purpose image transition, will wait for all stages to finish before transitioning, and will also stall all stages
	// until the transition happens. Recommended to use the overload with stage/access masks for better performance.
	void transition_image_layout(
		VkCommandBuffer cmd,
		VkImage image,
		VkFormat format,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkImageAspectFlags aspect
	);

	// Copies an image into another image
	void copy_image_to_image(
		VkCommandBuffer cmd,

		VkImage srcImage,
		VkImageLayout srcImageLayout,
		VkImageAspectFlags srcAspect,
		VkRect2D srcExtent,

		VkImage dstImage,
		VkImageLayout dstImageLayout,
		VkImageAspectFlags dstAspect,
		VkRect2D dstExtent

	);
}