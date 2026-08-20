# ==== SDL ====
set(SDL_SHARED       OFF)
set(SDL_STATIC       ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS        OFF)
set(SDL_EXAMPLES     OFF)
set(SDL_INSTALL      OFF)
set(SDL_INSTALL_DOCS OFF)

set(SDL_AUDIO        OFF)
set(SDL_CAMERA       OFF)
set(SDL_SENSOR       OFF)
set(SDL_POWER        OFF)
set(SDL_DIALOG       ON)
set(SDL_TRAY         OFF)
set(SDL_GPU          OFF)
set(SDL_RENDER       OFF)
set(SDL_OPENGL       OFF)
set(SDL_OPENGLES     OFF)
set(SDL_KMSDRM       OFF)
set(SDL_HAPTIC       OFF)

set(SDL_VULKAN       ON)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/SDL3 SYSTEM)

target_compile_definitions(SDL3-static INTERFACE SDL_MAIN_HANDLED)

# ==== GLM (Header-only library) ====
set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_TESTS   OFF)
set(GLM_BUILD_INSTALL OFF)
set(GLM_ENABLE_CXX_20 ON)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/glm SYSTEM)

target_compile_definitions(glm-header-only INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)

# ==== Slang ====
add_library(slang INTERFACE)
add_library(slang::slang ALIAS slang)
target_include_directories(slang INTERFACE "$ENV{VULKAN_SDK}/Include/slang")
find_library(SLANG_LIB NAMES slang slang.lib
        HINTS "$ENV{VULKAN_SDK}/Lib" REQUIRED)
target_link_libraries(slang INTERFACE ${SLANG_LIB}
)

# ==== fastgltf ====
set(FASTGLTF_COMPILE_AS_CPP20 ON)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/fastgltf SYSTEM)
