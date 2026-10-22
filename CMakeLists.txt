cmake_minimum_required(VERSION 3.16)
project(milo_expense_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(expense-tracker src/main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(expense-tracker PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(expense-tracker PRIVATE ws2_32)
endif()

if(MSVC)
  target_compile_options(expense-tracker PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(expense-tracker PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS expense-tracker RUNTIME DESTINATION bin)
install(DIRECTORY web/ DESTINATION share/expense-tracker/web)
