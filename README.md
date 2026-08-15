# Live Posts Service  
### REST API • Boost ASIO • Redis PubSub • PostgreSQL • WebSockets

A real‑time Live Posts service built with:

- **Boost ASIO** for async networking (HTTP + WebSockets)
- **Redis PubSub** for event distribution
- **PostgreSQL** for ClientCS SQL database
- **React/Vite** prerendered static pages (via `posts-vite-app` submodule)

---

## 📦 Static Post Generation

The React/Vite app is included as a git submodule:

```sh
git submodule add git@github.com:rwoollett/posts-vite-app.git
```

It is built in the docker image. Using npm install in the subfolder post-vite-app is /usr/src folder

<br/>

## CMake builder

### Create the cmake build folder

- Release: 

  `cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF`

- Debug:   

  `cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON`

## Use the built package to test:

- Debug:

  `cmake --build build/debug --target LivePostSvc`

## Docker container

### Build image
```sh
docker build -t livepostsvc:v1.0 .
```

With metadata

```
docker build \
  --build-arg GIT_COMMIT=$(git rev-parse HEAD) \
  --build-arg GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD) \
  --build-arg GIT_DIRTY=$(test -n "$(git status --porcelain)" && echo dirty || echo clean) \
  --build-arg BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ") \
  -t livepostsvc:v1.0 .
```

Inspect labels

```
docker inspect livepostsvc:v1.0 | jq '.[0].Config.Labels'
```

## Run container
```
docker run -d -p3011:3011 --network="host" --env TTTDB_USER=postgres --env TTTDB_PASSWORD=&lt;password&gt; livepostsvc:v1.0
```

## Run binary manually

```
./build/LivePostSvc --threads 2 --root ./latest
```

## Postgres database instance

When running doocker-compose, the Postgres database can be pushed from the local Postgres database.
In development a local Postgres instance is used, which is then pushed to the docker runtime instance.

```
npx prisma db push
```

Also the databases can be seeded with:

```
npx prisma db seed
```

Use the .env file to set the URL variable and use the env variable in ./src/prisma/schema.prisma and ./src/prisma/seed.ts.

## schema.prisma

```
datasource db {
  provider = "postgresql"
  url      = env("DATABASE_URL")
}

```

## seed.ts

```
  const prismaTest = new PrismaClient({
    datasources: {
      db: {
        url: process.env.DATABASE_TEST_URL
      }
    }
  });
```

<br />

# 🚀 Available Scripts

In the project directory, you can run:
<br />

## ⚡️ docker-compose up

This requires docker and docker compose installed on your system.

<br />

## Setup Redis cache for dev if not using docker-compose

```
docker run --rm --name test-redis -p 6379:6379 redis:6.2-alpine redis-server --loglevel warning --requirepass <your password here>
```

## Setup a Postgres for dev if not using docker-compose

Use a docker container to ease setup of postgres.
Sometimes users will have local postgres installed. It is not required and a docker container can be used.\
An env variable like this required: DATABASE_URL="postgresql://postgres:<password>@localhost:5432/cstoken?schema=public"

Check the port for connection, "-p <local port>:<image instance exposed port>" in the run command below.\
Usually 5432:5432 is always used. Postgres uses 5432 as the default exposed port in the running container.
Other local postgres instance could be using port 5432, so review your setup.

```
docker pull postgres:14.6
docker run --name cstoken -e POSTGRES_PASSWORD=password -d -p 5432:5432 postgres
```

## 🧪 test

LoadTest is load test runner.

<br />

# 🧬 Project structure

This is the structure of the files in the project:

```sh
    │
    ├── cpputest             # load test source files
    │   ├── CMakeLists.txt
    │   ├── load.cpp
    │   └── load.h
    ├── livepostsvc          # Service source files
    │   ├── prerender        # Prerender generation
    │   ├── routes           # Route registered in ClientCS api
    │   ├── CMakeLists.txt
    │   └── main.cpp         # Main entry point to start server
    ├── posts-vite-app       # Prerender static html
    ├── .dockerignore
    ├── .gitignore
    ├── bash_env.sh
    ├── build.sh
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── Dockerfile
    ├── Dockerfile.dev # *NB*: Only available if file reflex_linux_amd64.tar.gz (Reflex building)
    └── README.md
```
*__<sup>NB</sup>__* Dockerfile.dev only for Reflex building. File reflex_linux_amd64.tar.gz is required.
