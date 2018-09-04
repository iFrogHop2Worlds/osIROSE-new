set(DATE_INSTALL_DIR ${CMAKE_THIRD_PARTY_DIR})

ExternalProject_Add(
  date
  GIT_REPOSITORY https://github.com/HowardHinnant/date.git
  GIT_TAG 6a4d93a0bd8b56db68821067322c09ec37d0d8a9
  CMAKE_ARGS -G ${CMAKE_GENERATOR} -DCURL_LIBRARY=${CURL_LIBRARY} -DCURL_INCLUDE_DIR=${CURL_INCLUDE_DIR} -DCMAKE_INSTALL_PREFIX=${DATE_INSTALL_DIR} -DENABLE_DATE_TESTING=OFF
  PATCH_COMMAND ${PATCH_SCRIPT_PATH} ${CMAKE_PATCH_DIR}/date_win.patch
  INSTALL_DIR ${DATE_INSTALL_DIR}
  DEPENDS CURL::libcurl
  BUILD_COMMAND cmake --build . --target install
)

ExternalProject_Get_Property(
  date
  install_dir
)

set(DATE_INCLUDE_DIR "${install_dir}/include")
if(NOT TARGET DATE::DATE)
    add_library(DATE::DATE INTERFACE IMPORTED)
    add_dependencies(DATE::DATE date)
    set_target_properties(DATE::DATE PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${DATE_INCLUDE_DIR}")
endif()