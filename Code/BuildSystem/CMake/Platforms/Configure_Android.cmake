include("${CMAKE_CURRENT_LIST_DIR}/Configure_Default.cmake")

message(STATUS "Configuring Platform: Android")

set_property(GLOBAL PROPERTY EZ_CMAKE_PLATFORM_ANDROID ON)
set_property(GLOBAL PROPERTY EZ_CMAKE_PLATFORM_POSIX ON)
set_property(GLOBAL PROPERTY EZ_CMAKE_PLATFORM_SUPPORTS_VULKAN ON)

macro(ez_platform_pull_properties)

	get_property(EZ_CMAKE_PLATFORM_ANDROID GLOBAL PROPERTY EZ_CMAKE_PLATFORM_ANDROID)

endmacro()

macro(ez_platform_detect_generator)

	if(CMAKE_GENERATOR MATCHES "Ninja" OR CMAKE_GENERATOR MATCHES "Ninja Multi-Config")
		message(STATUS "Buildsystem is Ninja (EZ_CMAKE_GENERATOR_NINJA)")

		set_property(GLOBAL PROPERTY EZ_CMAKE_GENERATOR_NINJA ON)
		set_property(GLOBAL PROPERTY EZ_CMAKE_GENERATOR_PREFIX "Ninja")
		set_property(GLOBAL PROPERTY EZ_CMAKE_GENERATOR_CONFIGURATION ${CMAKE_BUILD_TYPE})

	else()
		message(FATAL_ERROR "Generator '${CMAKE_GENERATOR}' is not supported on Android! Please extend ez_platform_detect_generator()")
	endif()

endmacro()

macro(ez_platformhook_link_target_vulkan)

	# on linux is the loader a dll
	get_target_property(_dll_location EzVulkan::Loader IMPORTED_LOCATION)

	if(NOT _dll_location STREQUAL "")
		add_custom_command(TARGET ${TARGET_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:EzVulkan::Loader> $<TARGET_FILE_DIR:${TARGET_NAME}>)
	endif()

	unset(_dll_location)

endmacro()

macro(ez_platformhook_find_vulkan)
	if(EZ_CMAKE_ARCHITECTURE_64BIT)
		if((EZ_VULKAN_DIR STREQUAL "EZ_VULKAN_DIR-NOTFOUND") OR(EZ_VULKAN_DIR STREQUAL ""))
			set(CMAKE_FIND_DEBUG_MODE TRUE)
			unset(EZ_VULKAN_DIR CACHE)
			unset(EzVulkan_DIR CACHE)
			find_path(EZ_VULKAN_DIR config/vk_layer_settings.txt
					PATHS
					${EZ_VULKAN_DIR}
					$ENV{VULKAN_SDK}
			)

			if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
				if((EZ_VULKAN_DIR STREQUAL "EZ_VULKAN_DIR-NOTFOUND") OR (EZ_VULKAN_DIR STREQUAL ""))
					ez_download_and_extract("${EZ_CONFIG_VULKAN_SDK_LINUXX64_URL}" "${CMAKE_BINARY_DIR}/vulkan-sdk" "vulkan-sdk-${EZ_CONFIG_VULKAN_SDK_LINUXX64_VERSION}")
					set(EZ_VULKAN_DIR "${CMAKE_BINARY_DIR}/vulkan-sdk/${EZ_CONFIG_VULKAN_SDK_LINUXX64_VERSION}" CACHE PATH "Directory of the Vulkan SDK" FORCE)

					find_path(EZ_VULKAN_DIR config/vk_layer_settings.txt
							PATHS
							${EZ_VULKAN_DIR}
							$ENV{VULKAN_SDK}
					)
				endif()
			else ()
				message(FATAL_ERROR "Can't find Vulkan SDK, please set EZ_VULKAN_DIR to point to the folder containing 'config/vk_layer_settings.txt'")
			endif ()

			if((EZ_VULKAN_DIR STREQUAL "EZ_VULKAN_DIR-NOTFOUND") OR (EZ_VULKAN_DIR STREQUAL ""))
				message(FATAL_ERROR "Failed to find vulkan SDK. Ez requires the vulkan sdk ${EZ_CONFIG_VULKAN_SDK_LINUXX64_VERSION}. Please set the environment variable VULKAN_SDK to the vulkan sdk location.")
			endif()

			# set(CMAKE_FIND_DEBUG_MODE FALSE)
		endif()
	else()
		message(FATAL_ERROR "TODO: Vulkan is not yet supported on this platform and/or architecture.")
	endif()

	include(FindPackageHandleStandardArgs)
	find_package_handle_standard_args(EzVulkan DEFAULT_MSG EZ_VULKAN_DIR)

	if(NOT ANDROID_NDK)
		message(WARNING "ANDROID_NDK not set")

		if(NOT EXISTS "$ENV{ANDROID_NDK_HOME}")
			message(FATAL_ERROR "ANDROID_NDK_HOME environment variable not set. Please ensure it points to the android NDK root folder.")
		else()
			set(ANDROID_NDK $ENV{ANDROID_NDK_HOME})
		endif()
	endif()

	if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
		set(EZ_VULKAN_INCLUDE_DIR "${EZ_VULKAN_DIR}/x86_64/include")
	elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
		set(EZ_VULKAN_INCLUDE_DIR "${EZ_VULKAN_DIR}/Include")
	else ()
		message(FATAL_ERROR "Unknown host system, can't find vulkan include dir.")
	endif ()

	if(EZ_CMAKE_ARCHITECTURE_64BIT)
		if(EZ_CMAKE_ARCHITECTURE_ARM)
			add_library(EzVulkan::Loader SHARED IMPORTED)
			set_target_properties(EzVulkan::Loader PROPERTIES IMPORTED_LOCATION "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-android/${ANDROID_PLATFORM}/libvulkan.so")
			set_target_properties(EzVulkan::Loader PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EZ_VULKAN_INCLUDE_DIR}")
		#set_target_properties(EzVulkan::Loader PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EZ_VULKAN_DIR}/x86_64/include")
		elseif(EZ_CMAKE_ARCHITECTURE_X86)
			add_library(EzVulkan::Loader SHARED IMPORTED)
			set_target_properties(EzVulkan::Loader PROPERTIES IMPORTED_LOCATION "${CMAKE_SYSROOT}/usr/lib/x86_64-linux-android/${ANDROID_PLATFORM}/libvulkan.so")
			set_target_properties(EzVulkan::Loader PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EZ_VULKAN_INCLUDE_DIR}")
		endif()
		add_library(EzVulkan::DXC SHARED IMPORTED)
		#set_target_properties(EzVulkan::DXC PROPERTIES IMPORTED_LOCATION "${EZ_VULKAN_DIR}/x86_64/lib/libdxcompiler.so")
		#set_target_properties(EzVulkan::DXC PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EZ_VULKAN_DIR}/x86_64/include")
	else()
		message(FATAL_ERROR "TODO: Vulkan is not yet supported on this platform and/or architecture.")
	endif()

endmacro()