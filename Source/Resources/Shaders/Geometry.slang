struct CameraData
{
	float4x4 ViewProjection;
	float3   CameraPosition;
};

struct GPUMaterialData
{
	float4   Albedo;
	uint     AlbedoIndex;
	uint     NormalIndex;
	uint     MetallicRoughnessIndex;
	uint     OcclusionIndex;
	float    Metalness;
	float    Roughness;
	float    AlphaCutoff;
	uint     Flags;
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
	float4 Position  : SV_Position;
	float3 WorldPos  : TEXCOORD2;
	float3 Normal    : NORMAL;
	float2 TexCoord  : TEXCOORD0;
	float3 Tangent   : TANGENT;
	float3 Bitangent : TEXCOORD1;
};

// Bindless arrays
[[vk::binding(0, 0)]] Sampler2D g_Textures[];

[[vk::push_constant]]
cbuffer PushConstants
{
	float4x4 Model;
	CameraData* Camera;
	GPUMaterialData* Materials;
	uint             MaterialIndex;
	uint             _Pad0;
	uint             _Pad1;
	uint             _Pad2;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	const float4   worldPosition = mul(Model, float4(input.Position, 1.0));
	const float3x3 normalMatrix  = (float3x3)Model;

	const float3 N = normalize(mul(normalMatrix, input.Normal));
	const float3 T = normalize(mul(normalMatrix, input.Tangent.xyz));
	const float3 B = cross(N, T) * input.Tangent.w;

	VertexOutput output;
	output.Position  = mul(Camera->ViewProjection, worldPosition);
	output.WorldPos  = worldPosition.xyz;
	output.Normal    = N;
	output.TexCoord  = input.TexCoord;
	output.Tangent   = T;
	output.Bitangent = B;

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
	float4 tex = g_Textures[1].Sample(input.TexCoord);
	float4 material = Materials[MaterialIndex].Albedo;

	return tex * material;
}
