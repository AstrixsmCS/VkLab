#pragma once

#include "CommandBuffer.hpp"

class ImGuiLayer
{
public:
	void Initialize();
	void Shutdown();

	void Begin();
	void End(CommandBuffer& commandBuffer);
private:
	CommandBuffer m_CommandBuffer;
};
