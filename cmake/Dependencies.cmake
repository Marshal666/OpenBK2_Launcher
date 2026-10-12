include(FetchContent)

function(obk_transfer_dependencies)
  FetchContent_Declare(miniz_source
    URL https://codeload.github.com/richgel999/miniz/tar.gz/174573d60290f447c13a2b1b3405de2b96e27d6c
    URL_HASH SHA256=3cb61250e4ec1890423273fe58752f1e63d9a6d0c3b58dea61e6fe6c82d6d349
    SOURCE_SUBDIR unused DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(miniz_source)
  file(WRITE ${miniz_source_BINARY_DIR}/miniz_export.h "#pragma once\n#define MINIZ_EXPORT\n")
  add_library(miniz STATIC ${miniz_source_SOURCE_DIR}/miniz.c ${miniz_source_SOURCE_DIR}/miniz_tinfl.c ${miniz_source_SOURCE_DIR}/miniz_zip.c)
  target_include_directories(miniz PUBLIC ${miniz_source_SOURCE_DIR} ${miniz_source_BINARY_DIR})
  target_compile_definitions(miniz PUBLIC MINIZ_NO_DEFLATE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_STATIC_CURL OFF CACHE BOOL "" FORCE)
  set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
  set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
  set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
  set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
  set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
  set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
  set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
  set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
  set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
  set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)
  foreach(feature DICT FILE FTP GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP WEBSOCKETS)
    set(CURL_DISABLE_${feature} ON CACHE BOOL "" FORCE)
  endforeach()
  if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  else()
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
  endif()
  FetchContent_Declare(curl
    URL https://codeload.github.com/curl/curl/tar.gz/6e3f8dc1f173b47de9a68516ce4b95bf25598c2f
    URL_HASH SHA256=1bbcbe03719ffd2293b8c71903ef977efcd1f7133255264e4c42171e50ccae68
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  # curl's BUILD_TESTING option must not enable upstream executables.
  set(BUILD_TESTING OFF)
  FetchContent_MakeAvailable(curl)
endfunction()

function(obk_security_dependencies)
  FetchContent_Declare(yyjson_source
    URL https://codeload.github.com/ibireme/yyjson/tar.gz/7871d321ff4cd8068c1f777c97975dc2fb640ab3
    URL_HASH SHA256=a3bc9626ec0ba8bcc0644cc5355b3cf6eeda3188caf72bd7caa0c305acc78e79
    SOURCE_SUBDIR unused DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_Declare(monocypher_source
    URL https://codeload.github.com/LoupVaillant/Monocypher/tar.gz/0d85f98c9d9b0227e42cf795cb527dff372b40a4
    URL_HASH SHA256=7339f8a4729d5339ad6df61ba11937f8d5d163ba5a49edf5fdf3d8717b73d1a9
    SOURCE_SUBDIR unused DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_Declare(picosha_source
    URL https://codeload.github.com/okdshin/PicoSHA2/tar.gz/161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29
    URL_HASH SHA256=6cf473a00c98298d3ddee0aed853e3c799791f49dbc01c996ea46cb248e85802
    SOURCE_SUBDIR unused DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(yyjson_source monocypher_source picosha_source)
  add_library(yyjson STATIC ${yyjson_source_SOURCE_DIR}/src/yyjson.c)
  target_include_directories(yyjson PUBLIC ${yyjson_source_SOURCE_DIR}/src)
  add_library(monocypher STATIC ${monocypher_source_SOURCE_DIR}/src/monocypher.c
    ${monocypher_source_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
  target_include_directories(monocypher PUBLIC ${monocypher_source_SOURCE_DIR}/src ${monocypher_source_SOURCE_DIR}/src/optional)
  add_library(picosha INTERFACE)
  target_include_directories(picosha INTERFACE ${picosha_source_SOURCE_DIR})
endfunction()

function(obk_gui_dependencies)
  if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    find_package(X11 REQUIRED)
    pkg_check_modules(OBK_WAYLAND REQUIRED wayland-client>=1.18 wayland-egl wayland-cursor egl xkbcommon>=0.5.0)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(SDL_WAYLAND_LIBDECOR OFF CACHE BOOL "Borderless application uses compositor hit testing" FORCE)
  endif()
  set(SDL_REVISION "SDL-3.2.28-7f3ae3d57459e59943a4ecfefc8f6277ec6bf540" CACHE STRING "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_LIBC ON CACHE BOOL "" FORCE)
  set(SDL_STATIC_VCRT ON CACHE BOOL "" FORCE)
  foreach(feature AUDIO CAMERA JOYSTICK HAPTIC SENSOR HIDAPI GPU VULKAN RENDER_D3D12 RENDER_GPU)
    set(SDL_${feature} OFF CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(sdl
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540
    URL_HASH SHA256=7a8347c770b90b33daac2352858ca03f9c9a2ccc8ce711054870361d1a6b32e5
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_Declare(imgui_source
    URL https://codeload.github.com/ocornut/imgui/tar.gz/6d910d5487d11ca567b61c7824b0c78c569d62f0
    URL_HASH SHA256=b715a3226320aaebaea79dc6af6405ae37cdcb479817fddaef12a17d8bd15a3a
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(sdl imgui_source)
  set(OBK_FONT_SOURCE "${imgui_source_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf" PARENT_SCOPE)
  add_library(imgui STATIC
    ${imgui_source_SOURCE_DIR}/imgui.cpp
    ${imgui_source_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_source_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_source_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_source_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_source_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_source_SOURCE_DIR} ${imgui_source_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
  target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
endfunction()
