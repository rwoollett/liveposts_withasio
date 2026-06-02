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

echo "Building Docker image:"
echo "  Commit: $GIT_COMMIT"
echo "  Branch: $GIT_BRANCH"
echo "  Dirty:  $GIT_DIRTY"
echo "  Date:   $BUILD_DATE"
echo

# --- Build image with metadata ---
DOCKER_BUILDKIT=1 docker build \
  --ssh default \
  --build-arg GIT_COMMIT="$GIT_COMMIT" \
  --build-arg GIT_BRANCH="$GIT_BRANCH" \
  --build-arg GIT_DIRTY="$GIT_DIRTY" \
  --build-arg BUILD_DATE="$BUILD_DATE" \
  -t "$IMAGE_NAME:$IMAGE_TAG" \
  .

echo
echo "Image built: $IMAGE_NAME:$IMAGE_TAG"
