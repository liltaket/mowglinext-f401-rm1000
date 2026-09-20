cmake_minimum_required(VERSION 3.8)
project(mowgli_gnss_bridge)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rtcm_msgs REQUIRED)
find_package(mowgli_interfaces REQUIRED)
find_package(universal_gnss_ros2 REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Library: mowgli_gnss_bridge_lib
# Split from main.cpp so GTest can link the node without an executable entry.
# ---------------------------------------------------------------------------

add_library(mowgli_gnss_bridge_lib STATIC
  src/universal_gnss_topic_bridge.cpp
)

add_library(rtcm3_framer_lib STATIC
  src/rtcm3_framer.cpp
)

add_library(rtcm_tcp_client_lib STATIC
  src/rtcm_tcp_client.cpp
)

target_include_directories(rtcm_tcp_client_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(rtcm_tcp_client_lib
  rtcm3_framer_lib
)

target_include_directories(rtcm3_framer_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_include_directories(mowgli_gnss_bridge_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(mowgli_gnss_bridge_lib
  rclcpp
  std_msgs
  builtin_interfaces
  diagnostic_msgs
  rtcm_msgs
  mowgli_interfaces
  universal_gnss_ros2
)

# ---------------------------------------------------------------------------
# Executable: universal_gnss_topic_bridge
# ---------------------------------------------------------------------------

add_executable(universal_gnss_topic_bridge
  src/main.cpp
)

target_link_libraries(universal_gnss_topic_bridge
  mowgli_gnss_bridge_lib
)

ament_target_dependencies(universal_gnss_topic_bridge
  rclcpp
)

# A generic external correction source. It deliberately publishes the same
# internal topic as ntrip_node, but start_gps.sh launches exactly one source.
add_executable(rtcm_tcp_source
  src/rtcm_tcp_source.cpp
)

target_link_libraries(rtcm_tcp_source
  rtcm3_framer_lib
  rtcm_tcp_client_lib
  Threads::Threads
)

ament_target_dependencies(rtcm_tcp_source
  rclcpp
  universal_gnss_ros2
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS
    universal_gnss_topic_bridge
    rtcm_tcp_source
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_universal_gnss_topic_bridge
    test/test_universal_gnss_topic_bridge.cpp
  )
  target_link_libraries(test_universal_gnss_topic_bridge
    mowgli_gnss_bridge_lib
  )
  ament_target_dependencies(test_universal_gnss_topic_bridge
    rclcpp
    mowgli_interfaces
    universal_gnss_ros2
    diagnostic_msgs
    rtcm_msgs
  )

  ament_add_gtest(test_rtcm3_framer
    test/test_rtcm3_framer.cpp
  )
  target_link_libraries(test_rtcm3_framer
    rtcm3_framer_lib
  )

  ament_add_gtest(test_rtcm_tcp_client
    test/test_rtcm_tcp_client.cpp
  )
  target_link_libraries(test_rtcm_tcp_client
    rtcm_tcp_client_lib
    Threads::Threads
  )
endif()

ament_package()
