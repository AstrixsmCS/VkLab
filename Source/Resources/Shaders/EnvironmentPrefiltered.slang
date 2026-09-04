import Common;
import BRDF;
import EnvironmentMapping;

[[vk::binding(0, 0)]] TextureCube<float4> g_SampledImages[];
[[vk::binding(1, 0)]] SamplerState g_Samplers[];
[[vk::binding(2, 0)]] RWTexture2DArray<float4> g_StorageImages[];

static const uint LINEAR_CLAMP_SAMPLER = 1;

struct PushConstants
{
	uint EnvironmentTexture;
	uint OutputImage;
	uint OutputSize;
	uint SourceResolution;

	uint SourceMipCount;
	uint SampleCount;
	float Roughness;
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
	const float3 V = N;

	float3 prefilteredColor = float3(0.0);
	float totalWeight = 0.0;

	for (uint i = 0; i < g_PushConstants.SampleCount; ++i)
	{
		const float2 Xi = SampleHammersley(i, g_PushConstants.SampleCount);

		float3 H;
		float3 L;

		ImportanceSampleGGXDirection(Xi, V, N, g_PushConstants.Roughness, H, L);

		const float NdotL = dot(N, L);

		if (NdotL > 0.0)
		{
			const float NdotH = saturate(dot(N, H));
			const float LdotH = saturate(dot(L, H));

			const float D = DistributionGGX(NdotH, g_PushConstants.Roughness) / PI;
			const float pdf = max(D * NdotH / max(4.0 * LdotH, Epsilon), Epsilon);

			const float omegaS = 1.0 / (float(g_PushConstants.SampleCount) * pdf);

			const float sourceResolution = float(g_PushConstants.SourceResolution);
			const float omegaP = 4.0 * PI / (6.0 * sourceResolution * sourceResolution);

			float mipLevel = 0.5 * log2(omegaS / omegaP);

			const float maxMipLevel = min(max(log2(sourceResolution) - 3.0, 0.0), float(g_PushConstants.SourceMipCount - 1));

			mipLevel = clamp(mipLevel, 0.0, maxMipLevel);

			const float3 sampleColor = g_SampledImages[g_PushConstants.EnvironmentTexture].SampleLevel(g_Samplers[LINEAR_CLAMP_SAMPLER], L, mipLevel).rgb;

			prefilteredColor += sampleColor * NdotL;
			totalWeight += NdotL;
		}
	}

	prefilteredColor /= max(totalWeight, Epsilon);

	g_StorageImages[g_PushConstants.OutputImage][dispatchThreadID] = float4(prefilteredColor, 1.0);
}
