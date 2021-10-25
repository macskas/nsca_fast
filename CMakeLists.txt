cmake_minimum_required(VERSION 2.8)
project(nsca)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-O3 -march=native -std=c++0x")
LINK_LIBRARIES(event mcrypt pthread dl)
#LINK_LIBRARIES(event mcrypt pthread dl event_pthreads)
add_executable(nsca main.cpp common.h network_client.cpp network_client.h network.cpp network.h log.cpp log.h nsca_utils.h nsca_common.h nsca_utils.cpp fifo_client.cpp fifo_client.h config.cpp config.h processManager.cpp processManager.h threadManager.cpp threadManager.h crypt_thread_t.cpp crypt_thread_t.h stat_writer.cpp stat_writer.h result_path_client.cpp result_path_client.h)
