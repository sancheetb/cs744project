# Key value store

This project implements a http server with CRUD functionality. The server stores key value pairs in a MySQL database and caches recently accessed key value pairs.

## Server components
- HTTP server and thread pool
- LRU Cache
- Connection pool

## HTTP server
It uses httplib library for handling HTTP requests. A variable number of server threads can be assigned (set to 12 by default). It defines standard RESTful endpoints for CRUD operations (create, read, update, delete)

example requests:

read: curl -v "http://localhost:8080/read?key=sancheet"

create: curl -v -X POST -d "key=hello&val=world" http://localhost:8080/create

update: curl -v -X PUT -d "key=hello&val=byebye" http://localhost:8080/update

delete: curl -v -X DELETE "http://localhost:8080/delete?key=hello"

## LRU Cache
Implemented using a doubly linked list to maintain a list of key value pairs and a hash map which contains keys and their corresponding node in linked list. This allows constant time insertion, deletion, update and read operations on LRU Cache.

## Connection Pool
MySQL connections are established when the server is started. This is to prevent the server threads from repeatedly establishing a new connection on each HTTP request. SQL connection is handled using standard C++/SQL connector API <mysql/jbdc>. SQL connection pointers are stored in a pool of connections. The connection pool class handles SQL connection distribution among threads. The pool consists of a queue of connections and concurrency control variables to ensure mutual exclusion.

## Architecture illustration


