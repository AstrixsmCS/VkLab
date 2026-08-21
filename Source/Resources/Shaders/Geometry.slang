struct CameraData
{
	float4x4 ViewProjection;
};

struct VertexInput
{
	float3 Position : POSITION;
	float3 Normal   : NORMAL;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Normal   : NORMAL;
};

[[vk::push_constant]]
cbuffer PushConstants
{
	uint64_t CameraAddress;
	float4x4 Model;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	CameraData* camera = reinterpret<CameraData*>(CameraAddress);

	float4 worldPosition = mul(Model, float4(input.Position, 1.0));

	VertexOutput output;
	output.Position = mul(camera->ViewProjection, worldPosition);
	output.Normal   = normalize(mul((float3x3)Model, input.Normal));

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
	float3 color = input.Normal * 0.5 + 0.5;
	return float4(color, 1.0);
}
