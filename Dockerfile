FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG C.UTF-8
ENV LC_ALL C.UTF-8

RUN apt-get -y update && \
    apt-get -y upgrade && \
    apt-get -y dist-upgrade && \
    apt-get -y autoremove && \
    apt-get install -y build-essential gdb wget git libssl-dev clang-format && \
    mkdir ~/temp && cd ~/temp && \
    apt-get install -y cmake && \
    cd ~/temp &&  wget https://sourceforge.net/projects/boost/files/boost/1.81.0/boost_1_81_0.tar.gz && \
    tar -zxvf boost_1_81_0.tar.gz && cd ~/temp/boost_1_81_0 && ./bootstrap.sh && ./b2 cxxflags="-std=c++11" --reconfigure --with-fiber --with-context --with-atomic --with-date_time --with-filesystem --with-url install && \
    cd ~/temp && git clone -b v1.15 https://github.com/linux-test-project/lcov.git && cd lcov && make install && cd .. && \
    apt-get install -y libperlio-gzip-perl libjson-perl && \
    rm -rf ~/temp/* && \
    apt-get autoremove -y &&\
    apt-get clean -y &&\
    rm -rf /var/lib/apt/lists/*

RUN apt-get -y update && \
    apt-get install -y libpq-dev libsqlite3-dev unzip && \
    cd ~/temp && \
    git clone https://github.com/jtv/libpqxx.git && cd libpqxx && \
    git checkout 7.6.1 && \
    mkdir build && cd build && \
    cmake .. -DPostgreSQL_TYPE_INCLUDE_DIR=/usr/include/postgresql/libpq && \
    make -j6 && make install && \
    cd ~/temp && \
    wget https://github.com/SOCI/soci/archive/refs/tags/v4.0.3.zip && \
    unzip v4.0.3.zip && \
    cd soci-4.0.3 && \
    mkdir build && cd build && \
    cmake .. -DSOCI_WITH_BOOST=ON -DSOCI_WITH_POSTGRESQL=ON -DSOCI_WITH_SQLITE3=ON -DCMAKE_CXX_STANDARD=11 -DSOCI_CXX11=ON && \
    make -j6 && make install && \
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
    cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=11 .. && \
    make -j4 && \
    cp tests/libasyik_test  /usr/bin ; \
    fi

