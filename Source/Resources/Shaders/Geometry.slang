struct CameraData
{
	float4x4 ViewProjection;
	float3   CameraPosition;
};

struct VertexInput
{
	float3 Position : POSITION;
	float3 Normal   : NORMAL;
	float2 TexCoord : TEXCOORD0;
	float4 Tangent  : TANGENT;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

// Bindless arrays
[[vk::binding(0, 0)]] Texture2D    g_Textures[];
[[vk::binding(1, 0)]] SamplerState g_Samplers[];

[[vk::push_constant]]
cbuffer PushConstants
{
	float4x4 Model;
	CameraData* Camera;
	uint TextureIndex;
	uint SamplerIndex;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	const float4 worldPosition = mul(Model, float4(input.Position, 1.0));

	VertexOutput output;
	output.Position = mul(Camera->ViewProjection, worldPosition);
	output.TexCoord = input.TexCoord;

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
	return g_Textures[TextureIndex].Sample(g_Samplers[SamplerIndex], input.TexCoord);
}
