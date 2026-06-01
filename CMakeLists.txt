cmake_minimum_required(VERSION 3.20)
project(ufwGui)

set(LGI_LIB_ONLY TRUE)
add_subdirectory(../lgi/trunk lgi)

add_executable(ufwGui
    src/main.cpp
    ${LGI_DIR}/src/common/Lgi/LgiMain.cpp)

target_link_libraries(ufwGui PRIVATE lgi)
target_include_directories(ufwGui PRIVATE include)

add_executable(ufwWorker
    src/worker.cpp
    ${LGI_DIR}/src/common/Lgi/LgiMain.cpp)

target_link_libraries(ufwWorker PRIVATE lgi)
target_include_directories(ufwWorker PRIVATE include)