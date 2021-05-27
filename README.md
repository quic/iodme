## Overview

IODME (IO Data Mover Engine) is a library, and some tools, for optimizing 
typical IO operations that involve copying / moving data between memory
and file descriptors.
It provides various building blocks for data pipelining using hugepages, 
vmsplice, splice, and sendfile mechanisms. 

## Build dependencies

IODME requires the following components to build
* CMake (3.12 or later)
* Compiler with C++14 support
* HOGL logging library (2.0 or later)
* Boost libary (1.59.0 or later) 

CMakeList includes necessary checks for all requirements.

CMake 3.12 introduced CMP0074 which is used for finding packages (e.g. HOGL)

## Build procedure

Install dependencies and run
```
cmake -DCMAKE_CXX_FLAGS="-O3 -Wall -g -std=c++14"
cmake
```

## Usage examples

### Generator and sink tests

This tool / test implements streaming of large data frames over the TCP
connection, and storing them into files using various IO optimization 
techniques. 

To run data sink process
```
sudo ./src/iodme-sink --log-mask '.*WRITER.*:DEBUG' --output-dir /disk/speed-test -C 10 -W 4 --splice --directio --hugepages
```

Run _iodme-sink --help_ to see the documentation for all options.

To run data generators
```
sudo ./src/run-data-gen-test 2 localhost 30
```

Run _iodme-generator --help_ to see the documentation for all options.
The script above is just a wrapper that starts multiple data generators.
