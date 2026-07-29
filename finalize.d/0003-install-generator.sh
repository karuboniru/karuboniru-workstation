#!/bin/bash

clang++ $RPMOSTREE_WORKDIR/extra-source/systemd-cvmfs-generator.cpp \
        -O2 -std=c++17 \
        -o ./usr/lib/systemd/system-generators/systemd-cvmfs-generator
