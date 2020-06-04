FROM ubuntu:18.04

RUN apt-get -y update && \
    apt-get -y upgrade && \
    apt-get -y dist-upgrade && \
    apt-get -y autoremove && \
    apt-get install -y build-essential gdb wget git && \
    mkdir ~/temp && cd ~/temp && \
    wget  https://cmake.org/files/v3.14/cmake-3.14.5.tar.gz && \
    tar -zxvf cmake-3.14.5.tar.gz && \
    cd cmake-3.14.5 && \
    ./bootstrap && make -j4 && make install && \
    rm -rf ~/temp/* && \
    cd ~/temp &&  wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz && \
    tar -zxvf boost_1_70_0.tar.gz && cd boost_1_70_0 && ./bootstrap.sh && ./b2 cxxflags="-std=c++17" --reconfigure --with-fiber install && \
    rm -rf ~/temp/* && \
    apt-get autoremove -y &&\
    apt-get clean -y &&\
    rm -rf /var/lib/apt/lists/*

ARG DOCKER_TYPE

RUN mkdir /usr/local/libasyik
COPY . /usr/local/libasyik

RUN if [ "$DOCKER_TYPE" = "TEST" ]; then \
    cd /usr/local/libasyik && \
    git submodule update --init --recursive && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make -j 4 && \
    cp tests/libasyik_test  /usr/bin ; \
  fi
  
