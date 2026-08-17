#pragma once

#include "RendererTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

struct ShaderCompileResult
{
	std::vector<uint32_t> SpirV;
	std::vector<ShaderStage> Stages;

	bool IsValid() const { return !SpirV.empty() && !Stages.empty(); }
};

class ShaderCompiler
{
public:
	static ShaderCompileResult Compile(const std::filesystem::path& sourcePath);

	static bool Available();
};
