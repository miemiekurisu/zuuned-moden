cmake_minimum_required(VERSION 3.21)
project(DeviceMusicImportTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)
qt_standard_project_setup(REQUIRES 6.8)

set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../..")
qt_add_executable(device-music-import backend.cpp
    "${repo}/src/TrackImportBatch.h"
    "${repo}/src/TrackImportPath.h")
target_include_directories(device-music-import PRIVATE "${repo}/src")
target_link_libraries(device-music-import PRIVATE Qt6::Core)

qt_add_executable(device-album-art album-art.cpp
    "${repo}/src/library/AlbumArtService.cpp"
    "${repo}/src/library/AlbumArtService.h")
target_include_directories(device-album-art PRIVATE
    "${repo}/src/library" "${repo}/transcode")
target_link_libraries(device-album-art PRIVATE Qt6::Core Qt6::Gui)

# Album-art source precedence, gated against the real resolution order. The
# order is a file-static function, so this target compiles the service source
# rather than linking it - same shape as the September audit fixture it retires.
qt_add_executable(album-art-precedence album-art-precedence.cpp
    "${repo}/src/library/AlbumArtService.h")
target_include_directories(album-art-precedence PRIVATE
    "${repo}/src/library" "${repo}/transcode")
target_link_libraries(album-art-precedence PRIVATE Qt6::Core Qt6::Gui)
