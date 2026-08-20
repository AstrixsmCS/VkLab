cmake_minimum_required(VERSION 3.25...4.0 FATAL_ERROR)

project(VkLab VERSION 0.1.0 LANGUAGES CXX)

# C++ Configuration
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(Vulkan REQUIRED COMPONENTS volk)

# Source Files

file(GLOB_RECURSE VKLAB_SOURCES CONFIGURE_DEPENDS
		Source/*.cpp
		Source/*.hpp
)

# Target
add_executable(VkLab ${VKLAB_SOURCES})

# Compiler Configuration
if(MSVC)
	target_compile_options(VkLab PRIVATE
			/W4
			/MP
			/Gy
			/Zc:preprocessor
	)
else()
	target_compile_options(VkLab PRIVATE
			-Wall
			-Wextra
			-Wpedantic
	)
endif()

# ==== Add Subsystems ====
add_subdirectory(ThirdParty)

# Libraries
target_link_libraries(VkLab PRIVATE
		SDL3::SDL3-static
		glm::glm-header-only
		Vulkan::Vulkan
		Vulkan::volk
		slang::slang
		fastgltf::fastgltf
)
