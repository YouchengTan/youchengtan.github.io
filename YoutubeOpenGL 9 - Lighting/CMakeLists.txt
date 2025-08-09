cmake_minimum_required(VERSION 3.16)
project(YoutubeOpenGL9Lighting)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC
    src/Main.cpp
    src/Jelly.cpp
    src/shaderClass.cpp
    src/Camera.cpp
    src/VAO.cpp
    src/VBO.cpp
    src/EBO.cpp
    src/Texture.cpp
    src/stb.cpp
    src/glad.c
)

add_executable(YoutubeOpenGL9Lighting ${SRC})

target_include_directories(YoutubeOpenGL9Lighting PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include
)

find_package(glfw3 REQUIRED)
find_package(glm QUIET)

target_link_libraries(YoutubeOpenGL9Lighting PRIVATE glfw)
target_link_libraries(YoutubeOpenGL9Lighting PRIVATE
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
)

add_custom_command(TARGET YoutubeOpenGL9Lighting POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/default.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/default.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/light.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/light.frag
        $<TARGET_FILE_DIR:YoutubeOpenGL9Lighting>
)
