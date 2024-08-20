# A Multi-Threaded HTTPServer in C

## Running the server
The server takes a single-command line argument, an int, named _port_.

After building the files with `make`, you can start the httpserver with:

`./httpserver [-t threads] <port>`

## Multithreading
THe server uses a thread-pool design, which utilizes worker threads and a single dispatcher thread. This is implemented using the POSIX threads library and handles atomic and coherent requests.

## Thread Safety
In order to ensure coherent and atomic linearization, the server implements a thread-safe queue by utilizing reader-writer locks classes. These classes allow for multiple readers can request a resource but only a single writer can write to a resource at a time. Implements placing a shared lock, an exclusive lock, or removing a lock. (In other words it implements `flock()` from scratch.

## Execution
The server creates a socket, binds the socket to a port, and proceeds to listen for incoming connections. 
Ensure that the _port_ value is between 1 and 65535.

## Accepting and Processing Connections
Server repeatedly accepts connections made by clients to the port.
Processing connections use a simplified subset of the HTTP Protocol, only supporting `GET` and `PUT` requests. All requests are formatted as:

Method URI Version Header-Field Message-Body

where:

* **Method** refers to the request type (of which only GET and PUT are supported)
* **URI** is the Uniform Resource Idenfitier or in other words, the filename which you are trying to GET or PUT into
* **Version** refers to the version the server supports (in this case the only version so far is **1.1**)
* **[PUT-requests]** **Header-Field** refers to a key-value pair giving information about the message-field. Note that this is only supported for PUT requests and only supports `Content-Length`
* **[PUT-requests]** **Message-Body** refers to the message-body (in any format, ASCII, binary, etc) to be placed in a specified file. Note that this is only supported for PUT requests

These requests are also formatted to require `\r\n` special characters between sections.

Here are examples of valid GET and PUT requests:

`GET /foo.txt HTTP/1.1\r\n\r\n`

`Get /not.txt HTTP/1.1\r\n\r\n`


`PUT /foo.txt HTTP/1.1\r\nContent-Length: 21\r\n\r\nHello foo, I am World`

`PUT /new.txt HTTP/1.1\r\nContent-Length: 14\r\n\r\nHello\nI am new`

## Responses
Standard HTTP status codes are supported including: 200, 201, 400, 403, 404, 500, 501, 505.
