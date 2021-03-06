## CMakeLists.txt                                       -*- CMake -*-
##
## Copyright (C) 2018-2020 Christian Schenk
## 
## This file is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published
## by the Free Software Foundation; either version 2, or (at your
## option) any later version.
## 
## This file is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
## 
## You should have received a copy of the GNU General Public License
## along with this file; if not, write to the Free Software
## Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
## USA.

include(component.cmake)

set(MIKTEX_CURRENT_FOLDER "${MIKTEX_IDE_3RD_LIBRARIES_FOLDER}/fmt")

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}
)

set(public_include_directories
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include
)

set(fmt_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/source/src/format.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/source/src/os.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/chrono.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/color.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/compile.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/core.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/format-inl.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/format.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/locale.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/os.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/ostream.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/posix.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/printf.h
  ${CMAKE_CURRENT_SOURCE_DIR}/source/include/fmt/ranges.h
)

configure_file(
  fmt-version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/fmt-version.h
)

list(APPEND fmt_sources
  ${CMAKE_CURRENT_BINARY_DIR}/fmt-version.h
)

if(NOT LINK_EVERYTHING_STATICALLY)
  add_subdirectory(shared)
endif()

add_subdirectory(static)
