import Common;
import BRDF;
import EnvironmentMapping;

[[vk::binding(2, 0)]] RWTexture2D<float4> g_StorageImages[];

struct PushConstants
{
	uint OutputImage;
	uint OutputSize;
	uint SampleCount;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> g_PushConstants;

float Pow5(float x)
{
	const float x2 = x * x;
	return x2 * x2 * x;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	if (dispatchThreadID.x >= g_PushConstants.OutputSize || dispatchThreadID.y >= g_PushConstants.OutputSize)
	{
		return;
	}

	const float2 uv = (float2(dispatchThreadID.xy) + 0.5) / float(g_PushConstants.OutputSize);

	const float NdotV = max(uv.x, 0.5 / float(g_PushConstants.OutputSize));
	const float roughness = max(uv.y, Epsilon);
	const float linearRoughness = sqrt(roughness);

	const float3 N = float3(0.0, 0.0, 1.0);
	const float3 V = float3(sqrt(max(1.0 - NdotV * NdotV, 0.0)), 0.0, NdotV);

	float3 integratedBRDF = float3(0.0);

	for (uint i = 0; i < g_PushConstants.SampleCount; ++i)
	{
		float2 Xi = SampleHammersley(i, g_PushConstants.SampleCount);

		float3 H;
		float3 L;

		ImportanceSampleGGXDirection(Xi, V, N, roughness, H, L);

		const float NdotL = saturate(dot(N, L));
		const float NdotH = saturate(dot(N, H));
		const float LdotH = saturate(dot(L, H));

		if (NdotL > 0.0 && NdotH > 0.0)
		{
			const float visibility = VisibilitySmithGGXCorrelated(NdotV, NdotL, roughness);

			const float G = 4.0 * NdotV * NdotL * visibility;
			const float GVis = G * LdotH / max(NdotH * NdotV, Epsilon);
			const float Fc = Pow5(1.0 - LdotH);

			integratedBRDF.x += (1.0 - Fc) * GVis;
			integratedBRDF.y += Fc * GVis;
		}

		Xi = frac(Xi + 0.5);

		const float3 diffuseL = ImportanceSampleCosine(Xi, N);
		const float diffuseNdotL = saturate(dot(N, diffuseL));

		if (diffuseNdotL > 0.0)
		{
			const float3 diffuseH = normalize(V + diffuseL);
			const float diffuseLdotH = saturate(dot(diffuseL, diffuseH));

			const float fd90 = 0.5 + 2.0 * diffuseLdotH * diffuseLdotH * linearRoughness;
			const float lightScatter = 1.0 + (fd90 - 1.0) * Pow5(1.0 - diffuseNdotL);
			const float viewScatter = 1.0 + (fd90 - 1.0) * Pow5(1.0 - NdotV);

			integratedBRDF.z += lightScatter * viewScatter;
		}
	}

	integratedBRDF /= float(g_PushConstants.SampleCount);

	g_StorageImages[g_PushConstants.OutputImage][dispatchThreadID.xy] = float4(integratedBRDF, 1.0);
}
