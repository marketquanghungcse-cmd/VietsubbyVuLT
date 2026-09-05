cmake_minimum_required(VERSION 3.15)
project(VideoDubberPro CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(SOURCES
    src/main.cpp
    src/core/Config.cpp
    src/core/ApiKeyPool.cpp
    src/core/HttpClient.cpp
    src/stt/SrtParser.cpp
    src/downloader/VideoDownloader.cpp
    src/stt/WhisperTranscriber.cpp
    src/translator/AiTranslator.cpp
    src/tts/TtsEngine.cpp
    src/audio_separator/VocalSeparator.cpp
    src/video_processor/SubBlurrer.cpp
    src/core/PipelineManager.cpp
    src/core/ProcessRunner.cpp
    src/ui/HttpGuiServer.cpp
)

add_definitions(-DNOMINMAX)
add_executable(VideoDubberPro ${SOURCES})

target_link_libraries(VideoDubberPro PRIVATE
    ws2_32
    winhttp
    shell32
)

# Set UTF-8 execution charset on MSVC
if(MSVC)
    target_compile_options(VideoDubberPro PRIVATE /utf-8)
endif()