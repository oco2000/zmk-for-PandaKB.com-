if(CONFIG_ZMK_PERKEY_RGB)
  zephyr_library_named(zmk_perkey_rgb)
  # ZMK keeps app/include private to its own target.
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_include_directories(include)
  zephyr_library_sources(src/perkey_rgb.c)
  zephyr_library_sources(src/behavior_perkey_rgb.c)
endif()
