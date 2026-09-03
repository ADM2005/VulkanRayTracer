find_package(Vulkan REQUIRED)

add_subdirectory(vma)
add_subdirectory(vk-bootstrap)
add_subdirectory(SDL)
add_subdirectory(glm)
add_subdirectory(fastgltf)

add_library(imgui STATIC)
target_include_directories(imgui 
	PRIVATE 
		${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends

	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_sources(imgui PRIVATE
    imgui/imgui.h
	
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp

    imgui/backends/imgui_impl_sdl3.h
    imgui/backends/imgui_impl_vulkan.h

    imgui/backends/imgui_impl_sdl3.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)

target_link_libraries(imgui PRIVATE Vulkan::Vulkan SDL3::SDL3)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE stb_image)