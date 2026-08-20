#include "Mesh.hpp"

#include "Allocator.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesh
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

	fastgltf::Expected<fastgltf::Asset> assetResult{ fastgltf::Error::None };

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

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Geometry
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	for (const fastgltf::Mesh& gltfMesh : asset.meshes)
	{
		for (const fastgltf::Primitive& primitive : gltfMesh.primitives)
		{
			if (primitive.type != fastgltf::PrimitiveType::Triangles)
				continue;

			Submesh& submesh   = m_Submeshes.emplace_back();
			submesh.BaseVertex = static_cast<uint32_t>(m_Vertices.size());
			submesh.BaseIndex  = static_cast<uint32_t>(m_Indices.size());

			// Positions
			{
				const auto it = primitive.findAttribute("POSITION");
				assert(it != primitive.attributes.end() && "Mesh has no POSITION attribute");

				const fastgltf::Accessor& accessor = asset.accessors[it->accessorIndex];

				const uint32_t vertexStart = static_cast<uint32_t>(m_Vertices.size());
				m_Vertices.resize(vertexStart + accessor.count);

				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
					asset, accessor,
					[&](const fastgltf::math::fvec3& pos, size_t index)
					{
						m_Vertices[vertexStart + index].Position = { pos.x(), pos.y(), pos.z() };
					});

				submesh.VertexCount = static_cast<uint32_t>(accessor.count);
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
							m_Vertices[submesh.BaseVertex + index].Normal = { normal.x(), normal.y(), normal.z() };
						});
				}
			}

			// Indices
			{
				assert(primitive.indicesAccessor.has_value() && "Mesh primitive has no indices");

				const fastgltf::Accessor& accessor = asset.accessors[primitive.indicesAccessor.value()];

				const uint32_t indexStart = static_cast<uint32_t>(m_Indices.size());
				m_Indices.resize(indexStart + accessor.count);

				fastgltf::iterateAccessorWithIndex<uint32_t>(
					asset, accessor,
					[&](uint32_t index, size_t i)
					{
						m_Indices[indexStart + i] = submesh.BaseVertex + index;
					});

				submesh.IndexCount = static_cast<uint32_t>(accessor.count);
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Nodes
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	m_Nodes.emplace_back();
	m_Nodes[0].Name   = m_Name;
	m_Nodes[0].Parent = UINT32_MAX;

	const uint32_t nodeOffset = 1;

	m_Nodes.resize(asset.nodes.size() + 1);

	for (size_t i = 0; i < asset.nodes.size(); i++)
	{
		const fastgltf::Node& gltfNode = asset.nodes[i];
		Node& node = m_Nodes[nodeOffset + i];

		node.Name = gltfNode.name;

		ExtractTRS(gltfNode, node);
		node.LocalTransform = NodeToMatrix(gltfNode);

		for (size_t childIndex : gltfNode.children)
			node.Children.push_back(static_cast<uint32_t>(nodeOffset + childIndex));

		if (gltfNode.meshIndex.has_value())
		{
			uint32_t submeshIndex = 0;
			for (size_t m = 0; m < gltfNode.meshIndex.value(); m++)
				submeshIndex += static_cast<uint32_t>(asset.meshes[m].primitives.size());

			const fastgltf::Mesh& gltfMesh = asset.meshes[gltfNode.meshIndex.value()];
			for (size_t p = 0; p < gltfMesh.primitives.size(); p++)
				node.Submeshes.push_back(submeshIndex + static_cast<uint32_t>(p));
		}
	}

	// Wire top-level scene nodes to the synthetic root.
	if (!asset.scenes.empty())
	{
		const fastgltf::Scene& scene = asset.scenes[asset.defaultScene.value_or(0)];

		for (size_t nodeIndex : scene.nodeIndices)
		{
			const uint32_t mappedIndex = nodeOffset + static_cast<uint32_t>(nodeIndex);
			m_Nodes[0].Children.push_back(mappedIndex);
			m_Nodes[mappedIndex].Parent = 0;
		}
	}

	for (size_t i = 0; i < asset.nodes.size(); i++)
	{
		for (uint32_t childIndex : m_Nodes[nodeOffset + i].Children)
			m_Nodes[childIndex].Parent = nodeOffset + static_cast<uint32_t>(i);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// World Transforms
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TraverseNodes([&](const Node& node)
	{
		const uint32_t index = static_cast<uint32_t>(&node - m_Nodes.data());

		m_Nodes[index].WorldTransform = node.IsRoot()
			? node.LocalTransform
			: m_Nodes[node.Parent].WorldTransform * node.LocalTransform;
	});

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// GPU Upload
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	if (!m_Vertices.empty())
		m_VertexBuffer.Create(m_Vertices.data(), m_Vertices.size() * sizeof(Vertex));

	if (!m_Indices.empty())
		m_IndexBuffer.Create(m_Indices.data(), m_Indices.size() * sizeof(uint32_t));

	std::println("[Mesh] Loaded '{}' - {} vertices, {} indices, {} submeshes, {} nodes",
		m_Name,
		m_Vertices.size(),
		m_Indices.size(),
		m_Submeshes.size(),
		m_Nodes.size());

	return true;
}

void Mesh::Destroy()
{
	m_VertexBuffer.Destroy();
	m_IndexBuffer.Destroy();

	m_Vertices.clear();
	m_Indices.clear();
	m_Submeshes.clear();
	m_Nodes.clear();

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
