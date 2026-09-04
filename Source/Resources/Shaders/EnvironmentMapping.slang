#pragma once

import Common;
import BRDF;

// Compute Van der Corput radical inverse.
float RadicalInverseVdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

	return float(bits) * 2.3283064365386963e-10;
}

// Sample i-th point from Hammersley point set of sampleCount points total.
float2 SampleHammersley(uint i, uint sampleCount)
{
	const float invSamples = 1.0 / float(sampleCount);

	return float2(float(i) * invSamples, RadicalInverseVdC(i));
}

// Importance sample the Trowbridge-Reitz GGX distribution.
//
// roughness is the GGX alpha roughness:
// roughness = linearRoughness * linearRoughness.
float3 ImportanceSampleGGX(float2 Xi, float roughness, float3 N)
{
	const float alpha2 = roughness * roughness;

	const float phi = TwoPI * Xi.x;
	const float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha2 - 1.0) * Xi.y));
	const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));

	const float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

	const float3 upVector = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);

	const float3 tangentX = normalize(cross(upVector, N));
	const float3 tangentY = cross(N, tangentX);

	return normalize(tangentX * H.x + tangentY * H.y + N * H.z);
}

// Importance sample GGX and return both the half vector and reflected light vector.
void ImportanceSampleGGXDirection(float2 Xi, float3 V, float3 N, float roughness, out float3 H, out float3 L)
{
	H = ImportanceSampleGGX(Xi, roughness, N);
	L = normalize(2.0 * dot(V, H) * H - V);
}

// Frostbite approximation for the dominant direction of the GGX specular lobe.
float3 GetSpecularDominantDirection(float3 N, float3 R, float roughness)
{
	const float smoothness = saturate(1.0 - roughness);
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);

	return lerp(N, R, lerpFactor);
}

// Frostbite specular occlusion approximation.
float ComputeSpecularOcclusion(float NdotV, float occlusion, float roughness)
{
	return saturate(pow(NdotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion);
}

// Frostbite-style specular LD prefilter.
//
// This expects the source environment cubemap to already have a normal mip
// chain. A destination mip should be generated for each roughness level.
float3 PrefilterEnvironmentMap(TextureCube<float4> environmentTexture, SamplerState environmentSampler, float roughness, float3 R, uint sourceResolution, uint sourceMipCount, uint sampleCount)
{
	const float3 N = normalize(R);
	const float3 V = N;

	float3 prefilteredColor = float3(0.0);
	float totalWeight = 0.0;

	for (uint i = 0; i < sampleCount; ++i)
	{
		const float2 Xi = SampleHammersley(i, sampleCount);

		float3 H;
		float3 L;

		ImportanceSampleGGXDirection(Xi, V, N, roughness, H, L);

		const float NdotL = dot(N, L);

		if (NdotL > 0.0)
		{
			const float NdotH = saturate(dot(N, H));
			const float LdotH = saturate(dot(L, H));

			// VkLab DistributionGGX follows the Frostbite convention
			// where the 1 / PI term is applied separately.
			const float D = DistributionGGX(NdotH, roughness) / PI;

			const float pdf = max(D * NdotH / max(4.0 * LdotH, Epsilon), Epsilon);

			// Solid angle represented by the current sample.
			const float omegaS = 1.0 / (float(sampleCount) * pdf);

			// Approximate solid angle represented by one cubemap texel.
			const float resolution = float(sourceResolution);
			const float omegaP = 4.0 * PI / (6.0 * resolution * resolution);

			float mipLevel = 0.5 * log2(omegaS / omegaP);

			// Avoid source mips smaller than roughly 8x8 where cubemap
			// filtering becomes increasingly unreliable.
			const float maxMipLevel = min(max(log2(resolution) - 3.0, 0.0), float(sourceMipCount - 1));

			mipLevel = clamp(mipLevel, 0.0, maxMipLevel);

			const float3 sampleColor = environmentTexture.SampleLevel(environmentSampler, L, mipLevel).rgb;

			prefilteredColor += sampleColor * NdotL;
			totalWeight += NdotL;
		}
	}

	return prefilteredColor / max(totalWeight, Epsilon);
}

// Cosine-weighted hemisphere sample used for diffuse environment integration.
float3 SampleHemisphereCosine(float2 Xi)
{
	const float r = sqrt(Xi.x);
	const float phi = TwoPI * Xi.y;

	return float3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - Xi.x, 0.0)));
}

// Transform a cosine-weighted hemisphere sample into world space.
float3 ImportanceSampleCosine(float2 Xi, float3 N)
{
	const float3 upVector = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);

	const float3 tangentX = normalize(cross(upVector, N));
	const float3 tangentY = cross(N, tangentX);

	const float3 L = SampleHemisphereCosine(Xi);

	return normalize(tangentX * L.y + tangentY * L.x + N * L.z);
}

// Diffuse LD prefilter.
//
// The cosine-weighted sampling distribution cancels the Lambert cosine and
// sampling PDF terms, leaving an average of the sampled incident radiance.
//
// Disney diffuse is accounted for separately by the DFG preintegration.
float3 PrefilterDiffuseEnvironment(TextureCube<float4> environmentTexture, SamplerState environmentSampler, float3 N, uint sampleCount)
{
	N = normalize(N);

	float3 irradiance = float3(0.0);

	for (uint i = 0; i < sampleCount; ++i)
	{
		const float2 Xi = SampleHammersley(i, sampleCount);
		const float3 L = ImportanceSampleCosine(Xi, N);

		irradiance += environmentTexture.SampleLevel(environmentSampler, L, 0.0).rgb;
	}

	return irradiance / float(sampleCount);
}

// Convert compute dispatch coordinates into the direction represented by
// a cubemap texel.
float3 GetCubeMapTexCoord(uint3 dispatchThreadID, uint2 imageSize)
{
	const float2 st = (float2(dispatchThreadID.xy) + 0.5) / float2(imageSize);
	const float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - 1.0;

	float3 direction;

	if (dispatchThreadID.z == 0)
		direction = float3( 1.0,  uv.y, -uv.x);
	else if (dispatchThreadID.z == 1)
		direction = float3(-1.0,  uv.y,  uv.x);
	else if (dispatchThreadID.z == 2)
		direction = float3( uv.x,  1.0, -uv.y);
	else if (dispatchThreadID.z == 3)
		direction = float3( uv.x, -1.0,  uv.y);
	else if (dispatchThreadID.z == 4)
		direction = float3( uv.x,  uv.y,  1.0);
	else
		direction = float3(-uv.x,  uv.y, -1.0);

	return normalize(direction);
}

// Compute an orthonormal basis for converting from tangent space to world space.
void ComputeBasisVectors(float3 N, out float3 S, out float3 T)
{
	T = cross(N, float3(0.0, 1.0, 0.0));

	if (dot(T, T) < Epsilon)
		T = cross(N, float3(1.0, 0.0, 0.0));

	T = normalize(T);
	S = normalize(cross(N, T));
}

// Convert a tangent-space vector into world space.
float3 TangentToWorld(float3 v, float3 N, float3 S, float3 T)
{
	return S * v.x + T * v.y + N * v.z;
}
