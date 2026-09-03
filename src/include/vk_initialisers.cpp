#include "include/vk_initialisers.hpp"


VkImageSubresourceRange vkinit::imageSubResourceRange(VkImageAspectFlags aspectMask) {
	VkImageSubresourceRange range{};
	range.aspectMask = aspectMask;
	range.baseArrayLayer = 0;
	range.baseMipLevel = 0;
	range.layerCount = 1;
	range.levelCount = 1;

	return range;
}

VkSemaphoreSubmitInfo vkinit::semaphoreSubmitInfo(VkSemaphore semaphore, VkPipelineStageFlags2 stages) {
	VkSemaphoreSubmitInfo info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	info.semaphore = semaphore;
	info.stageMask = stages;
	info.deviceIndex = 0;
	
	return info;
}