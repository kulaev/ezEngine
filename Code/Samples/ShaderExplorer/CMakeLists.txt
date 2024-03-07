ez_cmake_init()

ez_requires_renderer()

# Get the name of this folder as the project name
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME_WE)

ez_create_target(APPLICATION ${PROJECT_NAME})

#TODO: Add actual packaging code. This is done in PRE_BUILD so that it happens before the
#apk gen steps that happen in POST_BUILD and which are already done via ez_create_target.
add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${EZ_ROOT}/Output/Bin/WinNinja2022Debug64/ShaderCache/VULKAN/ ${CMAKE_CURRENT_BINARY_DIR}/package/Assets/ShaderCache/VULKAN/)
add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${EZ_ROOT}/Data/Samples/ShaderExplorer/ ${CMAKE_CURRENT_BINARY_DIR}/package/Assets/Data/Samples/ShaderExplorer/)
add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${EZ_ROOT}/Data/Base/Shaders/ ${CMAKE_CURRENT_BINARY_DIR}/package/Assets/Data/Base/Shaders/)

ez_add_renderers(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
  PUBLIC
  RendererCore
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
    RendererVulkan
)

target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
ez_link_target_vulkan(${PROJECT_NAME})

add_dependencies(${PROJECT_NAME}
  InspectorPlugin
)
