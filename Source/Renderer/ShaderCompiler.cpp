#include "ShaderCompiler.hpp"

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

#include <print>
#include <string>

namespace
{
	bool CreateGlobalSession(Slang::ComPtr<slang::IGlobalSession>& globalSession)
	{
		if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
		{
			std::println("[ShaderCompiler] Failed to create Slang global session.");
			return false;
		}

		return globalSession != nullptr;
	}

	bool CreateSession(slang::IGlobalSession* globalSession, const std::filesystem::path& sourceDirectory, Slang::ComPtr<slang::ISession>& session)
	{
		slang::TargetDesc target
		{
			.format            = SLANG_SPIRV,
			.profile           = globalSession->findProfile("spirv_1_6"),
			.flags             = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
			.floatingPointMode = SLANG_FLOATING_POINT_MODE_PRECISE
		};

		slang::CompilerOptionEntry compilerOptions[]
		{
			{
				slang::CompilerOptionName::DebugInformation,
				{ slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_NONE, 0, nullptr, nullptr }
			},
			{
				slang::CompilerOptionName::Optimization,
				{ slang::CompilerOptionValueKind::Int, SLANG_OPTIMIZATION_LEVEL_NONE, 0, nullptr, nullptr }
			}
		};

		const std::string searchPath = sourceDirectory.string();
		const char* searchPaths[] = { searchPath.c_str() };

		slang::SessionDesc sessionDesc
		{
			.targets                    = &target,
			.targetCount                = 1,
			.defaultMatrixLayoutMode    = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
			.searchPaths                = searchPaths,
			.searchPathCount            = 1,
			.compilerOptionEntries      = compilerOptions,
			.compilerOptionEntryCount   = static_cast<uint32_t>(std::size(compilerOptions))
		};

		if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef())))
		{
			std::println("[ShaderCompiler] Failed to create Slang session.");
			return false;
		}

		return session != nullptr;
	}

	void PrintDiagnostics(slang::IBlob* diagnostics)
	{
		if (!diagnostics || diagnostics->getBufferSize() == 0)
			return;

		std::println("{}", static_cast<const char*>(diagnostics->getBufferPointer()));
	}

	std::vector<uint32_t> BlobToSpirV(slang::IBlob* blob)
	{
		if (!blob || blob->getBufferSize() == 0)
			return {};

		const auto* words     = static_cast<const uint32_t*>(blob->getBufferPointer());
		const size_t wordCount = blob->getBufferSize() / sizeof(uint32_t);

		return { words, words + wordCount };
	}

	ShaderStage FromSlangStage(SlangStage stage)
	{
		switch (stage)
		{
			case SLANG_STAGE_VERTEX:       return ShaderStage::Vertex;
			case SLANG_STAGE_FRAGMENT:     return ShaderStage::Fragment;
			case SLANG_STAGE_COMPUTE:      return ShaderStage::Compute;
			case SLANG_STAGE_RAY_GENERATION: return ShaderStage::RayGen;
			case SLANG_STAGE_MISS:         return ShaderStage::Miss;
			case SLANG_STAGE_CLOSEST_HIT:  return ShaderStage::ClosestHit;
			case SLANG_STAGE_ANY_HIT:      return ShaderStage::AnyHit;
			case SLANG_STAGE_INTERSECTION: return ShaderStage::Intersection;
			case SLANG_STAGE_CALLABLE:     return ShaderStage::Callable;
			default:                       return ShaderStage::None;
		}
	}
}

ShaderCompileResult ShaderCompiler::Compile(const std::filesystem::path& sourcePath)
{
	ShaderCompileResult result;

	Slang::ComPtr<slang::IGlobalSession> globalSession;
	if (!CreateGlobalSession(globalSession))
		return result;

	Slang::ComPtr<slang::ISession> session;
	if (!CreateSession(globalSession.get(), sourcePath.parent_path(), session))
		return result;

	Slang::ComPtr<slang::IBlob> diagnostics;
	const std::string moduleName = sourcePath.stem().string();

	slang::IModule* module = session->loadModule(moduleName.c_str(), diagnostics.writeRef());
	if (!module)
	{
		PrintDiagnostics(diagnostics);
		return result;
	}

	const SlangInt entryPointCount = module->getDefinedEntryPointCount();
	if (entryPointCount == 0)
	{
		std::println("[ShaderCompiler] No entry points found in '{}'.", sourcePath.string());
		return result;
	}

	for (SlangInt i = 0; i < entryPointCount; ++i)
	{
		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef())))
			continue;

		const ShaderStage stage = FromSlangStage(entryPoint->getLayout(0)->getEntryPointByIndex(0)->getStage());

		if (stage != ShaderStage::None)
			result.Stages.push_back(stage);
	}

	if (result.Stages.empty())
	{
		std::println("[ShaderCompiler] No valid shader stages found in '{}'.", sourcePath.string());
		return result;
	}

	Slang::ComPtr<slang::IBlob> spirv;
	diagnostics = nullptr;
	if (SLANG_FAILED(module->getTargetCode(0, spirv.writeRef(), diagnostics.writeRef())))
	{
		PrintDiagnostics(diagnostics);
		return result;
	}

	result.SpirV = BlobToSpirV(spirv);
	return result;
}

bool ShaderCompiler::Available()
{
	Slang::ComPtr<slang::IGlobalSession> globalSession;
	return CreateGlobalSession(globalSession);
}
