# The new base image to contain runtime dependencies
# Dependencies: Boost >= 1.86

FROM debian:12.13 AS runtime_base

RUN apt update -y --fix-missing;  
RUN apt install -y curl openssl libssl-dev zlib1g-dev libpq-dev iputils-ping netcat-traditional;

FROM runtime_base AS build_boost

RUN apt install -y git build-essential cmake pkg-config nlohmann-json3-dev;

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

FROM build_boost AS system_wide_dependencies
# Insert system wide installs for RwllttNet::APIServer
# RwllttNet depends on MTLog, which depends on fmt::fmt
# RwllttNet depends on finding packages: jwt-cpp, redis_pubsub and redis_stream(WorkQStream)
WORKDIR /usr/src

# Trust GitLab host key
RUN mkdir -p /root/.ssh && \
    chmod 700 /root/.ssh && \
    ssh-keyscan gitlab.com >> /root/.ssh/known_hosts
# Trust GitHub host key
RUN ssh-keyscan github.com >> /root/.ssh/known_hosts

# Fmt (public repo)
ARG FMT_COMMIT=unknown
RUN rm -rf fmt && \
    git clone --depth=1 https://github.com/fmtlib/fmt.git && \
    cd fmt && git checkout $FMT_COMMIT && \
    cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DFMT_TEST=OFF \
    -DFMT_DOC=OFF \
    -DFMT_FUZZ=OFF \
    -DFMT_BENCHMARK=OFF \
    -DFMT_INSTALL=ON && \
    cmake --build build --target install

# jwt-cpp (public repo)
ARG JWT_CPP_COMMIT=unknown
RUN rm -rf jwt-cpp && \
    git clone --depth=1 https://github.com/Thalhammer/jwt-cpp.git && \
    cd jwt-cpp && git checkout $JWT_CPP_COMMIT && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target install

# MTLog (private repo)
ARG MTLOG_COMMIT=unknown
RUN --mount=type=ssh rm -rf mtlog && \
    git clone --depth=1 git@github.com:rwoollett/mtlog.git && \
    cd mtlog && git checkout $MTLOG_COMMIT && \
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build-release --target install

# Redis PubSub
ARG REDIS_PUBSUB_COMMIT=unknown
RUN --mount=type=ssh rm -rf redis_pubsub && \
    git clone --depth=1 git@github.com:rwoollett/redis_pubsub.git && \
    cd redis_pubsub && git checkout $REDIS_PUBSUB_COMMIT && \
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build-release --target install

# Redis Stream (WorkQStream)
ARG REDIS_STREAM_COMMIT=unknown
RUN --mount=type=ssh rm -rf redis_stream && \
    git clone --depth=1 git@github.com:rwoollett/redis_stream.git && \
    cd redis_stream && git checkout $REDIS_STREAM_COMMIT && \
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build-release --target install

# APIServer (private repo)
ARG APISERVER_COMMIT=unknown
RUN --mount=type=ssh rm -rf apiserver && \
    git clone --depth=1 git@github.com:rwoollett/apiserver.git && \
    cd apiserver && git checkout $APISERVER_COMMIT && \
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build-release --target install


FROM system_wide_dependencies AS livepostsvc_builder

COPY . /usr/src

ARG GIT_COMMIT

# Build the project using CMake xxxx
RUN mkdir -p build; \
    cd build; \
    cmake .. \
    -DGIT_COMMIT=${GIT_COMMIT} \
    -DBUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release; \
    cd ..; cmake --build build --target LivePostSvc; \
    cd build; make install

RUN strip /usr/local/bin/LivePostSvc

FROM runtime_base AS livepostsvc_runtime

ARG node_version=v22.21.1
RUN cd / && curl -fsSL https://nodejs.org/dist/$node_version/node-$node_version-linux-x64.tar.gz -o node.tar.gz \
    && tar -xzvf node.tar.gz && rm node.tar.gz \
    && echo "export PATH=$PATH:/node-$version-linux-x64/bin" >> /root/.bashrc

COPY --from=livepostsvc_builder /usr/local/bin /usr/local/bin
COPY --from=livepostsvc_builder /usr/src/posts-vite-app /posts-vite-app
RUN export PATH=$PATH:/node-$node_version-linux-x64/bin
RUN cd /posts-vite-app; \
    export PATH=$PATH:/node-$node_version-linux-x64/bin; \
    npm install; \
    npm run build; 

ARG GIT_COMMIT
ARG GIT_BRANCH
ARG GIT_DIRTY
ARG BUILD_DATE
LABEL org.opencontainers.image.revision=$GIT_COMMIT
LABEL org.opencontainers.image.source-branch=$GIT_BRANCH
LABEL org.opencontainers.image.dirty=$GIT_DIRTY
LABEL org.opencontainers.image.created=$BUILD_DATE

WORKDIR /usr/src 

EXPOSE 3011

ENTRYPOINT [ "LivePostSvc", "--threads", "3", "--root", "/usr/src/latest"]