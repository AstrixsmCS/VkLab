#include "Mesh.hpp"

#include "Allocator.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <mikktspace.h>

#include <print>
#include <cassert>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static glm::mat4 NodeToMatrix(const fastgltf::Node& node)
{
	if (const auto* trs = std::get_if<fastgltf::TRS>(&node.transform))
	{
		const glm::vec3 translation = glm::make_vec3(trs->translation.data());
		const glm::quat rotation    = glm::make_quat(trs->rotation.data());
		const glm::vec3 scale       = glm::make_vec3(trs->scale.data());

		return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
	}

	if (const auto* mat = std::get_if<fastgltf::math::fmat4x4>(&node.transform))
	{
		return glm::make_mat4(mat->data());
	}

	return glm::mat4(1.0f);
}

static void ExtractTRS(const fastgltf::Node& gltfNode, Node& node)
{
	if (const auto* trs = std::get_if<fastgltf::TRS>(&gltfNode.transform))
	{
		node.Translation = glm::make_vec3(trs->translation.data());
		node.Rotation    = glm::make_quat(trs->rotation.data());
		node.Scale       = glm::make_vec3(trs->scale.data());
	}
}

// MikkTSpace
struct MikkTSpaceUserData
{
	std::vector<Vertex>&   Vertices;
	std::vector<uint32_t>& Indices;
	std::vector<glm::vec4> Tangents;
};

static int MikkGetNumFaces(const SMikkTSpaceContext* context)
{
	const auto* data = static_cast<const MikkTSpaceUserData*>(context->m_pUserData);
	return static_cast<int>(data->Indices.size() / 3);
}

static int MikkGetNumVerticesOfFace(const SMikkTSpaceContext*, const int)
{
	return 3;
}

static void MikkGetPosition(const SMikkTSpaceContext* context, float out[], const int iFace, const int iVert)
{
	const auto* data = static_cast<const MikkTSpaceUserData*>(context->m_pUserData);

	const uint32_t index = data->Indices[iFace * 3 + iVert];

	const glm::vec3& pos = data->Vertices[index].Position;

	out[0] = pos.x;
	out[1] = pos.y;
	out[2] = pos.z;
}

static void MikkGetNormal(const SMikkTSpaceContext* context, float out[], const int iFace, const int iVert)
{
	const auto* data = static_cast<const MikkTSpaceUserData*>(context->m_pUserData);

	const uint32_t index = data->Indices[iFace * 3 + iVert];

	const glm::vec3& n = data->Vertices[index].Normal;

	out[0] = n.x;
	out[1] = n.y;
	out[2] = n.z;
}

static void MikkGetTexCoord(const SMikkTSpaceContext* context, float out[], const int iFace, const int iVert)
{
	const auto* data = static_cast<const MikkTSpaceUserData*>(context->m_pUserData);

	const uint32_t index = data->Indices[iFace * 3 + iVert];

	const glm::vec2& uv = data->Vertices[index].TexCoord;

	out[0] = uv.x;
	out[1] = uv.y;
}

static void MikkSetTSpaceBasic(const SMikkTSpaceContext* context, const float tangent[], const float sign, const int iFace, const int iVert)
{
	auto* data = static_cast<MikkTSpaceUserData*>(context->m_pUserData);

	const uint32_t corner = iFace * 3 + iVert;

	data->Tangents[corner] =
	{
		tangent[0],
		tangent[1],
		tangent[2],
		sign
	};
}

bool Mesh::GenerateTangents(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
	if (vertices.empty() || indices.empty())
		return false;

	if (indices.size() % 3 != 0)
		return false;

	MikkTSpaceUserData userData
	{
		.Vertices = vertices,
		.Indices  = indices,
		.Tangents = std::vector<glm::vec4>(indices.size())
	};

	SMikkTSpaceInterface iface{};
	iface.m_getNumFaces          = MikkGetNumFaces;
	iface.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace;
	iface.m_getPosition          = MikkGetPosition;
	iface.m_getNormal            = MikkGetNormal;
	iface.m_getTexCoord          = MikkGetTexCoord;
	iface.m_setTSpaceBasic       = MikkSetTSpaceBasic;
	iface.m_setTSpace            = nullptr;

	SMikkTSpaceContext context{};
	context.m_pInterface = &iface;
	context.m_pUserData  = &userData;

	const tbool result = genTangSpaceDefault(&context);

	if (!result)
	{
		std::println("[Mesh] MikkTSpace tangent generation failed");
		return false;
	}

	std::vector<Vertex> newVertices;
	std::vector<uint32_t> newIndices;

	newVertices.reserve(indices.size());
	newIndices.reserve(indices.size());

	for (size_t i = 0; i < indices.size(); i++)
	{
		Vertex vertex = vertices[indices[i]];
		vertex.Tangent = userData.Tangents[i];

		newIndices.push_back(static_cast<uint32_t>(newVertices.size()));
		newVertices.push_back(vertex);
	}

	vertices = std::move(newVertices);
	indices  = std::move(newIndices);

	return true;
}

