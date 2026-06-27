# The new base image to contain runtime dependencies

FROM debian:12.13 AS runtime_base

RUN apt update -y --fix-missing;  
RUN apt install -y curl openssl libssl-dev zlib1g-dev libpq-dev iputils-ping netcat-traditional;

FROM rwlltt/netprocdependencies:1.1 AS livepostsvc_builder

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