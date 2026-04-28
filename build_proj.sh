#!/bin/bash

CC=x86_64-w64-mingw32-clang++

mkdir -p bin
${CC} --static AppChecker.cpp -o bin/AppChecker
