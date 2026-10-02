include(cmake/EngineSimSDL.cmake)
engine_sim_require_sdl3()

add_executable(engine-sim-desktop
    src/desktop_main.cpp
    src/desktop_platform_sdl.cpp
    src/sdl_audio_util.cpp
    src/sdl_audio_output.cpp
    src/sdl_gpu_renderer.cpp)
target_link_libraries(engine-sim-desktop PRIVATE SDL3::SDL3 engine-sim-visualization)
target_compile_features(engine-sim-desktop PRIVATE cxx_std_17)
target_compile_definitions(engine-sim-desktop PRIVATE
    ENGINE_SIM_SHADER_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/shaders"
    ENGINE_SIM_SOURCE_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(ENGINE_SIM_BUILD_SCRIPTING)
    target_link_libraries(engine-sim-desktop PRIVATE engine-sim-scripting)
    target_compile_definitions(engine-sim-desktop PRIVATE ATG_ENGINE_SIM_PIRANHA_ENABLED)
endif()

# A separate native host: no window, renderer, or display event loop.
if(UNIX AND ENGINE_SIM_BUILD_SCRIPTING)
    add_executable(engine-sim-audio
        src/audio_main.cpp src/audio_engine_runner.cpp src/audio_tui.cpp src/runtime_paths.cpp
        src/sdl_audio_util.cpp src/sdl_audio_output.cpp)
    target_link_libraries(engine-sim-audio PRIVATE SDL3::SDL3 engine-sim-scripting engine-sim::core)
    target_compile_definitions(engine-sim-audio PRIVATE
        ENGINE_SIM_SOURCE_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    if(APPLE)
        set_target_properties(engine-sim-audio PROPERTIES INSTALL_RPATH "@executable_path")
    else()
        set_target_properties(engine-sim-audio PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()
endif()

if(APPLE AND ENGINE_SIM_BUILD_SCRIPTING)
    enable_language(OBJCXX)
    find_program(ENGINE_SIM_XCRUN xcrun REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sound_ui.metallib"
        COMMAND "${ENGINE_SIM_XCRUN}" -sdk macosx metal -c "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/sound_ui.metal"
            -o "${CMAKE_CURRENT_BINARY_DIR}/sound_ui.air"
        COMMAND "${ENGINE_SIM_XCRUN}" -sdk macosx metallib "${CMAKE_CURRENT_BINARY_DIR}/sound_ui.air"
            -o "${CMAKE_CURRENT_BINARY_DIR}/sound_ui.metallib"
        DEPENDS assets/shaders/sound_ui.metal VERBATIM)
    add_custom_target(engine-sim-sound-shaders DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/sound_ui.metallib")
    add_executable(engine-sim-sound MACOSX_BUNDLE
        src/sound_mac.mm src/sound_metal.mm src/authored_mesh_library.cpp src/sound_session.cpp src/sound_verify.cpp src/engine_catalog.cpp
        src/audio_engine_runner.cpp src/runtime_paths.cpp
        src/sdl_audio_util.cpp src/sdl_audio_output.cpp)
    target_link_libraries(engine-sim-sound PRIVATE
        SDL3::SDL3 engine-sim-scripting engine-sim::core "-framework Cocoa" "-framework Metal" "-framework QuartzCore" "-framework CoreText" "-framework CoreVideo")
    add_dependencies(engine-sim-sound engine-sim-sound-shaders)
    target_compile_options(engine-sim-sound PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_include_directories(engine-sim-sound PRIVATE "${ENGINE_SIM_GENERATED_INCLUDE_DIRECTORY}")
    target_compile_definitions(engine-sim-sound PRIVATE
        ENGINE_SIM_SOURCE_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        ENGINE_SIM_SOUND_METALLIB="${CMAKE_CURRENT_BINARY_DIR}/sound_ui.metallib")
    configure_file(cmake/SoundInfo.plist.in "${CMAKE_CURRENT_BINARY_DIR}/SoundInfo.plist" @ONLY)
    set_target_properties(engine-sim-sound PROPERTIES
        OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES
        INSTALL_RPATH "@executable_path"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/SoundInfo.plist")
    install(TARGETS engine-sim-sound BUNDLE DESTINATION .)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "engine-sim-sound.app/Contents/Resources/assets" PATTERN ".DS_Store" EXCLUDE)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sound_ui.metallib"
        DESTINATION "engine-sim-sound.app/Contents/Resources/assets/shaders")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "engine-sim-sound.app/Contents/Resources")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "engine-sim-sound.app/Contents/Resources")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/"
        DESTINATION "engine-sim-sound.app/Contents/Resources/licenses")
    install(CODE [[
        set(sound_runtime "${CMAKE_INSTALL_PREFIX}/engine-sim-sound.app/Contents/MacOS")
        file(REAL_PATH "$<TARGET_FILE:SDL3::SDL3>" sound_sdl_source)
        file(INSTALL DESTINATION "${sound_runtime}" TYPE FILE
            FILES "${sound_sdl_source}" RENAME "$<TARGET_FILE_NAME:SDL3::SDL3>")
        execute_process(COMMAND otool -D "${sound_sdl_source}" OUTPUT_VARIABLE sound_sdl_names COMMAND_ERROR_IS_FATAL ANY)
        string(REGEX MATCH "\n([^\n]+)" sound_sdl_match "${sound_sdl_names}")
        execute_process(COMMAND install_name_tool -change "${CMAKE_MATCH_1}"
            "@executable_path/$<TARGET_FILE_NAME:SDL3::SDL3>" "${sound_runtime}/engine-sim-sound"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND install_name_tool -id "@rpath/$<TARGET_FILE_NAME:SDL3::SDL3>"
            "${sound_runtime}/$<TARGET_FILE_NAME:SDL3::SDL3>" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND codesign --force --deep --sign - "${CMAKE_INSTALL_PREFIX}/engine-sim-sound.app"
            COMMAND_ERROR_IS_FATAL ANY)
    ]])
endif()

engine_sim_add_shader_artifacts(engine-sim-shaders
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/engine_sim.hlsl"
    "${CMAKE_CURRENT_BINARY_DIR}/shaders")
if(TARGET engine-sim-shaders)
    add_dependencies(engine-sim-desktop engine-sim-shaders)
endif()

if(APPLE)
    set_target_properties(engine-sim-desktop PROPERTIES INSTALL_RPATH "@executable_path")
elseif(UNIX)
    set_target_properties(engine-sim-desktop PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

if(APPLE AND ENGINE_SIM_MACOS_APP_BUNDLE)
    set(ENGINE_SIM_MACOS_BUNDLE_IDENTIFIER "org.openenginesim.desktop")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY)
    set_target_properties(engine-sim-desktop PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        OUTPUT_NAME "engine-sim")
    set(ENGINE_SIM_INSTALL_ASSET_DIRECTORY "engine-sim.app/Contents/Resources/assets")
    set(ENGINE_SIM_INSTALL_RUNTIME_DIRECTORY "engine-sim.app/Contents/MacOS")
    install(TARGETS engine-sim-desktop BUNDLE DESTINATION .)
else()
    set(ENGINE_SIM_INSTALL_ASSET_DIRECTORY "assets")
    set(ENGINE_SIM_INSTALL_RUNTIME_DIRECTORY "bin")
    install(TARGETS engine-sim-desktop RUNTIME DESTINATION "${ENGINE_SIM_INSTALL_RUNTIME_DIRECTORY}")
endif()

# Homebrew's imported target may be a symlink into its Cellar. Package the
# actual library bytes so the application doesn't inherit a broken symlink.
install(CODE "
    file(REAL_PATH \"$<TARGET_FILE:SDL3::SDL3>\" sdl_library_source)
    set(sdl_library_destination \"\${CMAKE_INSTALL_PREFIX}/${ENGINE_SIM_INSTALL_RUNTIME_DIRECTORY}/$<TARGET_FILE_NAME:SDL3::SDL3>\")
    file(REMOVE \"\${sdl_library_destination}\")
    file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${ENGINE_SIM_INSTALL_RUNTIME_DIRECTORY}\"
        TYPE FILE FILES \"\${sdl_library_source}\" RENAME \"$<TARGET_FILE_NAME:SDL3::SDL3>\")
")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/" DESTINATION "${ENGINE_SIM_INSTALL_ASSET_DIRECTORY}"
    PATTERN ".DS_Store" EXCLUDE)
if(TARGET engine-sim-audio)
    install(TARGETS engine-sim-audio RUNTIME DESTINATION "${ENGINE_SIM_INSTALL_RUNTIME_DIRECTORY}")
endif()
if(TARGET engine-sim-shaders)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/engine_sim.vertex.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/engine_sim.fragment.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/engine_sim.vertex.dxil"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/engine_sim.fragment.dxil"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/engine_sim.vertex.msl"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/engine_sim.fragment.msl"
        DESTINATION "${ENGINE_SIM_INSTALL_ASSET_DIRECTORY}/shaders")
endif()

# Sign only after every bundle component has been installed. This is an ad-hoc
# signature: it makes the archive internally consistent, but Developer ID
# signing and notarization are still required for seamless Gatekeeper launches.
if(APPLE AND ENGINE_SIM_MACOS_APP_BUNDLE)
    install(CODE [[
        set(sdl_library_name "$<TARGET_FILE_NAME:SDL3::SDL3>")
        set(bundle_runtime_directory "${CMAKE_INSTALL_PREFIX}/engine-sim.app/Contents/MacOS")
        execute_process(
            COMMAND otool -D "${bundle_runtime_directory}/${sdl_library_name}"
            OUTPUT_VARIABLE sdl_install_names
            COMMAND_ERROR_IS_FATAL ANY)
        string(REGEX MATCH "\n([^\n]+)" sdl_install_name_match "${sdl_install_names}")
        set(sdl_install_name "${CMAKE_MATCH_1}")
        if(NOT sdl_install_name)
            message(FATAL_ERROR "Could not determine SDL's dynamic-library install name")
        endif()
        foreach(executable IN ITEMS engine-sim engine-sim-audio)
            if(EXISTS "${bundle_runtime_directory}/${executable}")
                execute_process(
                    COMMAND install_name_tool -change "${sdl_install_name}"
                        "@executable_path/${sdl_library_name}" "${bundle_runtime_directory}/${executable}"
                    COMMAND_ERROR_IS_FATAL ANY)
            endif()
        endforeach()
        execute_process(
            COMMAND install_name_tool -id "@rpath/${sdl_library_name}"
                "${bundle_runtime_directory}/${sdl_library_name}"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(
            COMMAND codesign --force --deep --sign - "${CMAKE_INSTALL_PREFIX}/engine-sim.app"
            COMMAND_ERROR_IS_FATAL ANY)
    ]])
endif()
