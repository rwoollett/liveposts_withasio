<h1 align="center">Live Posts Service Rest API with Boost Asio and PubSub Redis cache</h1>

<br />
Live Posts game which uses an api to create posts.
Uses Postgres for the ClientCS SQL database.
The API endpoint are on an asio network using async sockets with Boost ASIO and Context.
The API also has websocket for subscription to events made from the API for events that occur
when a players move is processed and a new board is pushed to the clients application interface.

<br/>
## Api Server
Dependencie on git repo apiserver as a submodule:
```
 git submodule add git@github.com:rwoollett/apiserver.git
```
## Post Static generation
Dependency on git repo posts-vite-app as a submodule
```
 git submodule add git@github.com:rwoollett/posts-vite-app.git
```
This is built in the docker image. Using npm install in the subfolder post-vite-app is /usr/src folder

<br/>

## CMake builder

## Create a cmake build folder:

cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=/usr/local -G "Unix Makefiles" . -B ./build-release
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=/usr/local -G "Unix Makefiles" . -B ./build-debug

## Use the built package to test:

cmake --build build-release --target LivePostSvc
cmake --build build-debug --target LivePostSvc

## Docker container

docker build -t livepostsvc:v1.0 .

# sample env for livepostsvc container

docker run -d -p3011:3011 --network="host" --env TTTDB_USER=postgres --env TTTDB_PASSWORD=&lt;password&gt; livepostsvc:v1.0

# Sample command to run:-

./build/LivePostSvc --threads 2 --root ./latest

## Uses Docker Compose

Make docker image in project root folder with:

```
docker build -t livepostsvc:v1.0 -f Dockerfile .
```

The docker compose will run the docker image livepostsvc:v1.0.
Run docker compose with:

```
docker-compose up
```

The compose runtime will generate:

- Redis cache (used for pubsub of events for apollo subscriptions as consumer of published events)
- PostGres database
- Network for the images to be located
- This TicTacToe API service

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
    │   ├── load.h
    │   └── main.cpp
    ├── livepostsvc          # Service source files
    │   ├── model            # Client CS data structures
    │   ├── nholmann         # JSON header
    │   ├── routes           # Route registered in ClientCS api
    │   ├── CMakeLists.txt
    │   ├── main.cpp         # Main entry point to start server
    │   └── *.cpp/*.h
    ├── apiserver            # API server library base for a API service
    │   ├── model            # Client CS data structures
    │   ├── nholmann         # JSON header
    │   ├── routes           # Route registered in ClientCS api
    │   ├── CMakeLists.txt
    │   ├── main.cpp         # Main entry point to start server
    │   └── *.cpp/*.h
    ├── .dockerignore
    ├── .gitignore
    ├── api.http
    ├── docker-compose.yml
    ├── Dockerfile
    └── README.md
```
