cmake_minimum_required(VERSION 3.10)
project(vrm2mdl VERSION 4.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Binary output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Common include directories
include_directories(include .)

# Platform specific flags
if(MSVC)
    add_compile_options(/W4 /O2 /EHsc)
else()
    add_compile_options(-Wall -Wextra -O3 -fPIC)
endif()

# Core VRM library
add_library(vrm_core SHARED main.cpp)
set_target_properties(vrm_core PROPERTIES 
    OUTPUT_NAME vrm_core
    VERSION ${PROJECT_VERSION}
)
target_compile_definitions(vrm_core PRIVATE VRM2MDL_SHARED)

# Assimp core (Optional)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assimp_core/main.cpp)
    find_package(assimp QUIET)
    
    # Manual fallback for common install paths if find_package fails
    if(NOT (assimp_FOUND OR ASSIMP_FOUND))
        set(ASSIMP_ROOT "C:/Program Files/Assimp")
        if(EXISTS ${ASSIMP_ROOT})
            set(ASSIMP_INCLUDE_DIRS "${ASSIMP_ROOT}/include")
            file(GLOB ASSIMP_LIBRARIES "${ASSIMP_ROOT}/lib/x64/assimp*.lib")
            if(ASSIMP_LIBRARIES)
                set(ASSIMP_FOUND TRUE)
                message(STATUS "Assimp found manually at ${ASSIMP_ROOT}")
            endif()
        endif()
    endif()

    if(assimp_FOUND OR ASSIMP_FOUND)
        add_library(assimp_core SHARED assimp_core/main.cpp)
        set_target_properties(assimp_core PROPERTIES 
            OUTPUT_NAME assimp_core
        )
        target_include_directories(assimp_core PRIVATE include . ${ASSIMP_INCLUDE_DIRS})
        if(TARGET assimp::assimp)
            target_link_libraries(assimp_core PRIVATE assimp::assimp)
        else()
            target_link_libraries(assimp_core PRIVATE ${ASSIMP_LIBRARIES})
        endif()
    else()
        message(STATUS "Assimp not found. Skipping assimp_core.")
    endif()
endif()

# Installation rules
install(TARGETS vrm_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
if(TARGET assimp_core)
    install(TARGETS assimp_core
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()
