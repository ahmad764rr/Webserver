#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include <string>
#include <sys/types.h>

struct CgiTask {
    bool active;
    pid_t pid;
    int pipe_in_fd;
    int pipe_out_fd;
    std::string body_to_write;
    std::size_t body_written;
    std::string cgi_output;
    long start_time_ms;
    bool process_exited;
    int exit_status;

    CgiTask();
    void cleanup();
};

struct ClientConnection {
    int fd;
    std::size_t serverIndex;
    HttpRequest request;
    HttpResponse response;
    bool hasResponse;
    bool closeAfterSend;
    std::string carryBuffer;
    CgiTask cgiTask;

    // File streaming for non-blocking large file downloads
    int sendFileFd;
    std::string sendFileBuf;
    bool readEof;

    ClientConnection();
};

#endif
