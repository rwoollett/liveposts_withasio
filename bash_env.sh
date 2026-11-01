#!/usr/bin/env bash

# Path to your cstoken env file
svcEnvPath="$HOME/netproc_infra/env/liveposts/.env"
mypass=$1

if [[ ! -f "$svcEnvPath" ]]; then
    echo "ERROR: .env not found at $svcEnvPath"
    exit 1
fi

while IFS= read -r line; do
    # Skip empty lines and comments
    [[ -z "$line" || "$line" =~ ^# ]] && continue

    # Match KEY=VALUE
    if [[ "$line" =~ ^([^=]+)=(.*)$ ]]; then
        key="${BASH_REMATCH[1]}"
        value="${BASH_REMATCH[2]}"

        # Strip surrounding single quotes
        if [[ "$value" == \'*\' ]]; then
            value="${value:1:${#value}-2}"
        fi

        # Export into current shell session
        export "$key=$value"
        echo "Set $key"
    fi
done < "$svcEnvPath"

export REDIS_HOST=localhost
export NODE_PATH=/home/rwlltt/.nvm/versions/node/v22.23.1/bin/node
export PRERENDER_SCRIPT=./posts-vite-app/scripts/prerenderhydrate.mjs
export APIDB_HOST=localhost
export APIDB_PORT=5440
export MTLOG_LEVEL=debug

mypass="$1"
export TEST_COOKIE=$(
  curl -s -D - \
    -H "Content-Type: application/json" \
    -X POST http://localhost:3001/api/v1/users/signin \
    -d "{\"email\":\"temp@test.co.nz\",\"password\":\"$mypass\"}" \
  | awk '/^Set-Cookie:/ {print $2; exit}' \
  | sed 's/;$//'
)

echo "$TEST_COOKIE"



echo "Environment variables loaded for this Bash session."
