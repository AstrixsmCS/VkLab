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
	float4 Tangent   : TANGENT; // xyz = tangent, w = handedness
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
	const float3x3 model3x3      = (float3x3)Model;

	const float3 N = normalize(mul(model3x3, input.Normal));
	const float3 T = normalize(mul(model3x3, input.Tangent.xyz));

	VertexOutput output;
	output.Position = mul(Camera->ViewProjection, worldPosition);
	output.WorldPos = worldPosition.xyz;
	output.Normal   = N;
	output.TexCoord = input.TexCoord;
	output.Tangent  = float4(T, input.Tangent.w);

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
	GPUMaterialData mat = Materials[MaterialIndex];

	float3 N = normalize(input.Normal);
	float3 T = normalize(input.Tangent.xyz);

	// Re-orthonormalize tangent against the normal.
	T = normalize(T - N * dot(N, T));

	// Reconstruct bitangent from tangent handedness.
	float3 B = cross(N, T) * input.Tangent.w;

	if (mat.NormalIndex != 0)
	{
		float3 tangentNormal = g_Textures[mat.NormalIndex].Sample(input.TexCoord).xyz * 2.0 - 1.0;
		N = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
	}

		return float4(N * 0.5 + 0.5, 1.0);
}
