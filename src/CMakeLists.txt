find_package(Vulkan REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

add_executable(VulkanRayTracer
    ${SOURCES}
)

target_compile_definitions(VulkanRayTracer PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(VulkanRayTracer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(
    VulkanRayTracer PRIVATE
    VulkanMemoryAllocator
    glm
    Vulkan::Vulkan
    fastgltf
    stb_image
    SDL3::SDL3
    vk-bootstrap
    imgui
    VulkanMemoryAllocator
)

if(WIN32)
    add_custom_command(TARGET VulkanRayTracer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_RUNTIME_DLLS:VulkanRayTracer>
            $<TARGET_FILE_DIR:VulkanRayTracer>
        COMMAND_EXPAND_LISTS
    )
endif()