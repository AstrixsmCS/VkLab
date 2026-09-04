import Common;
import EnvironmentMapping;

[[vk::binding(0, 0)]] Texture2D<float4> g_SampledImages[];
[[vk::binding(1, 0)]] SamplerState g_Samplers[];
[[vk::binding(2, 0)]] RWTexture2DArray<float4> g_StorageImages[];

struct PushConstants
{
	uint EquirectangularTexture;
	uint OutputCubemap;
	uint CubemapSize;
	uint _Pad0;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

static const uint LINEAR_REPEAT_SAMPLER = 0;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= g_PushConstants.CubemapSize || dispatchThreadID.y >= g_PushConstants.CubemapSize || dispatchThreadID.z >= 6)
	{
		return;
	}

	const uint textureIndex = g_PushConstants.EquirectangularTexture;
	const uint outputIndex = g_PushConstants.OutputCubemap;

	const float2 imageSize = float2(g_PushConstants.CubemapSize, g_PushConstants.CubemapSize);

	const float3 direction = GetCubeMapTexCoord(dispatchThreadID, imageSize);

	const float phi = atan2(direction.z, direction.x);
	const float theta = acos(clamp(direction.y, -1.0, 1.0));

	const float2 equirectangularUV = float2(phi / (2.0 * PI) + 0.5, theta / PI);

	const float4 color = g_SampledImages[textureIndex].SampleLevel(g_Samplers[LINEAR_REPEAT_SAMPLER], equirectangularUV, 0.0);

	g_StorageImages[outputIndex][dispatchThreadID] = color;
}
