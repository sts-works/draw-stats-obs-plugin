cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_OBS_STANDALONE_CLIENT "Enable the standalone Draw Stats OBS client by default" ON)
option(ENABLE_OBS_QA_MODE "Enable the local-only standalone client QA session" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_library(${CMAKE_PROJECT_NAME} MODULE)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    DRAW_STATS_ENABLE_OBS_QA_MODE=$<BOOL:${ENABLE_OBS_QA_MODE}>
    DRAW_STATS_OBS_STANDALONE_CLIENT_DEFAULT_ENABLED=$<BOOL:${ENABLE_OBS_STANDALONE_CLIENT}>
)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets Qt6::Network
)
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/obs_standalone_client.cpp
    src/obs_standalone_client.hpp
    src/obs_standalone_dock.cpp
    src/obs_standalone_dock.hpp
    src/obs_standalone_feature_flag.cpp
    src/obs_standalone_feature_flag.hpp
    src/obs_standalone_overlay_server.cpp
    src/obs_standalone_overlay_server.hpp
    src/obs_standalone_security.cpp
    src/obs_standalone_security.hpp
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

if(OS_MACOS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/obs_standalone_security_macos.mm)
  add_executable(draw-stats-obs-input-helper helpers/macos/obs_standalone_input_helper.mm)
  target_compile_features(draw-stats-obs-input-helper PRIVATE cxx_std_17)
  target_compile_options(draw-stats-obs-input-helper PRIVATE -fobjc-arc)
  target_link_libraries(
    draw-stats-obs-input-helper
    PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Foundation"
  )
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework Security" "-framework LocalAuthentication")
elseif(OS_WINDOWS)
  add_executable(draw-stats-obs-input-helper WIN32 helpers/windows/obs_standalone_input_helper.cpp)
  target_compile_features(draw-stats-obs-input-helper PRIVATE cxx_std_17)
  target_link_libraries(draw-stats-obs-input-helper PRIVATE user32 shell32 advapi32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE advapi32)
endif()

if(TARGET draw-stats-obs-input-helper)
  add_dependencies(${CMAKE_PROJECT_NAME} draw-stats-obs-input-helper)
  if(OS_MACOS)
    add_custom_command(
      TARGET ${CMAKE_PROJECT_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>/Contents/Resources/bin"
      COMMAND
        ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:draw-stats-obs-input-helper>"
        "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>/Contents/Resources/bin/draw-stats-obs-input-helper"
      COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>/Contents/Resources/qt-plugins/tls"
      COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-universal/plugins/tls"
        "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>/Contents/Resources/qt-plugins/tls"
    )
  elseif(OS_WINDOWS)
    install(TARGETS draw-stats-obs-input-helper RUNTIME DESTINATION "${CMAKE_PROJECT_NAME}/data/bin" COMPONENT Runtime)
    install(
      FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-x64/plugins/tls/qcertonlybackend.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-x64/plugins/tls/qschannelbackend.dll"
      DESTINATION "${CMAKE_PROJECT_NAME}/data/qt-plugins/tls"
      COMPONENT Runtime
    )
    add_custom_command(
      TARGET ${CMAKE_PROJECT_NAME}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/bin"
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:draw-stats-obs-input-helper>"
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/bin/draw-stats-obs-input-helper.exe"
      COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/qt-plugins/tls"
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-x64/plugins/tls/qcertonlybackend.dll"
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/qt-plugins/tls/qcertonlybackend.dll"
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-x64/plugins/tls/qschannelbackend.dll"
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/qt-plugins/tls/qschannelbackend.dll"
    )
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

include(CTest)
if(BUILD_TESTING)
  add_executable(
    obs-standalone-contract-test
    tests/obs_standalone_contract_test.cpp
    src/obs_standalone_client.cpp
    src/obs_standalone_client.hpp
    src/obs_standalone_feature_flag.cpp
    src/obs_standalone_security.cpp
    src/obs_standalone_overlay_server.cpp
    src/obs_standalone_overlay_server.hpp
  )
  target_compile_features(obs-standalone-contract-test PRIVATE cxx_std_17)
  set_target_properties(obs-standalone-contract-test PROPERTIES AUTOMOC ON)
  target_compile_definitions(
    obs-standalone-contract-test
    PRIVATE
      DRAW_STATS_ENABLE_OBS_QA_MODE=$<BOOL:${ENABLE_OBS_QA_MODE}>
      DRAW_STATS_OBS_STANDALONE_CLIENT_DEFAULT_ENABLED=$<BOOL:${ENABLE_OBS_STANDALONE_CLIENT}>
  )
  get_target_property(obs_libobs_include_directories OBS::libobs INTERFACE_INCLUDE_DIRECTORIES)
  target_include_directories(obs-standalone-contract-test PRIVATE ${obs_libobs_include_directories})
  target_link_libraries(obs-standalone-contract-test PRIVATE Qt6::Core Qt6::Network Qt6::Widgets)
  target_include_directories(obs-standalone-contract-test PRIVATE src)
  if(OS_MACOS)
    set_target_properties(
      obs-standalone-contract-test
      PROPERTIES XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME NO
    )
    target_link_options(
      obs-standalone-contract-test
      PRIVATE "LINKER:-rpath,${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-universal/lib"
    )
    add_custom_command(
      TARGET obs-standalone-contract-test
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Frameworks"
      COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-universal/lib/QtCore.framework"
        "${CMAKE_BINARY_DIR}/Frameworks/QtCore.framework"
      COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-universal/lib/QtGui.framework"
        "${CMAKE_BINARY_DIR}/Frameworks/QtGui.framework"
      COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-universal/lib/QtWidgets.framework"
        "${CMAKE_BINARY_DIR}/Frameworks/QtWidgets.framework"
    )
  elseif(OS_WINDOWS)
    add_custom_command(
      TARGET obs-standalone-contract-test
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Core>"
        "$<TARGET_FILE_DIR:obs-standalone-contract-test>"
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Network>"
        "$<TARGET_FILE_DIR:obs-standalone-contract-test>"
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Gui>"
        "$<TARGET_FILE_DIR:obs-standalone-contract-test>"
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Widgets>"
        "$<TARGET_FILE_DIR:obs-standalone-contract-test>"
    )
  endif()
  add_test(NAME obs-standalone-contract-test COMMAND obs-standalone-contract-test)
  if(OS_MACOS)
    set_tests_properties(
      obs-standalone-contract-test
      PROPERTIES
        ENVIRONMENT "DYLD_FRAMEWORK_PATH=${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-2025-08-23-universal/lib"
    )
  endif()
endif()
