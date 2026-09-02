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