#!/usr/bin/env bash
set -euo pipefail

# Start agent if not running
if [ -z "${SSH_AUTH_SOCK:-}" ]; then
    eval "$(ssh-agent -s)"
fi
echo $SSH_AUTH_SOCK

# Load GitLab key if not loaded
if ! ssh-add -l | grep -q "gitlab"; then
    ssh-add ~/.ssh/gitlab
fi
# Load GitHub key if not loaded
if ! ssh-add -l | grep -q "github"; then
    ssh-add ~/.ssh/id_ed25519
fi

IMAGE_NAME="livepostsvc"
IMAGE_TAG="v1.0"

# --- Collect Git metadata ---
GIT_COMMIT=$(git rev-parse HEAD)
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
GIT_DIRTY=$(test -n "$(git status --porcelain)" && echo "dirty" || echo "clean")
BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# -- System wide public third party repos
# -- nb. - nlohmann-json is installed with apt package
# --     - Boost 1.66 is installed in images with Boost_1_86_0.tar.gz
FMT_COMMIT=$(git ls-remote https://github.com/fmtlib/fmt.git HEAD | awk '{print $1}')
JWT_CPP_COMMIT=$(git ls-remote https://github.com/Thalhammer/jwt-cpp.git HEAD | awk '{print $1}')

# -- System wide library repos
MTLOG_COMMIT=$(git ls-remote git@github.com:rwoollett/mtlog.git HEAD | awk '{print $1}')
REDIS_PUBSUB_COMMIT=$(git ls-remote git@github.com:rwoollett/redis_pubsub.git HEAD | awk '{print $1}')
REDIS_STREAM_COMMIT=$(git ls-remote git@github.com:rwoollett/redis_stream.git HEAD | awk '{print $1}')
APISERVER_COMMIT=$(git ls-remote git@github.com:rwoollett/apiserver.git HEAD | awk '{print $1}')

echo "Building Docker image:"
echo "  Commit: $GIT_COMMIT"
echo "  Branch: $GIT_BRANCH"
echo "  Dirty:  $GIT_DIRTY"
echo "  Date:   $BUILD_DATE"
echo "  fmt commit:   $FMT_COMMIT"
echo "  jwt-cpp commit:   $JWT_CPP_COMMIT"
echo "  mtlog commit:   $MTLOG_COMMIT"
echo "  redis_pubsub commit:   $REDIS_PUBSUB_COMMIT"
echo "  redis_stream commit:   $REDIS_STREAM_COMMIT"
echo "  apiserver commit:   $APISERVER_COMMIT"
echo

# --- Build image with metadata ---
DOCKER_BUILDKIT=1 docker build \
  --ssh default \
  --build-arg GIT_COMMIT="$GIT_COMMIT" \
  --build-arg GIT_BRANCH="$GIT_BRANCH" \
  --build-arg GIT_DIRTY="$GIT_DIRTY" \
  --build-arg BUILD_DATE="$BUILD_DATE" \
  --build-arg FMT_COMMIT="$FMT_COMMIT" \
  --build-arg JWT_CPP_COMMIT="$JWT_CPP_COMMIT" \
  --build-arg MTLOG_COMMIT="$MTLOG_COMMIT" \
  --build-arg REDIS_PUBSUB_COMMIT="$REDIS_PUBSUB_COMMIT" \
  --build-arg REDIS_STREAM_COMMIT="$REDIS_STREAM_COMMIT" \
  --build-arg APISERVER_COMMIT="$APISERVER_COMMIT" \
  -t "$IMAGE_NAME:$IMAGE_TAG" \
  .

echo
echo "Image built: $IMAGE_NAME:$IMAGE_TAG"
