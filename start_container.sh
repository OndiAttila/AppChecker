#!/bin/bash

sudo docker run -d --rm --name clang -u $(id -u) -v $(pwd):/app clang sleep inf
