file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE TESTS "src/*.test.cpp")
list(REMOVE_ITEM SOURCES ${TESTS})

add_library(logo STATIC ${SOURCES})
if (NOT NNCC_LIBRARY_NO_PIC)
  set_target_properties(logo PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif(NOT NNCC_LIBRARY_NO_PIC)
target_include_directories(logo PRIVATE src)
target_include_directories(logo PUBLIC include)
target_link_libraries(logo PUBLIC loco)
target_link_libraries(logo PUBLIC logo_core)

if(NOT ENABLE_TEST)
  return()
endif(NOT ENABLE_TEST)

nnas_find_package(GTest REQUIRED)

GTest_AddTest(logo_test ${TESTS})
target_include_directories(logo_test PRIVATE src)
target_link_libraries(logo_test logo)
