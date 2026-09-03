#include "include/image_utils.hpp"
#include "include/vk_initialisers.hpp"

void utils::transition_image_layout(
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
) {

	VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.srcStageMask = srcStageMask;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.dstAccessMask = dstAccessMask;

	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;

	if (srcQueueFamilyIndex != dstQueueFamilyIndex) {
		barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
		barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	}
	else {
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	}

	barrier.image = image;
	barrier.subresourceRange = { aspect, 0, 1, 0, 1 };

	VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;


	vkCmdPipelineBarrier2(cmd, &dependency);
}

void utils::transition_image_layout(
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
) {
	VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.srcStageMask = srcStageMask;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.dstAccessMask = dstAccessMask;

	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;

	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	

	barrier.image = image;
	barrier.subresourceRange = { aspect, 0, 1, 0, 1 };

	VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;


	vkCmdPipelineBarrier2(cmd, &dependency);
}


void utils::transition_image_layout(
	VkCommandBuffer cmd,
	VkImage image,
	VkFormat format,
	VkImageLayout oldLayout,
	VkImageLayout newLayout,
	VkImageAspectFlags aspect
) {

	VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;

	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;


	barrier.image = image;
	barrier.subresourceRange = vkinit::imageSubResourceRange(aspect);

	VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;


	vkCmdPipelineBarrier2(cmd, &dependency);
}

