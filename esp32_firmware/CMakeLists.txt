# obligatorio en cualquier proyecto de CMake
cmake_minimum_required(VERSION 3.16)
# importa las funcionalidades de IDF para CMake
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# configura una propiedad global de IDF
idf_build_set_property(MINIMAL_BUILD ON)
# debe ir después de incluir IDF
project(tarea-1)