// Mesh
bool Mesh::Load(const std::filesystem::path& path)
{
	if (!std::filesystem::exists(path))
	{
		std::println("[Mesh] File not found: {}", path.string());
		return false;
	}

	constexpr fastgltf::Options options =
		fastgltf::Options::GenerateMeshIndices |
		fastgltf::Options::DecomposeNodeMatrices |
		fastgltf::Options::LoadExternalBuffers;

	fastgltf::Parser parser;

	auto dataResult = fastgltf::GltfDataBuffer::FromPath(path);
	if (dataResult.error() != fastgltf::Error::None)
	{
		std::println("[Mesh] Failed to read file '{}': {}", path.string(), fastgltf::getErrorMessage(dataResult.error()));

		return false;
	}

	const std::string ext = path.extension().string();

	fastgltf::Expected<fastgltf::Asset> assetResult { fastgltf::Error::None };

	if (ext == ".glb")
	{
		assetResult = parser.loadGltfBinary(dataResult.get(), path.parent_path(), options);
	}
	else if (ext == ".gltf")
	{
		assetResult = parser.loadGltf(dataResult.get(), path.parent_path(), options);
	}
	else
	{
		std::println("[Mesh] Unsupported file extension '{}': expected .gltf or .glb", ext);
		return false;
	}

	if (assetResult.error() != fastgltf::Error::None)
	{
		std::println("[Mesh] Failed to parse '{}': {}", path.string(), fastgltf::getErrorMessage(assetResult.error()));
		return false;
	}

	fastgltf::Asset& asset = assetResult.get();

	m_Name = path.stem().string();

	std::vector<Format> textureFormats(asset.textures.size(), Format::RGBA8_SRGB);

	for (const fastgltf::Material& gltfMaterial : asset.materials)
	{
		const auto& pbr = gltfMaterial.pbrData;

		if (gltfMaterial.normalTexture.has_value())
		{
			const size_t texIndex = gltfMaterial.normalTexture->textureIndex;
			if (texIndex < textureFormats.size())
				textureFormats[texIndex] = Format::RGBA8_UNorm;
		}

		if (pbr.metallicRoughnessTexture.has_value())
		{
			const size_t texIndex = pbr.metallicRoughnessTexture->textureIndex;
			if (texIndex < textureFormats.size())
				textureFormats[texIndex] = Format::RGBA8_UNorm;
		}

		if (gltfMaterial.occlusionTexture.has_value())
		{
			const size_t texIndex = gltfMaterial.occlusionTexture->textureIndex;
			if (texIndex < textureFormats.size())
				textureFormats[texIndex] = Format::RGBA8_UNorm;
		}
	}

	// Textures
	m_Textures.reserve(asset.textures.size());

	for (size_t i = 0; i < asset.textures.size(); i++)
	{
		const fastgltf::Texture& gltfTexture = asset.textures[i];

		if (!gltfTexture.imageIndex.has_value())
		{
			m_Textures.push_back(nullptr);
			continue;
		}

		const fastgltf::Image& gltfImage = asset.images[gltfTexture.imageIndex.value()];
		const Format format = textureFormats[i];

		auto texture = std::make_shared<Texture>();
		bool loaded  = false;

		std::visit(fastgltf::visitor
		{
			[&](const fastgltf::sources::URI& uri)
			{
				const std::filesystem::path texturePath = path.parent_path() / uri.uri.path();

				TextureSpecification spec;
				spec.DebugName    = gltfImage.name.empty() ? texturePath.filename().string() : std::string(gltfImage.name);
				spec.Format       = format;
				spec.GenerateMips = true;

				texture->Create(spec, texturePath);
				loaded = texture->IsValid();
			},
			[&](const fastgltf::sources::Array& arr)
			{
				TextureSpecification spec;
				spec.DebugName    = gltfImage.name.empty() ? std::string(m_Name) : std::string(gltfImage.name);
				spec.Format       = format;
				spec.GenerateMips = true;

				int width = 0;
				int height = 0;
				int channels = 0;

				stbi_uc* pixels = stbi_load_from_memory(
				reinterpret_cast<const stbi_uc*>(arr.bytes.data()),
				static_cast<int>(arr.bytes.size()),
				&width, &height, &channels,
				STBI_rgb_alpha);

				if (pixels)
				{
					spec.Width  = static_cast<uint32_t>(width);
					spec.Height = static_cast<uint32_t>(height);
					spec.Depth  = 1;

					texture->Create(spec, pixels);

					stbi_image_free(pixels);
					loaded = texture->IsValid();
				}
			},
			[&](const fastgltf::sources::BufferView& bufferView)
			{
				const fastgltf::BufferView& view   = asset.bufferViews[bufferView.bufferViewIndex];
				const fastgltf::Buffer&     buffer = asset.buffers[view.bufferIndex];

				std::visit(fastgltf::visitor
				{
					[&](const fastgltf::sources::Array& arr)
					{
						TextureSpecification spec;
						spec.DebugName    = gltfImage.name.empty() ? std::string(m_Name) : std::string(gltfImage.name);
						spec.Format       = format;
						spec.GenerateMips = true;

						int width = 0;
						int height = 0;
						int channels = 0;

						stbi_uc* pixels = stbi_load_from_memory(
							reinterpret_cast<const stbi_uc*>(arr.bytes.data() + view.byteOffset),
							static_cast<int>(view.byteLength),
							&width, &height, &channels,
							STBI_rgb_alpha);

						if (pixels)
						{
							spec.Width  = static_cast<uint32_t>(width);
							spec.Height = static_cast<uint32_t>(height);
							spec.Depth  = 1;

							texture->Create(spec, pixels);

							stbi_image_free(pixels);
							loaded = texture->IsValid();
						}
					},
					[](auto&) {}
				}, buffer.data);
			},
			[](auto&) {}
		}, gltfImage.data);

		if (loaded)
		{
			std::println("[Mesh] Loaded texture '{}' ({})", texture->GetSpecification().DebugName, format == Format::RGBA8_SRGB ? "sRGB" : "Linear");
			m_Textures.push_back(std::move(texture));
		}
		else
		{
			std::println("[Mesh] Failed to load texture at index {}", i);
			m_Textures.push_back(nullptr);
		}
	}

	// Materials
	m_Materials.reserve(asset.materials.size());

	for (const fastgltf::Material& gltfMaterial : asset.materials)
	{
		Material material;

		const auto& pbr = gltfMaterial.pbrData;

		// Albedo color factor
		const auto& c = pbr.baseColorFactor;
		material.SetColor({ c[0], c[1], c[2], c[3] });

		// Metallic / roughness factors
		material.SetMetalness(pbr.metallicFactor);
		material.SetRoughness(pbr.roughnessFactor);

		// Albedo texture
		if (pbr.baseColorTexture.has_value())
		{
			const size_t texIndex = pbr.baseColorTexture->textureIndex;
			assert(texIndex < m_Textures.size());

			if (m_Textures[texIndex])
			{
				material.SetTexture({
					.Texture    = m_Textures[texIndex],
					.Type       = MapType::Albedo,
					.UvIndex    = 0,
					.UseMap     = true,
					.UseTexture = true
				});
			}
		}

		// Normal texture
		if (gltfMaterial.normalTexture.has_value())
		{
			const size_t texIndex = gltfMaterial.normalTexture->textureIndex;
			assert(texIndex < m_Textures.size());

			if (m_Textures[texIndex])
			{
				material.SetTexture({
					.Texture    = m_Textures[texIndex],
					.Type       = MapType::Normal,
					.UvIndex    = 0,
					.UseMap     = true,
					.UseTexture = true
				});
			}
		}

		// Metallic/roughness texture
		if (pbr.metallicRoughnessTexture.has_value())
		{
			const size_t texIndex = pbr.metallicRoughnessTexture->textureIndex;
			assert(texIndex < m_Textures.size());

			if (m_Textures[texIndex])
			{
				material.SetTexture({
					.Texture    = m_Textures[texIndex],
					.Type       = MapType::MetallicRoughness,
					.UvIndex    = 0,
					.UseMap     = true,
					.UseTexture = true
				});
			}
		}

		// Occlusion texture
		if (gltfMaterial.occlusionTexture.has_value())
		{
			const size_t texIndex = gltfMaterial.occlusionTexture->textureIndex;
			assert(texIndex < m_Textures.size());

			if (m_Textures[texIndex])
			{
				material.SetTexture({
					.Texture    = m_Textures[texIndex],
					.Type       = MapType::Occlusion,
					.UvIndex    = 0,
					.UseMap     = true,
					.UseTexture = true
				});
			}
		}

		// Alpha mode
		if (gltfMaterial.alphaMode == fastgltf::AlphaMode::Mask)
		{
			material.SetRenderMode(Material::RenderMode::Cutout);
			material.SetAlphaCutoff(gltfMaterial.alphaCutoff);
		}
		else if (gltfMaterial.alphaMode == fastgltf::AlphaMode::Blend)
		{
			material.SetRenderMode(Material::RenderMode::Transparent);
		}

		m_Materials.push_back(std::move(material));
	}

	std::vector<std::vector<uint32_t>> meshSubmeshes(asset.meshes.size());

	for (size_t meshIndex = 0; meshIndex < asset.meshes.size(); meshIndex++)
	{
		const fastgltf::Mesh& gltfMesh = asset.meshes[meshIndex];

		for (const fastgltf::Primitive& primitive : gltfMesh.primitives)
		{
			if (primitive.type != fastgltf::PrimitiveType::Triangles)
				continue;

			std::vector<Vertex>   vertices;
			std::vector<uint32_t> indices;

			bool hasNormals   = false;
			bool hasTexCoords = false;

			// Positions
			{
				const auto it = primitive.findAttribute("POSITION");
				assert(it != primitive.attributes.end() && "Mesh has no POSITION attribute");

				const fastgltf::Accessor& accessor = asset.accessors[it->accessorIndex];

				vertices.resize(accessor.count);

				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
					asset, accessor,
					[&](const fastgltf::math::fvec3& pos, size_t index)
					{
						vertices[index].Position = { pos.x(), pos.y(), pos.z() };
					});
			}

			// Normals
			{
				const auto it = primitive.findAttribute("NORMAL");
				if (it != primitive.attributes.end())
				{
					const fastgltf::Accessor& accessor = asset.accessors[it->accessorIndex];

					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
						asset, accessor,
						[&](const fastgltf::math::fvec3& normal, size_t index)
						{
							vertices[index].Normal = { normal.x(), normal.y(), normal.z() };
						});

					hasNormals = true;
				}
			}

			// TexCoords
			{
				const auto it = primitive.findAttribute("TEXCOORD_0");
				if (it != primitive.attributes.end())
				{
					const fastgltf::Accessor& accessor = asset.accessors[it->accessorIndex];

					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
						asset, accessor,
						[&](const fastgltf::math::fvec2& uv, size_t index)
						{
							vertices[index].TexCoord = { uv.x(), uv.y() };
						});

					hasTexCoords = true;
				}
			}

			// Indices
			{
				assert(primitive.indicesAccessor.has_value() && "Mesh primitive has no indices");

				const fastgltf::Accessor& accessor = asset.accessors[primitive.indicesAccessor.value()];

				indices.resize(accessor.count);

				fastgltf::iterateAccessorWithIndex<uint32_t>(
					asset, accessor,
					[&](uint32_t index, size_t i)
					{
						indices[i] = index;
					});
			}

			// Generate tangents via MikkTSpace.
			// This expands vertices to one-per-corner (unindexed output from MikkTSpace).
			// Only run if we have the data MikkTSpace needs.
			if (hasNormals && hasTexCoords)
				GenerateTangents(vertices, indices);

			const uint32_t submeshIndex = static_cast<uint32_t>(m_Submeshes.size());

			meshSubmeshes[meshIndex].push_back(submeshIndex);

			Submesh& submesh = m_Submeshes.emplace_back();

			submesh.BaseVertex  = static_cast<uint32_t>(m_Vertices.size());
			submesh.BaseIndex   = static_cast<uint32_t>(m_Indices.size());
			submesh.VertexCount = static_cast<uint32_t>(vertices.size());
			submesh.IndexCount  = static_cast<uint32_t>(indices.size());

			if (primitive.materialIndex.has_value())
				submesh.MaterialIndex = static_cast<uint32_t>(primitive.materialIndex.value());

			m_Vertices.insert(m_Vertices.end(), vertices.begin(), vertices.end());
			m_Indices.insert(m_Indices.end(), indices.begin(), indices.end());
		}
	}

	// Nodes
	m_Nodes.emplace_back();

	m_Nodes[0].Name           = m_Name;
	m_Nodes[0].Parent         = UINT32_MAX;
	m_Nodes[0].LocalTransform = glm::mat4(1.0f);
	m_Nodes[0].WorldTransform = glm::mat4(1.0f);

	constexpr uint32_t nodeOffset = 1;

	m_Nodes.resize(asset.nodes.size() + nodeOffset);

	// Create Nodes
	for (size_t i = 0; i < asset.nodes.size(); i++)
	{
		const fastgltf::Node& gltfNode = asset.nodes[i];
		Node& node = m_Nodes[nodeOffset + static_cast<uint32_t>(i)];

		node.Name = gltfNode.name;

		ExtractTRS(gltfNode, node);
		node.LocalTransform = NodeToMatrix(gltfNode);

		// Children
		for (size_t childIndex : gltfNode.children)
		{
			node.Children.push_back(nodeOffset + static_cast<uint32_t>(childIndex));
		}

		// Mesh / Submeshes
		if (gltfNode.meshIndex.has_value())
		{
			const size_t meshIndex = gltfNode.meshIndex.value();

			assert(meshIndex < meshSubmeshes.size());

			for (uint32_t submeshIndex : meshSubmeshes[meshIndex])
			{
				node.Submeshes.push_back(submeshIndex);
			}
		}
	}

	// Parent Relationships
	for (size_t i = 0; i < asset.nodes.size(); i++)
	{
		const uint32_t parentIndex = nodeOffset + static_cast<uint32_t>(i);

		const Node& parent = m_Nodes[parentIndex];

		for (uint32_t childIndex : parent.Children)
		{
			assert(childIndex < m_Nodes.size());

			m_Nodes[childIndex].Parent = parentIndex;
		}
	}

	// Scene Roots
	if (!asset.scenes.empty())
	{
		const size_t sceneIndex = asset.defaultScene.value_or(0);

		assert(sceneIndex < asset.scenes.size());

		const fastgltf::Scene& scene = asset.scenes[sceneIndex];

		for (size_t gltfNodeIndex : scene.nodeIndices)
		{
			const uint32_t nodeIndex = nodeOffset + static_cast<uint32_t>(gltfNodeIndex);

			assert(nodeIndex < m_Nodes.size());

			m_Nodes[0].Children.push_back(nodeIndex);

			m_Nodes[nodeIndex].Parent = 0;
		}
	}
	else
	{
		for (size_t i = 0; i < asset.nodes.size(); i++)
		{
			const uint32_t nodeIndex = nodeOffset + static_cast<uint32_t>(i);

			if (m_Nodes[nodeIndex].Parent != UINT32_MAX)
				continue;

			m_Nodes[0].Children.push_back(nodeIndex);

			m_Nodes[nodeIndex].Parent = 0;
		}
	}

	// World Transforms
	std::function<void(uint32_t, const glm::mat4&)> updateTransforms = [&](uint32_t nodeIndex, const glm::mat4& parentTransform)
	{
		assert(nodeIndex < m_Nodes.size());

		Node& node = m_Nodes[nodeIndex];

		node.WorldTransform = parentTransform * node.LocalTransform;

		for (uint32_t childIndex : node.Children)
		{
			updateTransforms(childIndex, node.WorldTransform);
		}
	};

	updateTransforms(0, glm::mat4(1.0f));

	// GPU Upload
	if (!m_Vertices.empty())
	{
		m_VertexBuffer.Create(m_Vertices.data(), m_Vertices.size() * sizeof(Vertex));
	}

	if (!m_Indices.empty())
	{
		m_IndexBuffer.Create(m_Indices.data(), m_Indices.size() * sizeof(uint32_t));
	}

	std::println("[Mesh] Loaded '{}' - {} vertices, {} indices, {} submeshes, {} nodes, {} textures, {} materials",
		m_Name,
		m_Vertices.size(),
		m_Indices.size(),
		m_Submeshes.size(),
		m_Nodes.size(),
		m_Textures.size(),
		m_Materials.size());

	return true;
}

void Mesh::Destroy()
{
	m_VertexBuffer.Destroy();
	m_IndexBuffer.Destroy();

	m_Vertices.clear();
	m_Indices.clear();
	m_Submeshes.clear();
	m_Materials.clear();
	m_Nodes.clear();

	for (auto& texture : m_Textures)
	{
		if (texture)
			texture->Destroy();
	}

	m_Textures.clear();

	m_Name.clear();
}

void Mesh::TraverseNodes(const std::function<void(const Node&)>& callback) const
{
	if (m_Nodes.empty())
		return;

	std::function<void(uint32_t)> traverse = [&](uint32_t nodeIndex)
	{
		const Node& node = m_Nodes[nodeIndex];
		callback(node);

		for (uint32_t childIndex : node.Children)
			traverse(childIndex);
	};

	traverse(0);
}
