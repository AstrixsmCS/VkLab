[[vk::binding(0, 0)]]
TextureCube<float4> g_SampledImages[];

[[vk::binding(1, 0)]]
SamplerState g_Samplers[];

static const uint LINEAR_CLAMP_SAMPLER = 1;

struct UBCameraData
{
	float4x4 ViewProjection;
	float4x4 InverseViewProjection;

	float3 CameraPosition;
	float _Pad0;
};

struct PushConstants
{
	UBCameraData* UBCamera;

	uint EnvironmentTexture;
	float TextureLod;
	float Intensity;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> g_PushConstants;

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Direction : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(uint vertexID : SV_VertexID)
{
	const float2 positions[3] =
	{
		float2(-1.0, -1.0),
		float2( 3.0, -1.0),
		float2(-1.0,  3.0)
	};

	const float4 position = float4(positions[vertexID], 1.0, 1.0);

	float4 worldPosition = mul(g_PushConstants.UBCamera->InverseViewProjection, position);

	worldPosition /= worldPosition.w;

	VertexOutput output;
	output.Position = position;
	output.Direction = worldPosition.xyz - g_PushConstants.UBCamera->CameraPosition;

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target0
{
	const float3 direction = normalize(input.Direction);

	const float3 color = g_SampledImages[g_PushConstants.EnvironmentTexture].SampleLevel(g_Samplers[LINEAR_CLAMP_SAMPLER], direction, g_PushConstants.TextureLod).rgb * g_PushConstants.Intensity;

	return float4(color, 1.0);
}
