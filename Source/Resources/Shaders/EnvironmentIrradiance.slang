import Common;
import EnvironmentMapping;

[[vk::binding(0, 0)]] TextureCube<float4> g_SampledImages[];
[[vk::binding(1, 0)]] SamplerState g_Samplers[];
[[vk::binding(2, 0)]] RWTexture2DArray<float4> g_StorageImages[];

static const uint LINEAR_CLAMP_SAMPLER = 1;

struct PushConstants
{
	uint EnvironmentMap;
	uint OutputImage;
	uint OutputSize;
	uint SourceResolution;

	uint SourceMipCount;
	uint SampleCount;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= g_PushConstants.OutputSize || dispatchThreadID.y >= g_PushConstants.OutputSize || dispatchThreadID.z >= 6)
	{
		return;
	}

	const float3 N = GetCubeMapTexCoord(dispatchThreadID, uint2(g_PushConstants.OutputSize, g_PushConstants.OutputSize));

	float3 irradiance = float3(0.0);

	// Frostbite-style cosine-weighted diffuse integration with
	// source mip selection added to reduce HDR sampling variance.
	const float sourceResolution = float(g_PushConstants.SourceResolution);

	const float omegaP = 4.0 * PI / (6.0 * sourceResolution * sourceResolution);

	// Avoid the smallest cubemap mips.
	const float maxMipLevel = min(max(log2(sourceResolution) - 3.0, 0.0), float(g_PushConstants.SourceMipCount - 1));

	for (uint i = 0; i < g_PushConstants.SampleCount; ++i)
	{
		const float2 Xi = SampleHammersley(i, g_PushConstants.SampleCount);

		const float3 L = ImportanceSampleCosine(Xi, N);

		const float NdotL = saturate(dot(N, L));

		if (NdotL > 0.0)
		{
			// Cosine-weighted sampling PDF.
			const float pdf = max(NdotL / PI, Epsilon);

			// Estimate the solid angle covered by this sample.
			const float omegaS = 1.0 / (float(g_PushConstants.SampleCount) * pdf);

			// Select a filtered source mip to reduce variance.
			float mipLevel = 0.5 * log2(omegaS / omegaP);
			mipLevel = clamp(mipLevel, 0.0, maxMipLevel);

			irradiance += g_SampledImages[g_PushConstants.EnvironmentMap].SampleLevel(g_Samplers[LINEAR_CLAMP_SAMPLER], L, mipLevel).rgb;
		}
	}

	// Because cosine-weighted sampling cancels the Lambert cosine and PDF
	// terms, Frostbite's diffuse LD result is the average incoming radiance.
	irradiance /= float(g_PushConstants.SampleCount);

	g_StorageImages[g_PushConstants.OutputImage][dispatchThreadID] = float4(irradiance, 1.0);
}
