cmake_minimum_required(VERSION 3.21)
project(VisionMaster_CPP VERSION 4.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(OpenCV REQUIRED)

set(SOURCES
    src/main.cpp
    src/VisionEngine.cpp
    src/PlcClient.cpp
    src/FlowCanvas.cpp
    src/ImageViewport.cpp
    src/MainWindow.cpp
)

set(HEADERS
    src/VisionTypes.h
    src/VisionEngine.h
    src/PlcClient.h
    src/FlowCanvas.h
    src/ImageViewport.h
    src/MainWindow.h
)

if(WIN32)
    add_executable(VisionMaster_CPP WIN32 ${SOURCES} ${HEADERS})
    target_compile_definitions(VisionMaster_CPP PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(VisionMaster_CPP PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        ${OpenCV_LIBS}
        ws2_32
    )
else()
    add_executable(VisionMaster_CPP ${SOURCES} ${HEADERS})
    target_link_libraries(VisionMaster_CPP PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        ${OpenCV_LIBS}
    )
endif()

target_include_directories(VisionMaster_CPP PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS})
