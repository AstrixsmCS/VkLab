import Common;
import BRDF;

struct UBCameraData
{
	float4x4 ViewProjection;
	float4x4 InverseViewProjection;

	float3 CameraPosition;
	float _Pad0;
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
[[vk::binding(0, 0)]] Texture2D<float4> g_Textures[];
[[vk::binding(1, 0)]] SamplerState      g_Samplers[];

static const uint ANISOTROPIC_REPEAT_SAMPLER = 3;

[[vk::push_constant]]
cbuffer PushConstants
{
	float4x4 Model;

	UBCameraData* UBCamera;

	GPUMaterialData* SBMaterials;
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
	output.Position = mul(UBCamera->ViewProjection, worldPosition);
	output.WorldPos = worldPosition.xyz;
	output.Normal   = N;
	output.TexCoord = input.TexCoord;
	output.Tangent  = float4(T, input.Tangent.w);

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
	const GPUMaterialData mat = SBMaterials[MaterialIndex];
	const float2 uv = input.TexCoord;

	// Albedo
	float4 albedo = mat.Albedo;

	if (mat.AlbedoIndex != 0)
		albedo *= g_Textures[mat.AlbedoIndex].Sample(g_Samplers[ANISOTROPIC_REPEAT_SAMPLER], uv);

	// Normal
	float3 N = normalize(input.Normal);

	if (mat.NormalIndex != 0)
	{
		float3 T = normalize(input.Tangent.xyz);
		T = normalize(T - N * dot(N, T));

		const float3 B = cross(N, T) * input.Tangent.w;

		const float3 tangentNormal = g_Textures[mat.NormalIndex].Sample(g_Samplers[ANISOTROPIC_REPEAT_SAMPLER], uv).xyz * 2.0 - 1.0;

		N = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
	}

	// Metallic / Roughness
	float metalness = mat.Metalness;
	float linearRoughness = mat.Roughness;

	if (mat.MetallicRoughnessIndex != 0)
	{
		const float4 metallicRoughness = g_Textures[mat.MetallicRoughnessIndex].Sample(g_Samplers[ANISOTROPIC_REPEAT_SAMPLER], uv);

		linearRoughness *= metallicRoughness.g;
		metalness *= metallicRoughness.b;
	}

	metalness = saturate(metalness);
	linearRoughness = clamp(linearRoughness, 0.045, 1.0);

	// Frostbite GGX roughness / alpha
	const float roughness = linearRoughness * linearRoughness;

	// Occlusion
	float occlusion = 1.0;

	if (mat.OcclusionIndex != 0)
		occlusion = g_Textures[mat.OcclusionIndex].Sample(g_Samplers[ANISOTROPIC_REPEAT_SAMPLER], uv).r;

	// Directional light
	const float3 L = normalize(-LightDirection.xyz);
	const float3 V = normalize(UBCamera->CameraPosition - input.WorldPos);
	const float3 H = normalize(L + V);

	const float3 lightColor = LightColorIntensity.rgb;
	const float  lightIntensity = LightColorIntensity.a;

	const float3 radiance = lightColor * lightIntensity;

	// BRDF inputs
	const float NdotL = saturate(dot(N, L));
	const float NdotV = abs(dot(N, V)) + 0.00001;
	const float NdotH = saturate(dot(N, H));
	const float LdotH = saturate(dot(L, H));

	// Dielectric F0 -> metallic F0
	const float3 F0 = lerp(float3(0.04), albedo.rgb, metalness);

	// Schlick Fresnel
	const float3 F = FresnelSchlick(F0, 1.0, LdotH);

	// Trowbridge-Reitz GGX
	const float D = DistributionGGX(NdotH, roughness);

	// Height-correlated Smith GGX
	const float Vis = VisibilitySmithGGXCorrelated(NdotV, NdotL, roughness);

	// Cook-Torrance specular BRDF
	const float3 specular = SpecularCookTorrance(F, D, Vis);

	// Frostbite / Disney diffuse
	const float Fd = DiffuseDisney(NdotV, NdotL, LdotH, linearRoughness);

	// Energy conservation / metallic workflow
	const float3 kD = (1.0 - F) * (1.0 - metalness);

	const float3 diffuse = kD * albedo.rgb * Fd / PI;

	// Temporary ambient until IBL
	const float3 ambient = albedo.rgb * 0.05 * occlusion;

	// Rendering equation:
	//
	// Lo = (diffuse BRDF + specular BRDF) * incoming radiance * NdotL
	const float3 directLighting = (diffuse + specular) * radiance * NdotL;

	const float3 lighting = ambient + directLighting;

	return float4(lighting, albedo.a);
}
