# The new base image to contain runtime dependencies
# Dependencies: Boost >= 1.86

FROM debian:12.13 AS livepostsvc_base

RUN apt update -y;  
ARG version=v22.21.1
RUN apt install -y curl gdb openssl libssl-dev zlib1g-dev libpq-dev python3 python3-pybind11 python3-dev \
    && curl -fsSL https://nodejs.org/dist/$version/node-$version-linux-x64.tar.gz -o node.tar.gz \
    && tar -xzvf node.tar.gz && rm node.tar.gz \
    && echo "export PATH=$PATH:/node-$version-linux-x64/bin" >> /root/.bashrc

FROM livepostsvc_base AS livepostsvc_boost

RUN apt install -y git build-essential cmake pkg-config;

WORKDIR /usr/src

# Install Boost 1.86 or later
COPY boost_1_86_0.tar.gz /usr/src

RUN BOOST_VERSION=1.86.0; \
    BOOST_DIR=boost_1_86_0; \
    tar -xvf boost_1_86_0.tar.gz; \
    cd ${BOOST_DIR}; \
    ./bootstrap.sh --prefix=/usr/local; \
    ./b2 link=static --with-headers --with-system --with-thread --with-date_time --with-regex --with-serialization --with-program_options --with-url install; \
    cd ..; \
    rm -rf ${BOOST_DIR} ${BOOST_DIR}.tar.gz

FROM livepostsvc_boost AS livepostsvc_builder

COPY . /usr/src

ARG version=v22.21.1
RUN cd posts-vite-app; \
    export PATH=$PATH:/node-$version-linux-x64/bin; \
    npm install; \
    npm run build; 

# Build the project using CMake
RUN mkdir -p build; \
    cd build; \
    cmake .. -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Debug; \
    cd ..; cmake --build build --target LivePostSvc; \
    cd build; make install

# RUN strip /usr/local/bin/LivePostSvc

FROM livepostsvc_base AS livepostsvc_runtime

COPY --from=livepostsvc_builder /usr/local/bin /usr/local/bin
COPY --from=livepostsvc_builder /usr/src/posts-vite-app /usr/src/posts-vite-app
RUN export PATH=$PATH:/node-$version-linux-x64/bin

EXPOSE 3011

ENTRYPOINT [ "LivePostSvc", "--threads", "3", "--root", "/usr/src/latest"]