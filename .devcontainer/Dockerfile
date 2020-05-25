FROM ubuntu:18.04

RUN apt-get -y update && \
    apt-get -y upgrade && \
    apt-get -y dist-upgrade && \
    apt-get -y autoremove && \
    apt-get install -y build-essential gdb wget

RUN mkdir ~/temp && cd ~/temp && \
    wget  https://cmake.org/files/v3.14/cmake-3.14.5.tar.gz && \
    tar -zxvf cmake-3.14.5.tar.gz && \
    cd cmake-3.14.5 && \
    ./bootstrap && make -j4 && make install && \
    rm -rf ~/temp/*

RUN cd ~/temp &&  wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz && \
    tar -zxvf boost_1_70_0.tar.gz && cd boost_1_70_0 && ./bootstrap.sh && ./b2 cxxflags="-std=c++17" --reconfigure --with-fiber install && \
    rm -rf ~/temp/*

ENV DEBIAN_FRONTEND=noninteractive   

RUN apt-get install -y qt5-default libvtk6-dev && \
    apt-get install -y zlib1g-dev libjpeg-dev libwebp-dev libpng-dev libtiff5-dev libopenexr-dev libgdal-dev && \
    apt-get install -y libdc1394-22-dev libavcodec-dev libavformat-dev libswscale-dev libtheora-dev \
                       libvorbis-dev libxvidcore-dev libx264-dev yasm libopencore-amrnb-dev \
                       libopencore-amrwb-dev libv4l-dev libatlas-base-dev gfortran python3.6-dev python3.6-dev&& \
    apt-get install -y unzip

RUN cd ~ && \
    wget --progress=dot:giga https://github.com/opencv/opencv/archive/3.4.5.zip && \
    unzip -q 3.4.5.zip && \
    rm 3.4.5.zip && \
    mv opencv-3.4.5 OpenCV && \
    cd OpenCV && \
    mkdir build && \
    cd build && \
    cmake  \
        -D BUILD_SHARED_LIBS=OFF \ 
        -D WITH_QT=ON \
        -D WITH_OPENGL=ON \ 
        -D WITH_TBB=ON \ 
        -D WITH_V4L=ON \
        -D WITH_EIGEN=ON \
        -D WITH_FFMPEG=ON \
        -D WITH_CUDA=ON \
        -D CUDA_GENERATION=Turing \
        -D ENABLE_FAST_MATH=1 \
        -D CUDA_FAST_MATH=1 \
        -D WITH_GDAL=ON \ 
        -D WITH_CUBLAS=ON \
        -D BUILD_EXAMPLES=OFF \
        -D ENABLE_PRECOMPILED_HEADERS=OFF \
        -D BUILD_DOCS=OFF \
        -D BUILD_opencv_cudacodec=OFF \
        -D EIGEN_INCLUDE_PATH=/usr/include/eigen3 \
        -D BUILD_PERF_TESTS=OFF \
        -D BUILD_TESTS=OFF \
        -D BUILD_opencv_apps=OFF \
        .. && \
    make -j4 && \
    make install && \
    make clean && \
    ldconfig

RUN apt-get autoremove -y &&\
    apt-get clean -y &&\
    rm -rf /var/lib/apt/lists/*
