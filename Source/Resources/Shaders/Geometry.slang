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

	float4 LightDirection;
	float4 LightColorIntensity;
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
	const GPUMaterialData mat = Materials[MaterialIndex];
	const float2 uv = input.TexCoord;

	// Albedo
	float4 albedo = mat.Albedo;

	if (mat.AlbedoIndex != 0)
		albedo *= g_Textures[mat.AlbedoIndex].Sample(uv);

	// Normal
	float3 N = normalize(input.Normal);

	if (mat.NormalIndex != 0)
	{
		float3 T = normalize(input.Tangent.xyz);
		T = normalize(T - N * dot(N, T));

		const float3 B = cross(N, T) * input.Tangent.w;

		const float3 tangentNormal = g_Textures[mat.NormalIndex].Sample(uv).xyz * 2.0 - 1.0;

		N = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
	}

	// Metallic / Roughness
	float metalness = mat.Metalness;
	float roughness = mat.Roughness;

	if (mat.MetallicRoughnessIndex != 0)
	{
		const float4 metallicRoughness = g_Textures[mat.MetallicRoughnessIndex].Sample(uv);

		roughness *= metallicRoughness.g;
		metalness *= metallicRoughness.b;
	}

	// Occlusion
	float occlusion = 1.0;

	if (mat.OcclusionIndex != 0)
		occlusion = g_Textures[mat.OcclusionIndex].Sample(uv).r;

	// Directional light
	const float3 L = normalize(-LightDirection.xyz);
	const float3 V = normalize(Camera->CameraPosition - input.WorldPos);
	const float3 H = normalize(L + V);

	const float3 lightColor = LightColorIntensity.rgb;
	const float lightIntensity = LightColorIntensity.a;
	const float3 radiance = lightColor * lightIntensity;

	// Simple Blinn-Phong lighting
	const float NdotL = max(dot(N, L), 0.0);
	const float NdotH = max(dot(N, H), 0.0);

	const float3 ambient = albedo.rgb * 0.05 * occlusion;
	const float3 diffuse = albedo.rgb * NdotL;
	const float shininess = lerp(128.0, 8.0, roughness);
	const float specularStrength = lerp(0.04, 1.0, metalness);
	const float specular = NdotL > 0.0 ? pow(NdotH, shininess) * specularStrength : 0.0;

	const float3 lighting = ambient + (diffuse + specular) * radiance;

	return float4(lighting, albedo.a);
}
