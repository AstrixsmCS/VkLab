#pragma once

#include "Vulkan.hpp"

#include <vector>
#include <span>

// Thin wrapper over VK_KHR_dynamic_rendering. Every pass calls BeginRendering / EndRendering
// directly; VkLab has no VkRenderPass or VkFramebuffer anywhere in the codebase.
struct AttachmentInfo
{
	VkImageView ImageView = VK_NULL_HANDLE;

	Format Format = Format::Invalid;
	LoadOp LoadOp = LoadOp::Clear;
	StoreOp StoreOp = StoreOp::Store;

	VkClearValue ClearValue = {};

	VkImageLayout Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkResolveModeFlagBits ResolveMode = VK_RESOLVE_MODE_NONE;
	VkImageView ResolveImageView = VK_NULL_HANDLE;
	VkImageLayout ResolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RenderPassInfo
	{
		std::span<const AttachmentInfo> ColorAttachments;

		AttachmentInfo* DepthAttachment = nullptr;
		AttachmentInfo* StencilAttachment = nullptr;

		VkRect2D RenderArea = {};
		uint32_t LayerCount = 1;

		VkRenderingFlags Flags = 0;
	};

class DynamicRendering
{
public:
	static void BeginRendering(VkCommandBuffer commandBuffer, const RenderPassInfo& info)
	{
		std::vector<VkRenderingAttachmentInfo> colorAttachments;
		colorAttachments.reserve(info.ColorAttachments.size());

		for (const AttachmentInfo& attachment : info.ColorAttachments)
		{
			VkRenderingAttachmentInfo attachmentInfo
			{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = attachment.ImageView,
				.imageLayout = attachment.Layout,
				.resolveMode = attachment.ResolveMode,
				.resolveImageView = attachment.ResolveImageView,
				.resolveImageLayout = attachment.ResolveImageLayout,
				.loadOp = ToVulkan(attachment.LoadOp),
				.storeOp = ToVulkan(attachment.StoreOp),
				.clearValue = attachment.ClearValue
			};

			colorAttachments.push_back(attachmentInfo);
		}

		VkRenderingAttachmentInfo depthAttachment{};
		if (info.DepthAttachment)
		{
			depthAttachment =
			{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = info.DepthAttachment->ImageView,
				.imageLayout = info.DepthAttachment->Layout,
				.resolveMode = info.DepthAttachment->ResolveMode,
				.resolveImageView = info.DepthAttachment->ResolveImageView,
				.resolveImageLayout = info.DepthAttachment->ResolveImageLayout,
				.loadOp = ToVulkan(info.DepthAttachment->LoadOp),
				.storeOp = ToVulkan(info.DepthAttachment->StoreOp),
				.clearValue = info.DepthAttachment->ClearValue
			};
		}

		VkRenderingAttachmentInfo stencilAttachment{};
		if (info.StencilAttachment)
		{
			stencilAttachment =
			{
				.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				.imageView = info.StencilAttachment->ImageView,
				.imageLayout = info.StencilAttachment->Layout,
				.resolveMode = info.StencilAttachment->ResolveMode,
				.resolveImageView = info.StencilAttachment->ResolveImageView,
				.resolveImageLayout = info.StencilAttachment->ResolveImageLayout,
				.loadOp = ToVulkan(info.StencilAttachment->LoadOp),
				.storeOp = ToVulkan(info.StencilAttachment->StoreOp),
				.clearValue = info.StencilAttachment->ClearValue
			};
		}

		VkRenderingInfo renderingInfo
		{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.flags = info.Flags,
			.renderArea = info.RenderArea,
			.layerCount = info.LayerCount,
			.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
			.pColorAttachments = colorAttachments.empty() ? nullptr : colorAttachments.data(),
			.pDepthAttachment = info.DepthAttachment ? &depthAttachment : nullptr,
			.pStencilAttachment = info.StencilAttachment ? &stencilAttachment : nullptr
		};

		vkCmdBeginRendering(commandBuffer, &renderingInfo);
	}

	static void EndRendering(VkCommandBuffer commandBuffer)
	{
		vkCmdEndRendering(commandBuffer);
	}
};
