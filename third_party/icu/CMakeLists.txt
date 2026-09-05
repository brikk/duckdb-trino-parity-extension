# Consume the pinned upstream source manifests rather than maintaining a partial
# dependency list. Ordinary translation units need none of DuckDB's unity patches.
foreach(component common i18n stubdata)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${component}/sources.txt" sources)
  set(component_sources)
  foreach(source ${sources})
    list(APPEND component_sources "${component}/${source}")
  endforeach()

  set(target trino_parity_icu_${component})
  add_library(${target} OBJECT ${component_sources})
  target_include_directories(${target} BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
  target_compile_definitions(${target} PRIVATE UCONFIG_USE_LOCAL=1)
  if(component STREQUAL "common")
    target_compile_definitions(${target} PRIVATE U_COMMON_IMPLEMENTATION)
  elseif(component STREQUAL "i18n")
    target_compile_definitions(${target} PRIVATE U_I18N_IMPLEMENTATION)
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /bigobj)
  endif()
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF)
  disable_target_warnings(${target})
  list(APPEND ICU_LIBRARY_FILES $<TARGET_OBJECTS:${target}>)
endforeach()

set(ICU_LIBRARY_FILES ${ICU_LIBRARY_FILES} PARENT_SCOPE)
