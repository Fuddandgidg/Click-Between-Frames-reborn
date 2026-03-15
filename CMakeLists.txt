cmake_minimum_required(VERSION 3.25)

project(ClickBetweenFrames)

find_package(Geode REQUIRED)

# This creates the .geode file for you
add_geode_mod(
    syzzi.click_between_frames
    v1.4.6
    SOURCES src/main.cpp
)

# Important for iPad builds to link correctly
target_link_libraries(syzzi.click_between_frames PUBLIC geode-sdk)