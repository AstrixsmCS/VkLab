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
	GPUMaterialData mat = Materials[MaterialIndex];

	float2 uv = input.TexCoord;

	// Albedo
	float4 albedo = mat.Albedo;

	if (mat.AlbedoIndex != 0)
	{
		albedo *= g_Textures[mat.AlbedoIndex].Sample(uv);
	}

	// Normal
	float3 normal = normalize(input.Normal);

	if (mat.NormalIndex != 0)
	{
		float3 tangentNormal = g_Textures[mat.NormalIndex].Sample(uv).xyz * 2.0 - 1.0;

		float3 T = normalize(input.Tangent);
		float3 B = normalize(input.Bitangent);
		float3 N = normalize(input.Normal);

		float3x3 TBN = float3x3(T, B, N);

		normal = normalize(mul(tangentNormal, TBN));
	}

	// Metallic / Roughness
	float metalness = mat.Metalness;
	float roughness = mat.Roughness;

	if (mat.MetallicRoughnessIndex != 0)
	{
		float4 metallicRoughness = g_Textures[mat.MetallicRoughnessIndex].Sample(uv);

		roughness *= metallicRoughness.g;
		metalness *= metallicRoughness.b;
	}

	// Occlusion
	float occlusion = 1.0;

	if (mat.OcclusionIndex != 0)
	{
		occlusion = g_Textures[mat.OcclusionIndex].Sample(uv).r;
	}

	return float4(normal * 0.5 + 0.5, occlusion);
}
