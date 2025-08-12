cmake_minimum_required(VERSION 3.10)
project(jesd-eye-scan-gtk VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags to match Makefile
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--export-dynamic")

# Find required packages
find_package(PkgConfig REQUIRED)

# Find GTK3
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Find ncurses
find_library(NCURSES_LIBRARY ncurses REQUIRED)

# Optional libiio support
option(USE_LIBIIO "Enable libiio support" OFF)
if(USE_LIBIIO)
    pkg_check_modules(LIBIIO REQUIRED libiio)
    add_definitions(-DUSE_LIBIIO)
endif()

# Include directories
include_directories(${GTK3_INCLUDE_DIRS})
if(USE_LIBIIO)
    include_directories(${LIBIIO_INCLUDE_DIRS})
endif()

# Link directories  
link_directories(${GTK3_LIBRARY_DIRS})
if(USE_LIBIIO)
    link_directories(${LIBIIO_LIBRARY_DIRS})
endif()

# Add compiler flags
add_definitions(${GTK3_CFLAGS_OTHER})

# Common source files
set(COMMON_SOURCES jesd_common.c jesd_common.h)

# jesd_status executable
add_executable(jesd_status jesd_status.c ${COMMON_SOURCES})
target_link_libraries(jesd_status ${NCURSES_LIBRARY})
if(USE_LIBIIO)
    target_link_libraries(jesd_status ${LIBIIO_LIBRARIES})
endif()

# jesd_eye_scan executable  
add_executable(jesd_eye_scan jesd_eye_scan.c ${COMMON_SOURCES})
target_link_libraries(jesd_eye_scan ${GTK3_LIBRARIES} m)
if(USE_LIBIIO)
    target_link_libraries(jesd_eye_scan ${LIBIIO_LIBRARIES})
endif()

# Set default install prefix to match Makefile
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Install prefix" FORCE)
endif()

# Install targets
install(TARGETS jesd_status jesd_eye_scan
    RUNTIME DESTINATION bin
)

install(PROGRAMS jesd_eye_scan_autostart.sh
    DESTINATION bin
)

install(FILES jesd.glade
    DESTINATION share/jesd/
)

install(FILES icons/ADIlogo.png
    DESTINATION share/jesd/
)

# Install desktop file to user's autostart directory
install(CODE "
    execute_process(COMMAND \${CMAKE_COMMAND} -E make_directory \$ENV{HOME}/.config/autostart)
    execute_process(COMMAND \${CMAKE_COMMAND} -E copy 
        \${CMAKE_SOURCE_DIR}/jesd_eye_scan.desktop
        \$ENV{HOME}/.config/autostart/jesd_eye_scan.desktop)
")

# Add custom clean target to match Makefile behavior
add_custom_target(clean-extra
    COMMAND ${CMAKE_COMMAND} -E remove -f *.png *.eye
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Removing additional generated files"
)