cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(addresstable)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# add_executable(map map.cc)
#
# add_executable(unordered_map unordered_map.cc)
#
# add_executable(lmdb lmdb.cc)
# target_link_libraries(lmdb -llmdb)
#
# add_executable(macro macro.cc)
#
# add_executable(dlopen dlopen.cc)
# target_link_libraries(dlopen -ldl)
#
# add_executable(variable variable.cc)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/registerstruct.h
    COMMAND sh -c 'python3 -m flatten >registerstruct.h'
    DEPENDS gem_amc_top.xml flatten.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(registerstruct_h DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/registerstruct.h)

add_executable(struct struct.cc)
add_dependencies(struct registerstruct_h)

# add_executable(ministruct ministruct.cc)

# add_executable(test_register test_register.cc)

add_executable(memsvc_overhead memsvc_overhead.cc libmemsvc.c)
