if(CONFIG_ZMK_UNDERGLOW_INDICATORS)
  zephyr_library_named(zmk_underglow_indicators)
  # ZMK keeps app/include private to its own target.
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(src/underglow_indicators.c)
endif()
