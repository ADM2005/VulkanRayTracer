#pragma once
/* Helper functions for initialising Vulkan information structs */

#include "include/types.hpp"

namespace vkinit {
	/* 
		For creating the subresource range of a single image without mips and one layer.
		Will be doing this for the vast majority of images.
	*/
	VkImageSubresourceRange imageSubResourceRange(VkImageAspectFlags aspectMask);

	VkSemaphoreSubmitInfo semaphoreSubmitInfo(VkSemaphore semaphore, VkPipelineStageFlags2 stages);


}