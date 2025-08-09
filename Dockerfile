# The new base image to contain runtime dependencies
# Dependencies: Boost >= 1.86

FROM debian:12.11 AS livepostsvc_base

RUN apt update -y;  
RUN apt install -y build-essential cmake curl git openssl libssl-dev zlib1g-dev libpq-dev python3 python3-pybind11 python3-dev pkg-config;

FROM livepostsvc_base AS livepostsvc_boost

WORKDIR /usr/src

# Install Boost 1.86 or later
COPY boost_1_86_0.tar.gz /usr/src

RUN BOOST_VERSION=1.86.0; \
    BOOST_DIR=boost_1_86_0; \
    tar -xvf boost_1_86_0.tar.gz; \
    cd ${BOOST_DIR}; \
    ./bootstrap.sh --prefix=/usr/local; \
    ./b2 --with-headers --with-system --with-thread --with-date_time --with-regex --with-serialization --with-program_options --with-url install; \
    cd ..; \
    rm -rf ${BOOST_DIR} ${BOOST_DIR}.tar.gz

FROM livepostsvc_boost AS livepostsvc_builder

COPY . /usr/src

# Build the project using CMake
RUN mkdir -p build; \
    cd build; \
    cmake .. -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-fopenmp"; \
    cd ..; cmake --build build --target LivePostSvc; \
    cd build; make install

RUN strip /usr/local/bin/LivePostSvc

FROM livepostsvc_base AS livepostsvc_runtime

COPY --from=livepostsvc_builder /usr/local/bin /usr/local/bin
COPY --from=livepostsvc_builder /usr/local/lib /usr/local/lib

EXPOSE 3003

ENTRYPOINT [ "LivePostSvc", "--threads", "3", "--root", "/usr/src/latest"]